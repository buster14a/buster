#include <buster/tests/compiler/frontend/c/c_test.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL void c_test_token(UnitTestArguments* arguments, UnitTestResult* outer_result, CLexResult lex, u64 index, CTokenKind kind, String8 spelling)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, index < lex.token_count);
    if (index < lex.token_count)
    {
        BUSTER_TEST(arguments, lex.tokens[index].kind == kind);
        BUSTER_STRING_TEST(arguments, c_token_spelling(lex.spelling_base, lex.tokens[index]), spelling);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL void c_test_preprocessed_token(UnitTestArguments* arguments, UnitTestResult* outer_result, CPreprocessResult preprocess, u64 index,
                                                   CTokenKind kind, String8 spelling)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, index < preprocess.token_count);
    if (index < preprocess.token_count)
    {
        BUSTER_TEST(arguments, preprocess.tokens[index].kind == kind);
        BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), spelling);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL CEntityId c_test_find_local_entity(CParseResult* parse, String8 name, CScopeId scope)
{
    for (u32 entity_index = 0; entity_index < parse->entity_count; entity_index += 1)
    {
        CEntity* entity = &parse->entities[entity_index];
        if (entity->kind == C_ENTITY_LOCAL && (scope.value == C_ID_UNDERLYING_INVALID || entity->scope.value == scope.value) &&
            string_equal(entity->name, name))
        {
            return (CEntityId){.value = entity_index};
        }
    }
    return C_ENTITY_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL IrFunction* c_test_find_ir_function(IrModule* module, String8 name)
{
    if (module)
    {
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (string_equal(function->name, name))
            {
                return function;
            }
        }
    }

    return 0;
}

BUSTER_GLOBAL_LOCAL u64 c_test_ir_bit_field_value(IrProgram* program, IrGlobal* global, IrField* field)
{
    if (!program || !global || !field || !field->is_bit_field || !field->bit_width || !global->bytes.pointer ||
        field->offset >= global->bytes.length)
    {
        return UINT64_MAX;
    }
    IrType* storage_type = ir_type_from_id(&program->types, field->type);
    if (!storage_type || !storage_type->layout.resolved || !storage_type->layout.size || storage_type->layout.size > sizeof(u64) ||
        storage_type->layout.size > global->bytes.length - field->offset || field->bit_width > storage_type->layout.size * 8 ||
        field->bit_offset > storage_type->layout.size * 8 - field->bit_width)
    {
        return UINT64_MAX;
    }
    u64 storage = 0;
    for (u64 byte_index = 0; byte_index < storage_type->layout.size; byte_index += 1)
    {
        u64 target_index = program->data_layout.endianness == TARGET_ENDIAN_LITTLE ? byte_index : storage_type->layout.size - byte_index - 1;
        storage |= (u64)global->bytes.pointer[field->offset + byte_index] << (u32)(target_index * 8);
    }
    u64 mask = field->bit_width == 64 ? UINT64_MAX : ((u64)1 << field->bit_width) - 1;
    return (storage >> field->bit_offset) & mask;
}

BUSTER_GLOBAL_LOCAL u32 c_test_ir_direct_call_count(IrProgram* program, IrFunction* function, String8 target_name)
{
    u32 result;
    if (!program || !function)
    {
        result = 0;
    }
    else
    {
        u32 count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = function->instructions + instruction_index;
            if (instruction->opcode != IR_OPCODE_CALL)
            {
                continue;
            }
            IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
            count += symbol && string_equal(symbol->name, target_name);
        }
        result = count;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 c_test_ir_call_count(IrFunction* function)
{
    u32 result;
    if (!function)
    {
        result = 0;
    }
    else
    {
        u32 count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            count += function->instructions[instruction_index].opcode == IR_OPCODE_CALL;
        }
        result = count;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL CIRLowerResult c_test_lower_source(Arena* arena, String8 source, String8 source_path, Target target,
                                                       CPreprocessResult* preprocess_out, CParseResult* parse_out)
{
    *preprocess_out = c_preprocess(arena, source,
                                   (CPreprocessOptions){
                                       .target = target,
                                       .data_layout = target_data_layout(target),
                                   });
    *parse_out = c_parse(arena, *preprocess_out);
    CIRLowerResult result;
    if (preprocess_out->diagnostic_count || parse_out->diagnostic_count)
    {
        result = (CIRLowerResult){0};
    }
    else
    {
        result = c_lower_to_ir(arena, source_path, *preprocess_out, *parse_out, target);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void c_test_append_source(char8* destination, u64 capacity, u64* length, String8 source)
{
    if (!destination || !length || *length > capacity || source.length > capacity - *length)
    {
        return;
    }
    memcpy(destination + *length, source.pointer, source.length);
    *length += source.length;
}

BUSTER_GLOBAL_LOCAL u64 c_test_translate_source_scalar(String8 source, char8* translated)
{
    u64 input = 0;
    u64 output = 0;
    while (input < source.length)
    {
        char8 character = source.pointer[input];
        if (character == '\\' && input + 1 < source.length)
        {
            u64 splice_length = 0;
            if (source.pointer[input + 1] == '\n')
            {
                splice_length = 2;
            }
            else if (source.pointer[input + 1] == '\r')
            {
                splice_length = input + 2 < source.length && source.pointer[input + 2] == '\n' ? 3 : 2;
            }
            if (splice_length)
            {
                input += splice_length;
                continue;
            }
        }
        if (character == '\r')
        {
            translated[output++] = '\n';
            input += input + 1 < source.length && source.pointer[input + 1] == '\n' ? 2 : 1;
        }
        else
        {
            translated[output++] = character;
            input += 1;
        }
    }
    return output;
}

BUSTER_GLOBAL_LOCAL bool c_test_translate_source_paths_agree(Arena* arena, String8 source)
{
    u64 arena_position = arena->position;
    char8* scalar = arena_allocate(arena, char8, source.length + 1);
    u64 scalar_length = c_test_translate_source_scalar(source, scalar);
    CLexResult dispatched = c_lex(arena, source);
    bool result = c_test_translate_plain_run_paths_agree(source) && dispatched.translated_source.length == scalar_length;
    if (result && scalar_length)
    {
        result = memcmp(dispatched.translated_source.pointer, scalar, scalar_length) == 0;
    }
    arena->position = arena_position;
    return result;
}

BUSTER_GLOBAL_LOCAL void c_test_auto_type_diagnostic(UnitTestArguments* arguments, UnitTestResult* outer_result, String8 source,
                                                     CPreprocessDialect dialect, CDiagnosticKind kind, String8 message)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult preprocess = c_preprocess(temporary.arena, source,
                                                (CPreprocessOptions){
                                                    .target = target_native,
                                                    .data_layout = target_data_layout(target_native),
                                                    .dialect = dialect,
                                                });
    CParseResult parse = c_parse(temporary.arena, preprocess);
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 1);
    if (parse.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, parse.diagnostics[0].kind == kind);
        BUSTER_STRING_TEST(arguments, parse.diagnostics[0].message, message);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
    scratch_end(temporary);
}

BUSTER_GLOBAL_LOCAL void c_test_cleanup_diagnostic(UnitTestArguments* arguments, UnitTestResult* outer_result, String8 source,
                                                   CPreprocessDialect dialect, String8 message)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult preprocess = c_preprocess(temporary.arena, source,
                                                (CPreprocessOptions){
                                                    .target = target_native,
                                                    .data_layout = target_data_layout(target_native),
                                                    .dialect = dialect,
                                                });
    CParseResult parse = c_parse(temporary.arena, preprocess);
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 1);
    if (parse.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_CLEANUP_ATTRIBUTE);
        BUSTER_STRING_TEST(arguments, parse.diagnostics[0].message, message);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
    scratch_end(temporary);
}

BUSTER_GLOBAL_LOCAL void c_test_case_range_lower_diagnostic(UnitTestArguments* arguments, UnitTestResult* outer_result, String8 source, String8 message)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult preprocess = c_preprocess(temporary.arena, source,
                                                (CPreprocessOptions){
                                                    .target = target_native,
                                                    .data_layout = target_data_layout(target_native),
                                                    .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                });
    CParseResult parse = c_parse(temporary.arena, preprocess);
    CIRLowerResult lowered = {0};
    if (!parse.diagnostic_count)
    {
        lowered = c_lower_to_ir(temporary.arena, S8("case-range-invalid.c"), preprocess, parse, target_native);
    }
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.diagnostic_count == 1);
    if (lowered.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, lowered.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
        BUSTER_STRING_TEST(arguments, lowered.diagnostics[0].message, message);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
    scratch_end(temporary);
}

BUSTER_GLOBAL_LOCAL void c_test_result_add(UnitTestResult* result, UnitTestResult child)
{
    result->test_count += child.test_count;
    result->succeeded_test_count += child.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_local_static_aggregates(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena local_static_temporary = scratch_begin(0, 0);
    CPreprocessResult local_static_tokens = c_preprocess(
        local_static_temporary.arena,
        S8("struct StaticPair { int first; int second; };"
           " struct StaticNested { int values[2]; };"
           " enum { STATIC_LOCAL_INDEX = 7 };"
           " static int static_local_probe(void) {"
           " int result = 0;"
           " static const int scalar = 7;"
           " static void *self_pointer = &self_pointer;"
           " static const char *string_pointer = \"ok\";"
           " static const unsigned char *cast_string_pointer = (const unsigned char *)\"ok\";"
           " static void *void_string_pointer = \"ok\";"
           " static const struct StaticPair table[] = {"
           " [3] = { 1, 2 }, [STATIC_LOCAL_INDEX] = { 3, 4 } };"
           " static const struct StaticPair brace_elision[] = { 1, 2, 3, 4 };"
           " static const int nested_elision[][2] = { 1, 2, 3, 4 };"
           " static const struct StaticNested chained[] = { [3].values[1] = 7 };"
           " static const int cast_index[] = { [(unsigned char)256] = 1 };"
           " static const int duplicate_designator[] = { [3] = 4, [1] = 2, [3] = 5 };"
           "\n#define ADD_MACRO_STATIC(value) { static const int macro_table[] = { value }; result += macro_table[0]; }\n"
           "\n#define ONE(value) { static const int macro_same[] = { value }; result += macro_same[0]; }\n"
           "\n#define BOTH(first_value, second_value) ONE(first_value) ONE(second_value)\n"
           " ADD_MACRO_STATIC(17) ADD_MACRO_STATIC(19)"
           " BOTH(1, 2)"
           " { static const int duplicate[] = { 11 }; result += duplicate[0]; }"
           " { static const int duplicate[] = { 13 }; result += duplicate[0]; }"
           " return scalar + table[STATIC_LOCAL_INDEX].second + result + brace_elision[1].second + nested_elision[1][1] + chained[3].values[1] + cast_index[0] + duplicate_designator[3] + (self_pointer == (void *)&self_pointer) + (string_pointer[1] == 'k') + (cast_string_pointer[1] == 'k' ? 0 : 1) + (((const char *)void_string_pointer)[1] == 'k' ? 0 : 1); }"
           " int main(void) { return static_local_probe() == 94 ? 0 : 1; }\n"),
        (CPreprocessOptions){0});
    CParseResult local_static_parse = c_parse(local_static_temporary.arena, local_static_tokens);
    CIRLowerResult local_static_ir =
        c_lower_to_ir(local_static_temporary.arena, S8("local-static-aggregate.c"), local_static_tokens, local_static_parse, target_native);
    BUSTER_TEST(arguments, local_static_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, local_static_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, local_static_ir.diagnostic_count == 0);
    if (local_static_ir.program)
    {
        IrModule* module = &local_static_ir.program->modules[0];
        IrGlobal* scalar_global = 0;
        IrGlobal* self_pointer_global = 0;
        IrGlobal* string_pointer_global = 0;
        IrGlobal* cast_string_pointer_global = 0;
        IrGlobal* void_string_pointer_global = 0;
        IrGlobal* table_global = 0;
        IrGlobal* brace_elision_global = 0;
        IrGlobal* nested_elision_global = 0;
        IrGlobal* chained_global = 0;
        IrGlobal* cast_index_global = 0;
        IrGlobal* duplicate_designator_global = 0;
        IrSymbolId duplicate_symbols[2] = {IR_SYMBOL_ID_INVALID, IR_SYMBOL_ID_INVALID};
        IrSymbolId macro_symbols[2] = {IR_SYMBOL_ID_INVALID, IR_SYMBOL_ID_INVALID};
        IrSymbolId nested_macro_symbols[2] = {IR_SYMBOL_ID_INVALID, IR_SYMBOL_ID_INVALID};
        u32 duplicate_count = 0;
        u32 macro_count = 0;
        u32 nested_macro_count = 0;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&local_static_ir.program->symbols, global->symbol);
            if (!symbol || !string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.")))
            {
                continue;
            }
            if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.scalar.")))
            {
                scalar_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.self_pointer.")))
            {
                self_pointer_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.string_pointer.")))
            {
                string_pointer_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.cast_string_pointer.")))
            {
                cast_string_pointer_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.void_string_pointer.")))
            {
                void_string_pointer_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.table.")))
            {
                table_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.brace_elision.")))
            {
                brace_elision_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.nested_elision.")))
            {
                nested_elision_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.chained.")))
            {
                chained_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.cast_index.")))
            {
                cast_index_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.duplicate_designator.")))
            {
                duplicate_designator_global = global;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.macro_table.")) && macro_count < 2)
            {
                macro_symbols[macro_count++] = global->symbol;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.macro_same.")) && nested_macro_count < 2)
            {
                nested_macro_symbols[nested_macro_count++] = global->symbol;
            }
            else if (string_starts_with_sequence(symbol->link_name, S8(".L.static_local_probe.duplicate.")) && duplicate_count < 2)
            {
                duplicate_symbols[duplicate_count++] = global->symbol;
            }
            BUSTER_TEST(arguments, symbol->linkage == IR_LINKAGE_INTERNAL);
            BUSTER_TEST(arguments, self_pointer_global == global || string_pointer_global == global || cast_string_pointer_global == global ||
                                             void_string_pointer_global == global || global->is_read_only);
        }
        BUSTER_TEST(arguments, scalar_global != 0);
        BUSTER_TEST(arguments, self_pointer_global != 0);
        BUSTER_TEST(arguments, string_pointer_global != 0);
        BUSTER_TEST(arguments, cast_string_pointer_global != 0);
        BUSTER_TEST(arguments, void_string_pointer_global != 0);
        BUSTER_TEST(arguments, table_global != 0);
        BUSTER_TEST(arguments, brace_elision_global != 0);
        BUSTER_TEST(arguments, nested_elision_global != 0);
        BUSTER_TEST(arguments, chained_global != 0);
        BUSTER_TEST(arguments, cast_index_global != 0);
        BUSTER_TEST(arguments, duplicate_designator_global != 0);
        BUSTER_TEST(arguments, duplicate_count == 2);
        BUSTER_TEST(arguments, macro_count == 2);
        BUSTER_TEST(arguments, nested_macro_count == 2);
        BUSTER_TEST(arguments, duplicate_symbols[0].value != duplicate_symbols[1].value);
        BUSTER_TEST(arguments, macro_symbols[0].value != macro_symbols[1].value);
        BUSTER_TEST(arguments, nested_macro_symbols[0].value != nested_macro_symbols[1].value);
        if (scalar_global)
        {
            BUSTER_TEST(arguments, scalar_global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
            BUSTER_TEST(arguments, scalar_global->initializer_bits == 7);
        }
        if (self_pointer_global)
        {
            BUSTER_TEST(arguments, self_pointer_global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
            BUSTER_TEST(arguments, self_pointer_global->initializer_symbol.value == self_pointer_global->symbol.value);
        }
        if (string_pointer_global)
        {
            BUSTER_TEST(arguments, string_pointer_global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
            IrSymbol* string_symbol = ir_symbol_from_id(&local_static_ir.program->symbols, string_pointer_global->initializer_symbol);
            BUSTER_TEST(arguments, string_symbol && string_starts_with_sequence(string_symbol->link_name, S8(".L.cstr.")));
        }
        if (cast_string_pointer_global)
        {
            BUSTER_TEST(arguments, cast_string_pointer_global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
            IrSymbol* string_symbol = ir_symbol_from_id(&local_static_ir.program->symbols, cast_string_pointer_global->initializer_symbol);
            BUSTER_TEST(arguments, string_symbol && string_starts_with_sequence(string_symbol->link_name, S8(".L.cstr.")));
        }
        if (void_string_pointer_global)
        {
            BUSTER_TEST(arguments, void_string_pointer_global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
            IrSymbol* string_symbol = ir_symbol_from_id(&local_static_ir.program->symbols, void_string_pointer_global->initializer_symbol);
            BUSTER_TEST(arguments, string_symbol && string_starts_with_sequence(string_symbol->link_name, S8(".L.cstr.")));
        }
        if (table_global)
        {
            BUSTER_TEST(arguments, table_global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
            BUSTER_TEST(arguments, table_global->bytes.length == 64);
            if (table_global->bytes.pointer && table_global->bytes.length == 64)
            {
                u32 first = 0;
                u32 second = 0;
                memcpy(&first, table_global->bytes.pointer + 7 * 8, sizeof(first));
                memcpy(&second, table_global->bytes.pointer + 7 * 8 + 4, sizeof(second));
                BUSTER_TEST(arguments, first == 3);
                BUSTER_TEST(arguments, second == 4);
            }
        }
        if (brace_elision_global)
        {
            BUSTER_TEST(arguments, brace_elision_global->bytes.length == 16);
            u32 second = 0;
            memcpy(&second, brace_elision_global->bytes.pointer + 12, sizeof(second));
            BUSTER_TEST(arguments, second == 4);
        }
        if (nested_elision_global)
        {
            BUSTER_TEST(arguments, nested_elision_global->bytes.length == 16);
            u32 second = 0;
            memcpy(&second, nested_elision_global->bytes.pointer + 12, sizeof(second));
            BUSTER_TEST(arguments, second == 4);
        }
        if (chained_global)
        {
            BUSTER_TEST(arguments, chained_global->bytes.length == 32);
            u32 value = 0;
            memcpy(&value, chained_global->bytes.pointer + 28, sizeof(value));
            BUSTER_TEST(arguments, value == 7);
        }
        if (cast_index_global)
        {
            BUSTER_TEST(arguments, cast_index_global->bytes.length == 4);
            u32 value = 0;
            memcpy(&value, cast_index_global->bytes.pointer, sizeof(value));
            BUSTER_TEST(arguments, value == 1);
        }
        if (duplicate_designator_global)
        {
            BUSTER_TEST(arguments, duplicate_designator_global->bytes.length == 16);
            u32 value = 0;
            memcpy(&value, duplicate_designator_global->bytes.pointer + 12, sizeof(value));
            BUSTER_TEST(arguments, value == 5);
        }
        BUSTER_TEST(arguments, local_static_ir.canonical_ir_certified);
        BUSTER_TEST(arguments, ir_validate_canonical_module(local_static_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(local_static_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_static_range_designators(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult preprocess = c_preprocess(
        temporary.arena,
        S8("struct RangePair { int first; int second; };"
           " struct RangeNested { int values[3]; };"
           " union RangeUnion { int first; int second; };"
           " struct RangeEmpty {};"
           " struct RangeZeroNested { struct RangeEmpty values[18446744073709551615ULL]; };"
           " static int range_target;"
           " static const int range_scalar[] = { [0 ... 2] = 3, [1] = 4, [2 ... 4] = 5 };"
           " static const struct RangePair range_pairs[] = { [1 ... 3] = { 7, 8 }, [2].first = 9 };"
           " static const struct RangeNested range_nested[] = { [1 ... 2].values[1 ... 2] = 6 };"
           " static const struct RangeNested range_nested_values[] = { [0 ... 1] = (struct RangeNested){ .values = { [0 ... 2] = 9 } } };"
           " struct RangeBits { unsigned first : 3; unsigned second : 5; };"
           " static const struct RangeBits range_bits[] = { [0 ... 2].first = 5, [1].second = 17 };"
           " static int *range_ptrs[] = { [0 ... 2] = &range_target };"
           " static int *range_overlap_ptrs[] = { [0 ... 2] = &range_target, [1] = 0 };"
           " static const int range_singleton[] = { [2 ... 2] = 11 };"
           " static union RangeUnion range_unions[] = { [0 ... 2].second = 7, [1].first = 9 };"
           " static struct RangeEmpty range_zero[18446744073709551615ULL] = { [0 ... 18446744073709551614ULL] = {} };"
           " static struct RangeZeroNested range_zero_nested[18446744073709551615ULL] = { [0 ... 18446744073709551614ULL].values[0 ... 18446744073709551614ULL] = {} };"
           " static int range_probe(void) {"
           " static const int local_ranges[] = { [1 ... 3] = 4, [2] = 5 };"
           " return local_ranges[0] + local_ranges[1] + local_ranges[2] + local_ranges[3]; }"
           " int main(void) { return range_probe() == 13 ? 0 : 1; }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
            .dialect = C_PREPROCESS_DIALECT_GNU23,
        });
    CParseResult parse = c_parse(temporary.arena, preprocess);
    CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("static-range-designators.c"), preprocess, parse, target_native);
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
    if (lowered.program)
    {
        IrModule* module = &lowered.program->modules[0];
        IrGlobal* scalar = 0;
        IrGlobal* pairs = 0;
        IrGlobal* nested = 0;
        IrGlobal* nested_values = 0;
        IrGlobal* bits = 0;
        IrGlobal* pointers = 0;
        IrGlobal* overlap_pointers = 0;
        IrGlobal* singleton = 0;
        IrGlobal* unions = 0;
        IrGlobal* zero = 0;
        IrGlobal* zero_nested = 0;
        IrGlobal* locals = 0;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&lowered.program->symbols, global->symbol);
            if (!symbol)
            {
                continue;
            }
            if (string_equal(symbol->name, S8("range_scalar"))) scalar = global;
            if (string_equal(symbol->name, S8("range_pairs"))) pairs = global;
            if (string_equal(symbol->name, S8("range_nested"))) nested = global;
            if (string_equal(symbol->name, S8("range_nested_values"))) nested_values = global;
            if (string_equal(symbol->name, S8("range_bits"))) bits = global;
            if (string_equal(symbol->name, S8("range_ptrs"))) pointers = global;
            if (string_equal(symbol->name, S8("range_overlap_ptrs"))) overlap_pointers = global;
            if (string_equal(symbol->name, S8("range_singleton"))) singleton = global;
            if (string_equal(symbol->name, S8("range_unions"))) unions = global;
            if (string_equal(symbol->name, S8("range_zero"))) zero = global;
            if (string_equal(symbol->name, S8("range_zero_nested"))) zero_nested = global;
            if (string_equal(symbol->name, S8("local_ranges")) || string_starts_with_sequence(symbol->link_name, S8(".L.range_probe.local_ranges."))) locals = global;
        }
        BUSTER_TEST(arguments, scalar != 0);
        BUSTER_TEST(arguments, pairs != 0);
        BUSTER_TEST(arguments, nested != 0);
        BUSTER_TEST(arguments, nested_values != 0);
        BUSTER_TEST(arguments, bits != 0);
        BUSTER_TEST(arguments, pointers != 0);
        BUSTER_TEST(arguments, overlap_pointers != 0);
        BUSTER_TEST(arguments, singleton != 0);
        BUSTER_TEST(arguments, unions != 0);
        BUSTER_TEST(arguments, zero != 0);
        BUSTER_TEST(arguments, zero_nested != 0);
        BUSTER_TEST(arguments, locals != 0);
        if (scalar)
        {
            BUSTER_TEST(arguments, scalar->bytes.length == 5 * sizeof(u32));
            if (scalar->bytes.pointer && scalar->bytes.length == 5 * sizeof(u32))
            {
                u32 expected[5] = {3, 4, 5, 5, 5};
                for (u32 index = 0; index < 5; index += 1)
                {
                    u32 value = 0;
                    memcpy(&value, scalar->bytes.pointer + index * sizeof(value), sizeof(value));
                    BUSTER_TEST(arguments, value == expected[index]);
                }
            }
        }
        if (pairs)
        {
            BUSTER_TEST(arguments, pairs->bytes.length == 4 * 2 * sizeof(u32));
            if (pairs->bytes.pointer && pairs->bytes.length == 4 * 2 * sizeof(u32))
            {
                u32 expected[8] = {0, 0, 7, 8, 9, 8, 7, 8};
                for (u32 index = 0; index < 8; index += 1)
                {
                    u32 value = 0;
                    memcpy(&value, pairs->bytes.pointer + index * sizeof(value), sizeof(value));
                    BUSTER_TEST(arguments, value == expected[index]);
                }
            }
        }
        if (nested)
        {
            BUSTER_TEST(arguments, nested->bytes.length == 3 * 3 * sizeof(u32));
            if (nested->bytes.pointer && nested->bytes.length == 3 * 3 * sizeof(u32))
            {
                for (u32 index = 0; index < 3; index += 1)
                {
                    for (u32 value_index = 0; value_index < 3; value_index += 1)
                    {
                        u32 value = 0;
                        memcpy(&value, nested->bytes.pointer + (index * 3 + value_index) * sizeof(value), sizeof(value));
                        BUSTER_TEST(arguments, value == ((index != 0 && value_index != 0) ? 6 : 0));
                    }
                }
            }
        }
        if (pointers)
        {
            BUSTER_TEST(arguments, pointers->relocation_count == 3);
            if (pointers->relocations && pointers->relocation_count == 3)
            {
                for (u32 index = 0; index < pointers->relocation_count; index += 1)
                {
                    IrGlobalRelocation relocation = pointers->relocations[index];
                    IrSymbol* target = ir_symbol_from_id(&lowered.program->symbols, relocation.symbol);
                    BUSTER_TEST(arguments, target && string_equal(target->name, S8("range_target")));
                    BUSTER_TEST(arguments, relocation.offset == index * target_data_layout(target_native).pointer.size);
                }
            }
        }
        if (nested_values)
        {
            BUSTER_TEST(arguments, nested_values->bytes.length == 2 * 3 * sizeof(u32));
            if (nested_values->bytes.pointer && nested_values->bytes.length == 2 * 3 * sizeof(u32))
            {
                for (u32 index = 0; index < 2 * 3; index += 1)
                {
                    u32 value = 0;
                    memcpy(&value, nested_values->bytes.pointer + index * sizeof(value), sizeof(value));
                    BUSTER_TEST(arguments, value == 9);
                }
            }
        }
        if (bits)
        {
            BUSTER_TEST(arguments, bits->bytes.length == 3 * sizeof(u32));
            if (bits->bytes.pointer && bits->bytes.length == 3 * sizeof(u32))
            {
                u32 expected[3] = {5, 5 | (17u << 3), 5};
                for (u32 index = 0; index < 3; index += 1)
                {
                    u32 value = 0;
                    memcpy(&value, bits->bytes.pointer + index * sizeof(value), sizeof(value));
                    BUSTER_TEST(arguments, value == expected[index]);
                }
            }
        }
        if (overlap_pointers)
        {
            BUSTER_TEST(arguments, overlap_pointers->relocation_count == 2);
            if (overlap_pointers->relocations && overlap_pointers->relocation_count == 2)
            {
                for (u32 index = 0; index < overlap_pointers->relocation_count; index += 1)
                {
                    IrGlobalRelocation relocation = overlap_pointers->relocations[index];
                    IrSymbol* target = ir_symbol_from_id(&lowered.program->symbols, relocation.symbol);
                    BUSTER_TEST(arguments, target && string_equal(target->name, S8("range_target")));
                    BUSTER_TEST(arguments, relocation.offset == index * 2 * target_data_layout(target_native).pointer.size);
                }
            }
        }
        if (singleton)
        {
            BUSTER_TEST(arguments, singleton->bytes.length == 3 * sizeof(u32));
            if (singleton->bytes.pointer && singleton->bytes.length == 3 * sizeof(u32))
            {
                u32 value = 0;
                memcpy(&value, singleton->bytes.pointer + 2 * sizeof(value), sizeof(value));
                BUSTER_TEST(arguments, value == 11);
            }
        }
        if (unions)
        {
            BUSTER_TEST(arguments, unions->bytes.length == 3 * sizeof(u32));
            if (unions->bytes.pointer && unions->bytes.length == 3 * sizeof(u32))
            {
                u32 expected[3] = {7, 9, 7};
                for (u32 index = 0; index < 3; index += 1)
                {
                    u32 value = 0;
                    memcpy(&value, unions->bytes.pointer + index * sizeof(value), sizeof(value));
                    BUSTER_TEST(arguments, value == expected[index]);
                }
            }
        }
        if (zero)
        {
            BUSTER_TEST(arguments, zero->bytes.length == 0);
            BUSTER_TEST(arguments, zero->relocation_count == 0);
        }
        if (zero_nested)
        {
            BUSTER_TEST(arguments, zero_nested->bytes.length == 0);
            BUSTER_TEST(arguments, zero_nested->relocation_count == 0);
        }
        if (locals)
        {
            BUSTER_TEST(arguments, locals->bytes.length == 4 * sizeof(u32));
            if (locals->bytes.pointer && locals->bytes.length == 4 * sizeof(u32))
            {
                u32 expected[4] = {0, 4, 5, 4};
                for (u32 index = 0; index < 4; index += 1)
                {
                    u32 value = 0;
                    memcpy(&value, locals->bytes.pointer + index * sizeof(value), sizeof(value));
                    BUSTER_TEST(arguments, value == expected[index]);
                }
            }
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
    }
    c_test_case_range_lower_diagnostic(
        arguments, &result,
        S8("int runtime_range_scalar(void) { return ((int[4]){ [1 ... 2] = 3 })[1]; }\n"),
        S8("in function 'runtime_range_scalar': range designators are only supported for static aggregate initializers"));
    c_test_case_range_lower_diagnostic(
        arguments, &result,
        S8("struct RuntimeRangePair { int first; int second; };"
           " int runtime_range_aggregate(void) {"
           " return ((struct RuntimeRangePair[2]){ [0 ... 1] = { 3, 4 } })[1].second; }\n"),
        S8("in function 'runtime_range_aggregate': range designators are only supported for static aggregate initializers"));
    c_test_auto_type_diagnostic(
        arguments, &result,
        S8("int strict_initializer_range_c11(void) { static int values[2] = { [0 ... 1] = 1 }; return values[0]; }\n"),
        C_PREPROCESS_DIALECT_C11, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
        S8("in function 'strict_initializer_range_c11': GNU initializer ranges are only available in GNU dialects"));
    c_test_auto_type_diagnostic(
        arguments, &result,
        S8("int strict_initializer_range_c17(void) { static int values[2] = { [0 ... 1] = 1 }; return values[0]; }\n"),
        C_PREPROCESS_DIALECT_C17, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
        S8("in function 'strict_initializer_range_c17': GNU initializer ranges are only available in GNU dialects"));
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_u64_initializer_slots(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    u64 count = (u64)UINT32_MAX + 1;
    IrType element = {
        .kind = IR_TYPE_INTEGER,
        .layout =
            {
                .size = 1,
                .alignment = 1,
                .resolved = true,
            },
    };
    IrType array = {
        .element_type = {.value = 7},
        .kind = IR_TYPE_ARRAY,
        .element_count = count,
        .layout =
            {
                .size = count,
                .alignment = 1,
                .resolved = true,
            },
    };
    BUSTER_TEST(arguments, c_test_ir_initializer_slot_count(&array) == count);
    BUSTER_TEST(arguments, array.element_count > UINT32_MAX);
    BUSTER_TEST(arguments, element.layout.size == 1);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_parse_storage_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena parse_growth_temporary = scratch_begin(0, 0);
    u32 typedef_depth = 20;
    u32 object_count = 140;
    u32 string_count = 24;
    u64 source_capacity = BUSTER_KB(128);
    char8* source_buffer = arena_allocate(parse_growth_temporary.arena, char8, source_capacity);
    u64 source_length = 0;
    String8 previous_alias = S8("int");
    for (u32 depth = 0; depth < typedef_depth; depth += 1)
    {
        String8 alias = string_format(parse_growth_temporary.arena, S8("A{u32}"), depth);
        c_test_append_source(source_buffer, source_capacity, &source_length,
                             string_format(parse_growth_temporary.arena, S8("typedef {S8} {S8}[];\n"), previous_alias, alias));
        previous_alias = alias;
    }
    for (u32 object_index = 0; object_index < object_count; object_index += 1)
    {
        c_test_append_source(source_buffer, source_capacity, &source_length,
                             string_format(parse_growth_temporary.arena, S8("static {S8} growth_array_{u32} = {{ 1 }};\n"), previous_alias,
                                            object_index));
    }
    for (u32 string_index = 0; string_index < string_count; string_index += 1)
    {
        c_test_append_source(source_buffer, source_capacity, &source_length,
                             string_format(parse_growth_temporary.arena, S8("static const char *growth_string_{u32} = \"x\";\n"), string_index));
    }
    c_test_append_source(source_buffer, source_capacity, &source_length,
                         S8("static int growth_inferred[] = { [1 + 2] = 7 };\n"
                            "static int (*growth_nested_function)(int, int [1 + 2]);\n"));
    c_test_append_source(source_buffer, source_capacity, &source_length, S8("int parse_growth_use(void) { return growth_string_0[0]; }\n"));
    String8 growth_source = {
        .pointer = source_buffer,
        .length = source_length,
    };
    CPreprocessResult growth_tokens = c_preprocess(parse_growth_temporary.arena, growth_source,
                                                   (CPreprocessOptions){
                                                       .target = target_native,
                                                       .data_layout = target_data_layout(target_native),
                                                   });
    CParseResult growth_parse = c_parse(parse_growth_temporary.arena, growth_tokens);
    CIRLowerResult growth_ir = c_lower_to_ir(parse_growth_temporary.arena, S8("parse-storage-growth.c"), growth_tokens, growth_parse, target_native);
    u32 open_bracket_count = 0;
    for (u32 token_index = 0; token_index < growth_tokens.token_count; token_index += 1)
    {
        CToken token = growth_tokens.tokens[token_index];
        open_bracket_count += token.kind == C_TOKEN_PUNCTUATOR && string_equal(c_token_spelling(growth_tokens.spelling_base, token), S8("["));
    }
    u64 old_type_capacity = (u64)growth_tokens.token_count * 2 + 1;
    u64 old_array_bound_capacity = (u64)open_bracket_count + 1;
    BUSTER_TEST(arguments, growth_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, growth_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, growth_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, growth_parse.arena == parse_growth_temporary.arena);
    BUSTER_TEST(arguments, growth_parse.type_count > old_type_capacity);
    BUSTER_TEST(arguments, growth_parse.type_capacity >= growth_parse.type_count);
    BUSTER_TEST(arguments, growth_parse.type_capacity < (u64)growth_parse.type_count * 2 + 2);
    BUSTER_TEST(arguments, growth_parse.array_bound_count > old_array_bound_capacity);
    BUSTER_TEST(arguments, growth_parse.array_bound_capacity >= growth_parse.array_bound_count);
    BUSTER_TEST(arguments, growth_parse.array_bound_capacity < (u64)growth_parse.array_bound_count * 2 + 2);
    if (growth_parse.declaration_count)
    {
        CDeclaration declaration = growth_parse.declarations[growth_parse.declaration_count - 1];
        BUSTER_TEST(arguments, declaration.type.value < growth_parse.type_count);
        if (declaration.type.value < growth_parse.type_count)
        {
            CType type = growth_parse.types[declaration.type.value];
            BUSTER_TEST(arguments, type.kind == C_TYPE_FUNCTION);
        }
    }
    CDeclaration inferred_declaration = {0};
    CDeclaration nested_function_declaration = {0};
    bool found_inferred_declaration = false;
    bool found_nested_function_declaration = false;
    for (u32 declaration_index = 0; declaration_index < growth_parse.declaration_count; declaration_index += 1)
    {
        CDeclaration declaration = growth_parse.declarations[declaration_index];
        if (string_equal(declaration.name, S8("growth_inferred")))
        {
            inferred_declaration = declaration;
            found_inferred_declaration = true;
        }
        if (string_equal(declaration.name, S8("growth_nested_function")))
        {
            nested_function_declaration = declaration;
            found_nested_function_declaration = true;
        }
    }
    BUSTER_TEST(arguments, found_inferred_declaration);
    BUSTER_TEST(arguments, found_nested_function_declaration);
    if (found_inferred_declaration && inferred_declaration.type.value < growth_parse.type_count)
    {
        CType inferred_type = growth_parse.types[inferred_declaration.type.value];
        BUSTER_TEST(arguments, inferred_type.kind == C_TYPE_ARRAY);
        BUSTER_TEST(arguments, inferred_type.array_bound < growth_parse.array_bound_count);
        if (inferred_type.array_bound < growth_parse.array_bound_count)
        {
            CArrayBound inferred_bound = growth_parse.array_bounds[inferred_type.array_bound];
            BUSTER_TEST(arguments, inferred_bound.has_inferred_count);
            BUSTER_TEST(arguments, inferred_bound.inferred_count == 4);
        }
    }
    if (found_nested_function_declaration && nested_function_declaration.type.value < growth_parse.type_count)
    {
        CType nested_pointer = growth_parse.types[nested_function_declaration.type.value];
        BUSTER_TEST(arguments, nested_pointer.kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, nested_pointer.element_type.value < growth_parse.type_count);
        if (nested_pointer.element_type.value < growth_parse.type_count)
        {
            CType nested_function = growth_parse.types[nested_pointer.element_type.value];
            BUSTER_TEST(arguments, nested_function.kind == C_TYPE_FUNCTION);
            BUSTER_TEST(arguments, nested_function.parameter_count == 2);
        }
    }
    scratch_end(parse_growth_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_type_parse_rollback_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    bool rollback_growth = false;
    bool rollback_pointer = false;
    bool rollback_old_tag = false;
    bool rollback_grown_tag = false;
    TemporalArena rollback_growth_temporary = scratch_begin(0, 0);
    // The seam performs the real result-array growth between the recorded
    // tag mutation and the failed speculative parse rollback.
    BUSTER_TEST(arguments, c_test_type_parse_rollback_after_growth(rollback_growth_temporary.arena, &rollback_growth, &rollback_pointer,
                                                                    &rollback_old_tag, &rollback_grown_tag));
    BUSTER_TEST(arguments, rollback_growth);
    BUSTER_TEST(arguments, rollback_pointer);
    BUSTER_TEST(arguments, rollback_old_tag);
    BUSTER_TEST(arguments, rollback_grown_tag);
    scratch_end(rollback_growth_temporary);

    TemporalArena rollback_parse_temporary = scratch_begin(0, 0);
    CPreprocessResult rollback_parse_tokens = c_preprocess(
        rollback_parse_temporary.arena,
        S8("struct RollbackTag; struct Broken { struct RollbackTag { int value; } bad[1; };\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult rollback_parse = c_parse(rollback_parse_temporary.arena, rollback_parse_tokens);
    BUSTER_TEST(arguments, rollback_parse_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, rollback_parse.diagnostic_count != 0);
    scratch_end(rollback_parse_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_aggregate_corrections(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena aggregate_correction_temporary = scratch_begin(0, 0);
    CPreprocessResult aggregate_correction_tokens = c_preprocess(
        aggregate_correction_temporary.arena,
        S8("static int aggregate_correction_target;"
           " union AggregateCorrectionUnion { int first; int second; };"
           " struct AggregateCorrectionOuter { struct { int promoted; } ; int tail; };"
           " struct AggregateCorrectionOverride { int head; struct { int first; int second; } sub; int *pointer; };"
           " struct AggregateCorrectionInnerPointer { int *p; };"
           " struct AggregateCorrectionRelocationOuter { int *head; struct AggregateCorrectionInnerPointer inner; };"
           " struct AggregateCorrectionString { char text[4]; int value; };"
           " struct AggregateCorrectionExactString { char text[3]; int value; };"
           " struct AggregateCorrectionZero { int first; int second; };"
           " struct AggregateCorrectionBits { unsigned : 3; unsigned x : 3; unsigned y : 5; };"
           " struct AggregateCorrectionZeroBits { unsigned : 0; unsigned x : 3; };"
           " struct AggregateCorrectionPointer { int *pointer; };"
           " static int aggregate_correction_relocation_a;"
           " static int aggregate_correction_relocation_b;"
           " static int aggregate_correction_relocation_c;"
           " static union AggregateCorrectionUnion union_values[] = { 1, 2 };"
           " static struct AggregateCorrectionOverride override_value = { .sub = { 1, 2 }, .sub = { 3 }, .pointer = &aggregate_correction_target, .pointer = 0 };"
           " static char braced_string[] = { \"abc\" };"
           " static char exact_braced_string[3] = { \"abc\" };"
           " static struct AggregateCorrectionString nested_string = { \"abc\", 7 };"
           " static struct AggregateCorrectionExactString exact_nested_string = { { \"abc\" }, 9 };"
           " static struct AggregateCorrectionRelocationOuter relocation_override = { .head = &aggregate_correction_relocation_a, .inner = (struct AggregateCorrectionInnerPointer){ .p = &aggregate_correction_relocation_b, .p = &aggregate_correction_relocation_c } };"
           " static struct AggregateCorrectionZero mutable_zero = { 0 };"
           " static _Thread_local struct AggregateCorrectionZero tls_zero = { 0 };"
           " static const struct AggregateCorrectionZero const_zero = { 0 };"
           " static int mutable_scalar_zero = 0;"
           " static _Thread_local int tls_scalar_zero = 0;"
           " static const int const_scalar_zero = 0;"
           " static int *mutable_pointer_zero = (int *)0;"
           " static _Thread_local int *tls_pointer_zero = (int *)0;"
           " static int *const const_pointer_zero = (int *)0;"
           " static char empty_string[1] = \"\";"
           " static _Thread_local char tls_empty_string[1] = \"\";"
           " static const char const_empty_string[1] = \"\";"
           " static struct AggregateCorrectionBits positional_bits = { 5, 9 };"
           " static struct AggregateCorrectionZeroBits zero_width_bits = { 5 };"
           " static struct AggregateCorrectionZero nonzero = { 0, 1 };"
           " static struct AggregateCorrectionPointer relocation_guard = { &aggregate_correction_target };"
           " static struct AggregateCorrectionOuter promoted_values[] = { [3].promoted = 7 };"
           " int aggregate_correction_main(void) { return union_values[1].first + braced_string[3] + exact_braced_string[2] + nested_string.value + exact_nested_string.value + promoted_values[3].promoted + override_value.sub.first + (relocation_override.head == &aggregate_correction_relocation_a) + (relocation_override.inner.p == &aggregate_correction_relocation_c) + (positional_bits.x != 5) + (positional_bits.y != 9) + (zero_width_bits.x != 5); }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult aggregate_correction_parse = c_parse(aggregate_correction_temporary.arena, aggregate_correction_tokens);
    CIRLowerResult aggregate_correction_ir = c_lower_to_ir(aggregate_correction_temporary.arena, S8("aggregate-corrections.c"),
                                                           aggregate_correction_tokens, aggregate_correction_parse, target_native);
    BUSTER_TEST(arguments, aggregate_correction_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_correction_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_correction_ir.diagnostic_count == 0);
    if (aggregate_correction_ir.program)
    {
        IrModule* module = &aggregate_correction_ir.program->modules[0];
        IrGlobal* union_global = 0;
        IrGlobal* override_global = 0;
        IrGlobal* string_global = 0;
        IrGlobal* exact_string_global = 0;
        IrGlobal* nested_string_global = 0;
        IrGlobal* exact_nested_string_global = 0;
        IrGlobal* relocation_override_global = 0;
        IrGlobal* mutable_zero_global = 0;
        IrGlobal* tls_zero_global = 0;
        IrGlobal* const_zero_global = 0;
        IrGlobal* mutable_scalar_zero_global = 0;
        IrGlobal* tls_scalar_zero_global = 0;
        IrGlobal* const_scalar_zero_global = 0;
        IrGlobal* mutable_pointer_zero_global = 0;
        IrGlobal* tls_pointer_zero_global = 0;
        IrGlobal* const_pointer_zero_global = 0;
        IrGlobal* empty_string_global = 0;
        IrGlobal* tls_empty_string_global = 0;
        IrGlobal* const_empty_string_global = 0;
        IrGlobal* positional_bits_global = 0;
        IrGlobal* zero_width_bits_global = 0;
        IrGlobal* nonzero_global = 0;
        IrGlobal* relocation_guard_global = 0;
        IrGlobal* promoted_global = 0;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&aggregate_correction_ir.program->symbols, global->symbol);
            if (!symbol)
            {
                continue;
            }
            union_global = string_equal(symbol->name, S8("union_values")) ? global : union_global;
            override_global = string_equal(symbol->name, S8("override_value")) ? global : override_global;
            string_global = string_equal(symbol->name, S8("braced_string")) ? global : string_global;
            exact_string_global = string_equal(symbol->name, S8("exact_braced_string")) ? global : exact_string_global;
            nested_string_global = string_equal(symbol->name, S8("nested_string")) ? global : nested_string_global;
            exact_nested_string_global = string_equal(symbol->name, S8("exact_nested_string")) ? global : exact_nested_string_global;
            relocation_override_global = string_equal(symbol->name, S8("relocation_override")) ? global : relocation_override_global;
            mutable_zero_global = string_equal(symbol->name, S8("mutable_zero")) ? global : mutable_zero_global;
            tls_zero_global = string_equal(symbol->name, S8("tls_zero")) ? global : tls_zero_global;
            const_zero_global = string_equal(symbol->name, S8("const_zero")) ? global : const_zero_global;
            mutable_scalar_zero_global = string_equal(symbol->name, S8("mutable_scalar_zero")) ? global : mutable_scalar_zero_global;
            tls_scalar_zero_global = string_equal(symbol->name, S8("tls_scalar_zero")) ? global : tls_scalar_zero_global;
            const_scalar_zero_global = string_equal(symbol->name, S8("const_scalar_zero")) ? global : const_scalar_zero_global;
            mutable_pointer_zero_global = string_equal(symbol->name, S8("mutable_pointer_zero")) ? global : mutable_pointer_zero_global;
            tls_pointer_zero_global = string_equal(symbol->name, S8("tls_pointer_zero")) ? global : tls_pointer_zero_global;
            const_pointer_zero_global = string_equal(symbol->name, S8("const_pointer_zero")) ? global : const_pointer_zero_global;
            empty_string_global = string_equal(symbol->name, S8("empty_string")) ? global : empty_string_global;
            tls_empty_string_global = string_equal(symbol->name, S8("tls_empty_string")) ? global : tls_empty_string_global;
            const_empty_string_global = string_equal(symbol->name, S8("const_empty_string")) ? global : const_empty_string_global;
            positional_bits_global = string_equal(symbol->name, S8("positional_bits")) ? global : positional_bits_global;
            zero_width_bits_global = string_equal(symbol->name, S8("zero_width_bits")) ? global : zero_width_bits_global;
            nonzero_global = string_equal(symbol->name, S8("nonzero")) ? global : nonzero_global;
            relocation_guard_global = string_equal(symbol->name, S8("relocation_guard")) ? global : relocation_guard_global;
            promoted_global = string_equal(symbol->name, S8("promoted_values")) ? global : promoted_global;
        }
        BUSTER_TEST(arguments, union_global != 0);
        BUSTER_TEST(arguments, override_global != 0);
        BUSTER_TEST(arguments, string_global != 0);
        BUSTER_TEST(arguments, mutable_zero_global != 0);
        BUSTER_TEST(arguments, tls_zero_global != 0);
        BUSTER_TEST(arguments, const_zero_global != 0);
        BUSTER_TEST(arguments, mutable_scalar_zero_global != 0);
        BUSTER_TEST(arguments, tls_scalar_zero_global != 0);
        BUSTER_TEST(arguments, const_scalar_zero_global != 0);
        BUSTER_TEST(arguments, mutable_pointer_zero_global != 0);
        BUSTER_TEST(arguments, tls_pointer_zero_global != 0);
        BUSTER_TEST(arguments, const_pointer_zero_global != 0);
        BUSTER_TEST(arguments, empty_string_global != 0);
        BUSTER_TEST(arguments, tls_empty_string_global != 0);
        BUSTER_TEST(arguments, const_empty_string_global != 0);
        BUSTER_TEST(arguments, positional_bits_global != 0);
        BUSTER_TEST(arguments, zero_width_bits_global != 0);
        BUSTER_TEST(arguments, nonzero_global != 0);
        BUSTER_TEST(arguments, relocation_guard_global != 0);
        BUSTER_TEST(arguments, promoted_global != 0);
        if (mutable_zero_global)
        {
            BUSTER_TEST(arguments, mutable_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !mutable_zero_global->is_read_only && !mutable_zero_global->is_thread_local);
            BUSTER_TEST(arguments, mutable_zero_global->bytes.length == 0 && mutable_zero_global->relocation_count == 0);
        }
        if (tls_zero_global)
        {
            BUSTER_TEST(arguments, tls_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !tls_zero_global->is_read_only && tls_zero_global->is_thread_local);
            BUSTER_TEST(arguments, tls_zero_global->bytes.length == 0 && tls_zero_global->relocation_count == 0);
        }
        if (const_zero_global)
        {
            BUSTER_TEST(arguments, const_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
            BUSTER_TEST(arguments, const_zero_global->is_read_only && const_zero_global->bytes.length != 0);
            for (u64 byte_index = 0; byte_index < const_zero_global->bytes.length; byte_index += 1)
            {
                BUSTER_TEST(arguments, const_zero_global->bytes.pointer[byte_index] == 0);
            }
        }
        if (mutable_scalar_zero_global)
        {
            BUSTER_TEST(arguments, mutable_scalar_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !mutable_scalar_zero_global->is_read_only && !mutable_scalar_zero_global->is_thread_local);
        }
        if (tls_scalar_zero_global)
        {
            BUSTER_TEST(arguments, tls_scalar_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !tls_scalar_zero_global->is_read_only && tls_scalar_zero_global->is_thread_local);
        }
        if (const_scalar_zero_global)
        {
            BUSTER_TEST(arguments, const_scalar_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
            BUSTER_TEST(arguments, const_scalar_zero_global->is_read_only && const_scalar_zero_global->initializer_bits == 0);
        }
        if (mutable_pointer_zero_global)
        {
            BUSTER_TEST(arguments, mutable_pointer_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !mutable_pointer_zero_global->is_read_only && !mutable_pointer_zero_global->is_thread_local);
        }
        if (tls_pointer_zero_global)
        {
            BUSTER_TEST(arguments, tls_pointer_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !tls_pointer_zero_global->is_read_only && tls_pointer_zero_global->is_thread_local);
        }
        if (const_pointer_zero_global)
        {
            BUSTER_TEST(arguments, const_pointer_zero_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, const_pointer_zero_global->is_read_only);
        }
        if (empty_string_global)
        {
            BUSTER_TEST(arguments, empty_string_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !empty_string_global->is_read_only && !empty_string_global->is_thread_local);
            BUSTER_TEST(arguments, empty_string_global->bytes.length == 0 && empty_string_global->relocation_count == 0);
        }
        if (tls_empty_string_global)
        {
            BUSTER_TEST(arguments, tls_empty_string_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            BUSTER_TEST(arguments, !tls_empty_string_global->is_read_only && tls_empty_string_global->is_thread_local);
            BUSTER_TEST(arguments, tls_empty_string_global->bytes.length == 0 && tls_empty_string_global->relocation_count == 0);
        }
        if (const_empty_string_global)
        {
            BUSTER_TEST(arguments, const_empty_string_global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
            BUSTER_TEST(arguments, const_empty_string_global->is_read_only && const_empty_string_global->bytes.length == 1);
            BUSTER_TEST(arguments, const_empty_string_global->bytes.pointer && const_empty_string_global->bytes.pointer[0] == 0);
        }
        if (positional_bits_global)
        {
            IrType* bits_type = ir_type_from_id(&aggregate_correction_ir.program->types, positional_bits_global->type);
            BUSTER_TEST(arguments, bits_type && bits_type->field_count == 3);
            if (bits_type && bits_type->field_count == 3)
            {
                BUSTER_TEST(arguments, c_test_ir_bit_field_value(aggregate_correction_ir.program, positional_bits_global, bits_type->fields + 1) == 5);
                BUSTER_TEST(arguments, c_test_ir_bit_field_value(aggregate_correction_ir.program, positional_bits_global, bits_type->fields + 2) == 9);
            }
        }
        if (zero_width_bits_global)
        {
            IrType* bits_type = ir_type_from_id(&aggregate_correction_ir.program->types, zero_width_bits_global->type);
            BUSTER_TEST(arguments, bits_type && bits_type->field_count == 2);
            if (bits_type && bits_type->field_count == 2)
            {
                BUSTER_TEST(arguments, c_test_ir_bit_field_value(aggregate_correction_ir.program, zero_width_bits_global, bits_type->fields + 1) == 5);
            }
        }
        if (nonzero_global)
        {
            BUSTER_TEST(arguments, nonzero_global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
            BUSTER_TEST(arguments, nonzero_global->bytes.length != 0 && nonzero_global->bytes.pointer[sizeof(u32)] == 1);
        }
        if (relocation_guard_global)
        {
            BUSTER_TEST(arguments, relocation_guard_global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
            BUSTER_TEST(arguments, relocation_guard_global->relocation_count == 1);
        }
        if (union_global && union_global->bytes.pointer && union_global->bytes.length == 2 * sizeof(u32))
        {
            u32 first = 0;
            u32 second = 0;
            memcpy(&first, union_global->bytes.pointer, sizeof(first));
            memcpy(&second, union_global->bytes.pointer + sizeof(second), sizeof(second));
            BUSTER_TEST(arguments, first == 1);
            BUSTER_TEST(arguments, second == 2);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
        if (string_global && string_global->bytes.pointer && string_global->bytes.length == 4)
        {
            BUSTER_TEST(arguments, memcmp(string_global->bytes.pointer, "abc\0", 4) == 0);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
        if (exact_string_global && exact_string_global->bytes.pointer && exact_string_global->bytes.length == 3)
        {
            BUSTER_TEST(arguments, memcmp(exact_string_global->bytes.pointer, "abc", 3) == 0);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
        if (nested_string_global && nested_string_global->bytes.pointer)
        {
            BUSTER_TEST(arguments, nested_string_global->bytes.length >= 4 + sizeof(u32));
            BUSTER_TEST(arguments, memcmp(nested_string_global->bytes.pointer, "abc\0", 4) == 0);
            u32 value = 0;
            memcpy(&value, nested_string_global->bytes.pointer + 4, sizeof(value));
            BUSTER_TEST(arguments, value == 7);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
        if (exact_nested_string_global && exact_nested_string_global->bytes.pointer)
        {
            BUSTER_TEST(arguments, exact_nested_string_global->bytes.length >= 3 + sizeof(u32));
            BUSTER_TEST(arguments, memcmp(exact_nested_string_global->bytes.pointer, "abc", 3) == 0);
            u32 value = 0;
            memcpy(&value, exact_nested_string_global->bytes.pointer + 4, sizeof(value));
            BUSTER_TEST(arguments, value == 9);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
        if (override_global)
        {
            IrType* type = ir_type_from_id(&aggregate_correction_ir.program->types, override_global->type);
            u64 sub_offset = UINT64_MAX;
            u64 pointer_offset = UINT64_MAX;
            if (type)
            {
                for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
                {
                    if (string_equal(type->fields[field_index].name, S8("sub"))) sub_offset = type->fields[field_index].offset;
                    if (string_equal(type->fields[field_index].name, S8("pointer"))) pointer_offset = type->fields[field_index].offset;
                }
            }
            u32 sub_first = 0;
            u32 sub_second = 0;
            BUSTER_TEST(arguments, sub_offset != UINT64_MAX && pointer_offset != UINT64_MAX && override_global->bytes.pointer);
            if (override_global->bytes.pointer && sub_offset != UINT64_MAX && pointer_offset != UINT64_MAX)
            {
                memcpy(&sub_first, override_global->bytes.pointer + sub_offset, sizeof(sub_first));
                memcpy(&sub_second, override_global->bytes.pointer + sub_offset + sizeof(sub_second), sizeof(sub_second));
                BUSTER_TEST(arguments, sub_first == 3);
                BUSTER_TEST(arguments, sub_second == 0);
                BUSTER_TEST(arguments, override_global->relocation_count == 0);
                for (u64 byte_index = 0; byte_index < target_data_layout(target_native).pointer.size; byte_index += 1)
                {
                    BUSTER_TEST(arguments, override_global->bytes.pointer[pointer_offset + byte_index] == 0);
                }
            }
        }
        if (relocation_override_global)
        {
            IrType* type = ir_type_from_id(&aggregate_correction_ir.program->types, relocation_override_global->type);
            u64 inner_offset = UINT64_MAX;
            if (type)
            {
                for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
                {
                    if (string_equal(type->fields[field_index].name, S8("inner")))
                    {
                        inner_offset = type->fields[field_index].offset;
                    }
                }
            }
            BUSTER_TEST(arguments, relocation_override_global->relocation_count == 2 && inner_offset != UINT64_MAX);
            bool found_head = false;
            bool found_inner = false;
            for (u32 relocation_index = 0; relocation_index < relocation_override_global->relocation_count; relocation_index += 1)
            {
                IrGlobalRelocation relocation = relocation_override_global->relocations[relocation_index];
                IrSymbol* symbol = ir_symbol_from_id(&aggregate_correction_ir.program->symbols, relocation.symbol);
                found_head |= relocation.offset == 0 && symbol && string_equal(symbol->name, S8("aggregate_correction_relocation_a"));
                found_inner |= relocation.offset == inner_offset && symbol && string_equal(symbol->name, S8("aggregate_correction_relocation_c"));
            }
            BUSTER_TEST(arguments, found_head && found_inner);
        }
        if (promoted_global && promoted_global->bytes.pointer && promoted_global->bytes.length == 4 * sizeof(u64))
        {
            u32 value = 0;
            memcpy(&value, promoted_global->bytes.pointer + 3 * sizeof(u64), sizeof(value));
            BUSTER_TEST(arguments, value == 7);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(aggregate_correction_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(aggregate_correction_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_brace_designators(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena brace_designator_temporary = scratch_begin(0, 0);
    CPreprocessResult brace_designator_tokens = c_preprocess(
        brace_designator_temporary.arena,
        S8("struct BraceDesignatorA { int x[3]; };"
           " struct BraceDesignatorB { struct BraceDesignatorA a; int z; };"
           " static struct BraceDesignatorB scalar = { .a.x[1] = 7, .z = 10 };"
           " static struct BraceDesignatorB fixed[3] = { [2].a.x[1] = 7, [2].z = 10 };"
           " static struct BraceDesignatorB inferred[] = { [2].a.x[1] = 7, [2].z = 10 };"
           " int brace_designator_main(void) { return scalar.a.x[1] + scalar.z + fixed[2].a.x[1] + fixed[2].z + inferred[2].a.x[1] + inferred[2].z; }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult brace_designator_parse = c_parse(brace_designator_temporary.arena, brace_designator_tokens);
    CIRLowerResult brace_designator_ir = c_lower_to_ir(brace_designator_temporary.arena, S8("brace-designators.c"), brace_designator_tokens,
                                                        brace_designator_parse, target_native);
    BUSTER_TEST(arguments, brace_designator_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, brace_designator_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, brace_designator_ir.diagnostic_count == 0);
    if (brace_designator_ir.program)
    {
        IrModule* module = &brace_designator_ir.program->modules[0];
        IrGlobal* scalar_global = 0;
        IrGlobal* fixed_global = 0;
        IrGlobal* inferred_global = 0;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&brace_designator_ir.program->symbols, global->symbol);
            if (!symbol)
            {
                continue;
            }
            scalar_global = string_equal(symbol->name, S8("scalar")) ? global : scalar_global;
            fixed_global = string_equal(symbol->name, S8("fixed")) ? global : fixed_global;
            inferred_global = string_equal(symbol->name, S8("inferred")) ? global : inferred_global;
        }
        BUSTER_TEST(arguments, scalar_global != 0);
        BUSTER_TEST(arguments, fixed_global != 0);
        BUSTER_TEST(arguments, inferred_global != 0);
        if (scalar_global)
        {
            IrType* scalar_type = ir_type_from_id(&brace_designator_ir.program->types, scalar_global->type);
            IrField* a_field = 0;
            IrField* z_field = 0;
            if (scalar_type)
            {
                for (u32 field_index = 0; field_index < scalar_type->field_count; field_index += 1)
                {
                    a_field = string_equal(scalar_type->fields[field_index].name, S8("a")) ? scalar_type->fields + field_index : a_field;
                    z_field = string_equal(scalar_type->fields[field_index].name, S8("z")) ? scalar_type->fields + field_index : z_field;
                }
            }
            IrType* a_type = a_field ? ir_type_from_id(&brace_designator_ir.program->types, a_field->type) : 0;
            IrField* x_field = 0;
            if (a_type)
            {
                for (u32 field_index = 0; field_index < a_type->field_count; field_index += 1)
                {
                    x_field = string_equal(a_type->fields[field_index].name, S8("x")) ? a_type->fields + field_index : x_field;
                }
            }
            u64 x_offset = a_field && x_field ? a_field->offset + x_field->offset + sizeof(u32) : UINT64_MAX;
            u64 z_offset = z_field ? z_field->offset : UINT64_MAX;
            bool scalar_offsets_valid = scalar_global->bytes.pointer && scalar_global->bytes.length >= sizeof(u32) &&
                                        x_offset <= scalar_global->bytes.length - sizeof(u32) && z_offset <= scalar_global->bytes.length - sizeof(u32);
            BUSTER_TEST(arguments, scalar_offsets_valid);
            if (scalar_offsets_valid)
            {
                u32 x = 0;
                u32 z = 0;
                memcpy(&x, scalar_global->bytes.pointer + x_offset, sizeof(x));
                memcpy(&z, scalar_global->bytes.pointer + z_offset, sizeof(z));
                BUSTER_TEST(arguments, x == 7 && z == 10);
            }
        }
        if (fixed_global)
        {
            IrType* fixed_type = ir_type_from_id(&brace_designator_ir.program->types, fixed_global->type);
            BUSTER_TEST(arguments, fixed_type && fixed_type->kind == IR_TYPE_ARRAY && fixed_type->element_count == 3);
        }
        if (inferred_global)
        {
            IrType* inferred_type = ir_type_from_id(&brace_designator_ir.program->types, inferred_global->type);
            BUSTER_TEST(arguments, inferred_type && inferred_type->kind == IR_TYPE_ARRAY && inferred_type->element_count == 3);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(brace_designator_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(brace_designator_temporary);
    return result;
}

// The C23 attribute specifier in every position the grammar allows it, read
// as a parse rather than as a program: tests/basic_c_c23_attributes.c proves
// the decorated declarations still behave, and this proves they registered at
// all -- with the declared name and type rather than the attribute's -- and
// that no identifier inside a sequence leaked out as a use of something.  That
// was the original failure: an attributed declaration produced no entity, and
// the error surfaced later and elsewhere as "undeclared identifier".
//
// The source is parsed under C17 as well as C23 because the syntax reaches the
// frontend through system headers that spell it unconditionally, so it is
// accepted in every dialect the way clang accepts it.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_c23_attribute_positions(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 attribute_source = S8(
        "[[maybe_unused]] static int one_list = 1;"
        "[[maybe_unused]] [[deprecated]] static int two_lists = 2;"
        "[[maybe_unused, deprecated]] static int two_names = 4;"
        "[[gnu::unused]] static int scoped = 8;"
        "[[deprecated(\"superseded, see one_list\")]] static int with_arguments = 16;"
        "static int trailing [[maybe_unused]] = 32;"
        "struct [[deprecated]] Tagged { int first [[maybe_unused]]; int second; };"
        "union [[maybe_unused]] Choice { int as_integer; unsigned as_unsigned; };"
        "enum [[deprecated]] Counted {"
        " counted_first [[deprecated]] = 7, counted_second,"
        " counted_third [[maybe_unused]] = 20, counted_fourth };"
        "typedef int attributed_integer [[maybe_unused]];"
        "[[maybe_unused]];"
        "static int parameters([[maybe_unused]] int before, int after [[maybe_unused]])"
        " { return before + after; }"
        "static int statements(int value) {"
        " [[maybe_unused]] int local = value;"
        " [[maybe_unused]] plain_label: local += 1;"
        " if (value) [[maybe_unused]] guarded_label: local += 2;"
        " if (value > 1000) [[unlikely]] { local += 4; }"
        " for ([[maybe_unused]] int index = 0; index < 2; index += 1) { local += 8; }"
        " switch (value) { case 0: local += 16; [[fallthrough]];"
        " case 1: local += 32; break; default: local += 64; break; }"
        " return local; }"
        "int attribute_positions_main(void) {"
        " struct Tagged tagged = {1, 2}; union Choice choice = {3};"
        " attributed_integer typed = 4;"
        " return one_list + two_lists + two_names + scoped + with_arguments + trailing"
        " + tagged.first + tagged.second + choice.as_integer + typed"
        " + counted_first + counted_second + counted_third + counted_fourth"
        " + parameters(5, 6) + statements(0); }\n");
    CPreprocessDialect attribute_dialects[] = {
        C_PREPROCESS_DIALECT_C23,
        C_PREPROCESS_DIALECT_C17,
    };
    for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(attribute_dialects); dialect_index += 1)
    {
        TemporalArena attribute_temporary = scratch_begin(0, 0);
        CPreprocessResult attribute_tokens = c_preprocess(attribute_temporary.arena, attribute_source,
                                                          (CPreprocessOptions){
                                                              .target = target_native,
                                                              .data_layout = target_data_layout(target_native),
                                                              .dialect = attribute_dialects[dialect_index],
                                                          });
        CParseResult attribute_parse = c_parse(attribute_temporary.arena, attribute_tokens);
        CIRLowerResult attribute_ir = c_lower_to_ir(attribute_temporary.arena, S8("c23-attribute-positions.c"), attribute_tokens, attribute_parse,
                                                    target_native);
        BUSTER_TEST(arguments, attribute_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, attribute_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, attribute_ir.diagnostic_count == 0);
        // Nothing spelled inside a sequence may become an entity, and every
        // identifier the parse did record as a use must have bound to one.
        // An attribute name leaking into either table is the shape of the
        // original defect.
        for (u32 entity_index = 0; entity_index < attribute_parse.entity_count; entity_index += 1)
        {
            String8 name = attribute_parse.entities[entity_index].name;
            BUSTER_TEST(arguments, !string_equal(name, S8("maybe_unused")) && !string_equal(name, S8("deprecated")) &&
                                       !string_equal(name, S8("fallthrough")) && !string_equal(name, S8("unlikely")) &&
                                       !string_equal(name, S8("gnu")) && !string_equal(name, S8("unused")));
        }
        for (u32 use_index = 0; use_index < attribute_parse.identifier_use_count; use_index += 1)
        {
            BUSTER_TEST(arguments, attribute_parse.identifier_uses[use_index].entity.value != C_ID_UNDERLYING_INVALID);
        }
        // The declarations the attributes decorate must have registered with
        // their own names and kinds.
        bool found_one_list = false;
        bool found_trailing = false;
        bool found_typedef = false;
        bool found_parameters = false;
        bool found_statements = false;
        for (u32 declaration_index = 0; declaration_index < attribute_parse.declaration_count; declaration_index += 1)
        {
            CDeclaration declaration = attribute_parse.declarations[declaration_index];
            bool typed = declaration.type.value != C_ID_UNDERLYING_INVALID;
            found_one_list |= string_equal(declaration.name, S8("one_list")) && declaration.kind == C_DECLARATION_OBJECT && typed;
            found_trailing |= string_equal(declaration.name, S8("trailing")) && declaration.kind == C_DECLARATION_OBJECT && typed;
            found_typedef |= string_equal(declaration.name, S8("attributed_integer")) && declaration.kind == C_DECLARATION_TYPEDEF && typed;
            found_parameters |=
                string_equal(declaration.name, S8("parameters")) && declaration.kind == C_DECLARATION_FUNCTION && declaration.parameter_count == 2;
            found_statements |= string_equal(declaration.name, S8("statements")) && declaration.kind == C_DECLARATION_FUNCTION && typed;
        }
        BUSTER_TEST(arguments, found_one_list && found_trailing && found_typedef && found_parameters && found_statements);
        // The enumerators take the values their initializers give and the
        // implicit successors of those, with the sequences between each name
        // and its '=' stepped over rather than read as part of the value.
        bool found_counted_first = false;
        bool found_counted_second = false;
        bool found_counted_third = false;
        bool found_counted_fourth = false;
        for (u32 entity_index = 0; entity_index < attribute_parse.entity_count; entity_index += 1)
        {
            CEntity* entity = &attribute_parse.entities[entity_index];
            if (entity->kind != C_ENTITY_ENUMERATOR)
            {
                continue;
            }
            found_counted_first |= string_equal(entity->name, S8("counted_first")) && entity->constant_value == 7;
            found_counted_second |= string_equal(entity->name, S8("counted_second")) && entity->constant_value == 8;
            found_counted_third |= string_equal(entity->name, S8("counted_third")) && entity->constant_value == 20;
            found_counted_fourth |= string_equal(entity->name, S8("counted_fourth")) && entity->constant_value == 21;
        }
        BUSTER_TEST(arguments, found_counted_first && found_counted_second && found_counted_third && found_counted_fourth);
        if (attribute_ir.program)
        {
            IrModule* module = &attribute_ir.program->modules[0];
            BUSTER_TEST(arguments, ir_validate_canonical_module(attribute_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(attribute_temporary);
    }
    return result;
}

// [[noreturn]] is the one C23 attribute buster acts on, and the reason the
// syntax had to be parsed rather than only tolerated: c_ir_noreturn_marker_in_range
// has always had a [[ branch, but nothing could reach it.  A call to a
// noreturn callee ends control flow, so the caller's block is terminated as
// unreachable instead of falling through to a return.  The unmarked control
// is what makes that a proof rather than an observation.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_c23_attribute_noreturn(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena noreturn_temporary = scratch_begin(0, 0);
    CPreprocessResult noreturn_tokens = c_preprocess(noreturn_temporary.arena,
                                                      S8("[[noreturn]] void die_marked(int status);"
                                                         "[[__gnu__::__noreturn__]] void die_scoped(int status);"
                                                         "void die_plain(int status);"
                                                         "int through_marked(int status) { die_marked(status); }"
                                                         "int through_scoped(int status) { die_scoped(status); }"
                                                         "int through_plain(int status) { die_plain(status); return 0; }\n"),
                                                      (CPreprocessOptions){
                                                          .target = target_native,
                                                          .data_layout = target_data_layout(target_native),
                                                          .dialect = C_PREPROCESS_DIALECT_C23,
                                                      });
    CParseResult noreturn_parse = c_parse(noreturn_temporary.arena, noreturn_tokens);
    CIRLowerResult noreturn_ir =
        c_lower_to_ir(noreturn_temporary.arena, S8("c23-attribute-noreturn.c"), noreturn_tokens, noreturn_parse, target_native);
    BUSTER_TEST(arguments, noreturn_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, noreturn_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, noreturn_ir.diagnostic_count == 0);
    if (noreturn_ir.program)
    {
        IrModule* module = &noreturn_ir.program->modules[0];
        u32 checked = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            bool marked = string_equal(function->name, S8("through_marked")) || string_equal(function->name, S8("through_scoped"));
            bool plain = string_equal(function->name, S8("through_plain"));
            if (!marked && !plain)
            {
                continue;
            }
            bool unreachable = false;
            bool returns = false;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                unreachable |= function->instructions[instruction_index].opcode == IR_OPCODE_UNREACHABLE;
                returns |= function->instructions[instruction_index].opcode == IR_OPCODE_RETURN;
            }
            // The marked callers end in the trap and never return; the plain
            // one returns and never traps.  Both halves matter: without the
            // second, a compiler that marked everything noreturn would pass.
            BUSTER_TEST(arguments, marked ? (unreachable && !returns) : (returns && !unreachable));
            checked += 1;
        }
        BUSTER_TEST(arguments, checked == 3);
        // No canonical-validation assertion here, unlike the sibling tests.
        // A caller whose block ends in the trap keeps the dead tail of the
        // return sequence behind it, so the module reports
        // IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR.  That is the shape the
        // GNU spelling has always produced -- replacing the two attributes
        // above with __attribute__((noreturn)) yields the identical code --
        // so it is a pre-existing property of the noreturn lowering rather
        // than anything the C23 syntax introduced, and asserting on it here
        // would be asserting on an unrelated contract.
    }
    scratch_end(noreturn_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_c23_empty_initializers(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena c23_empty_initializer_temporary = scratch_begin(0, 0);
    CPreprocessResult c23_empty_initializer_tokens = c_preprocess(
        c23_empty_initializer_temporary.arena,
        S8(" static int scalar = {};"
           " static _Thread_local int tls_scalar = {};"
           " static const int const_scalar = {};"
           " static int *pointer = {};"
           " static _Thread_local int *tls_pointer = {};"
           " static int *const const_pointer = {};"
           " int c23_empty_initializer_main(void) { return scalar + tls_scalar + const_scalar + (pointer != 0) + (tls_pointer != 0) + (const_pointer != 0); }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
            .dialect = C_PREPROCESS_DIALECT_C23,
        });
    CParseResult c23_empty_initializer_parse = c_parse(c23_empty_initializer_temporary.arena, c23_empty_initializer_tokens);
    CIRLowerResult c23_empty_initializer_ir =
        c_lower_to_ir(c23_empty_initializer_temporary.arena, S8("c23-empty-initializers.c"), c23_empty_initializer_tokens,
                      c23_empty_initializer_parse, target_native);
    BUSTER_TEST(arguments, c23_empty_initializer_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, c23_empty_initializer_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, c23_empty_initializer_ir.diagnostic_count == 0);
    if (c23_empty_initializer_ir.program)
    {
        IrModule* module = &c23_empty_initializer_ir.program->modules[0];
        u32 zero_global_count = 0;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&c23_empty_initializer_ir.program->symbols, global->symbol);
            if (!symbol || (!string_ends_with_sequence(symbol->name, S8("scalar")) && !string_ends_with_sequence(symbol->name, S8("pointer"))))
            {
                continue;
            }
            if (string_equal(symbol->name, S8("const_scalar")) || string_equal(symbol->name, S8("const_pointer")))
            {
                BUSTER_TEST(arguments, global->is_read_only);
            }
            else
            {
                BUSTER_TEST(arguments, !global->is_read_only);
            }
            BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
            zero_global_count += 1;
        }
        BUSTER_TEST(arguments, zero_global_count == 6);
        BUSTER_TEST(arguments, ir_validate_canonical_module(c23_empty_initializer_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(c23_empty_initializer_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_initializer_separators(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena initializer_separator_temporary = scratch_begin(0, 0);
    CPreprocessResult initializer_separator_tokens = c_preprocess(
        initializer_separator_temporary.arena,
        S8("static int separator_leading[2] = {, 1};"
           " static int separator_doubled[3] = {1, , 2};"
           " static int separator_inferred[] = {1, , 2};"
           " static int separator_scalar = {,};"
           " static int *separator_pointer = {,};"
           " static int separator_trailing[2] = {1,};"
           " static int separator_inferred_trailing[] = {1,};"
           " static char separator_string_trailing[] = {\"ok\",};"
           " static int separator_scalar_trailing = {1,};"
           " static int *separator_pointer_trailing = {(int *)0,};\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
            .dialect = C_PREPROCESS_DIALECT_C23,
        });
    CParseResult initializer_separator_parse = c_parse(initializer_separator_temporary.arena, initializer_separator_tokens);
    CIRLowerResult initializer_separator_ir = c_lower_to_ir(initializer_separator_temporary.arena, S8("initializer-separators.c"),
                                                             initializer_separator_tokens, initializer_separator_parse, target_native);
    u32 invalid_separator_diagnostics = 0;
    bool consistent_separator_diagnostics = true;
    for (u32 diagnostic_index = 0; diagnostic_index < initializer_separator_ir.diagnostic_count; diagnostic_index += 1)
    {
        String8 message = initializer_separator_ir.diagnostics[diagnostic_index].message;
        if (string_first_sequence(message, S8("invalid initializer separator")) != BUSTER_STRING_NO_MATCH)
        {
            invalid_separator_diagnostics += 1;
            consistent_separator_diagnostics &= string_equal(message, S8("C IR lowering: invalid initializer separator"));
        }
    }
    BUSTER_TEST(arguments, initializer_separator_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, initializer_separator_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_separator_diagnostics == 5);
    BUSTER_TEST(arguments, consistent_separator_diagnostics);
    BUSTER_TEST(arguments, initializer_separator_ir.diagnostic_count == 5);
    scratch_end(initializer_separator_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_ambiguous_promoted_ir(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena ambiguous_promoted_temporary = scratch_begin(0, 0);
    CPreprocessResult ambiguous_promoted_tokens = c_preprocess(
        ambiguous_promoted_temporary.arena,
        S8("struct AmbiguousPromoted { struct { int x; }; struct { int x; }; };"
           " static struct AmbiguousPromoted ambiguous_promoted = { .x = 1 };\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult ambiguous_promoted_parse = c_parse(ambiguous_promoted_temporary.arena, ambiguous_promoted_tokens);
    CIRLowerResult ambiguous_promoted_ir = c_lower_to_ir(ambiguous_promoted_temporary.arena, S8("ambiguous-promoted.c"), ambiguous_promoted_tokens,
                                                          ambiguous_promoted_parse, target_native);
    bool found_ambiguity = false;
    for (u32 diagnostic_index = 0; diagnostic_index < ambiguous_promoted_ir.diagnostic_count; diagnostic_index += 1)
    {
        found_ambiguity |= string_starts_with_sequence(ambiguous_promoted_ir.diagnostics[diagnostic_index].message,
                                                       S8("C IR lowering: ambiguous promoted member designator"));
    }
    BUSTER_TEST(arguments, ambiguous_promoted_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, ambiguous_promoted_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ambiguous_promoted_ir.diagnostic_count == 1);
    BUSTER_TEST(arguments, found_ambiguity);
    scratch_end(ambiguous_promoted_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_ambiguous_promoted_parse(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena ambiguous_promoted_parse_temporary = scratch_begin(0, 0);
    CPreprocessResult ambiguous_array_tokens = c_preprocess(
        ambiguous_promoted_parse_temporary.arena,
        S8("struct AmbiguousPromotedArray { struct { int x; }; struct { int x; }; };"
           " static struct AmbiguousPromotedArray ambiguous_promoted_array[] = { { .x = 1 } };\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult ambiguous_array_parse = c_parse(ambiguous_promoted_parse_temporary.arena, ambiguous_array_tokens);
    BUSTER_TEST(arguments, ambiguous_array_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, ambiguous_array_parse.diagnostic_count == 1);
    if (ambiguous_array_parse.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, ambiguous_array_parse.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
        BUSTER_TEST(arguments, string_equal(ambiguous_array_parse.diagnostics[0].message, S8("ambiguous promoted member designator")));
    }
    scratch_end(ambiguous_promoted_parse_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_invalid_union_initializer(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena union_invalid_temporary = scratch_begin(0, 0);
    CPreprocessResult union_invalid_tokens = c_preprocess(
        union_invalid_temporary.arena,
        S8("union InvalidUnion { int first; int second; }; static union InvalidUnion invalid = { 1, 2 };"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult union_invalid_parse = c_parse(union_invalid_temporary.arena, union_invalid_tokens);
    CIRLowerResult union_invalid_ir = c_lower_to_ir(union_invalid_temporary.arena, S8("invalid-union-initializer.c"), union_invalid_tokens,
                                                    union_invalid_parse, target_native);
    BUSTER_TEST(arguments, union_invalid_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, union_invalid_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, union_invalid_ir.diagnostic_count == 1);
    BUSTER_TEST(arguments, !union_invalid_ir.canonical_ir_certified);
    scratch_end(union_invalid_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_deferred_assert_positive(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena deferred_assert_temporary = scratch_begin(0, 0);
    CPreprocessResult deferred_assert_tokens = c_preprocess(
        deferred_assert_temporary.arena,
        S8("static int inferred_assert_array[] = { [(unsigned char)256] = 1 };"
           " _Static_assert(sizeof(inferred_assert_array) == 4, \"inferred array size\");"
           " int deferred_assert_positive(void) { return inferred_assert_array[0]; }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult deferred_assert_parse = c_parse(deferred_assert_temporary.arena, deferred_assert_tokens);
    CIRLowerResult deferred_assert_ir = c_lower_to_ir(deferred_assert_temporary.arena, S8("deferred-assert-positive.c"), deferred_assert_tokens,
                                                      deferred_assert_parse, target_native);
    BUSTER_TEST(arguments, deferred_assert_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, deferred_assert_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, deferred_assert_parse.deferred_static_assert_count == 1);
    BUSTER_TEST(arguments, deferred_assert_ir.diagnostic_count == 0);
    scratch_end(deferred_assert_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_deferred_assert_false(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena deferred_assert_false_temporary = scratch_begin(0, 0);
    CPreprocessResult deferred_assert_false_tokens = c_preprocess(
        deferred_assert_false_temporary.arena,
        S8("static int false_assert_array[] = { [(unsigned char)256] = 1 };"
           " _Static_assert(sizeof(false_assert_array) == 8, \"false inferred array size\");\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult deferred_assert_false_parse = c_parse(deferred_assert_false_temporary.arena, deferred_assert_false_tokens);
    CIRLowerResult deferred_assert_false_ir = c_lower_to_ir(deferred_assert_false_temporary.arena, S8("deferred-assert-false.c"),
                                                            deferred_assert_false_tokens, deferred_assert_false_parse, target_native);
    BUSTER_TEST(arguments, deferred_assert_false_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, deferred_assert_false_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, deferred_assert_false_parse.deferred_static_assert_count == 1);
    BUSTER_TEST(arguments, deferred_assert_false_ir.diagnostic_count == 1);
    if (deferred_assert_false_ir.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, deferred_assert_false_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
    }
    scratch_end(deferred_assert_false_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_deferred_assert_nonconstant(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena deferred_assert_nonconstant_temporary = scratch_begin(0, 0);
    CPreprocessResult deferred_assert_nonconstant_tokens = c_preprocess(
        deferred_assert_nonconstant_temporary.arena,
        S8("static int nonconstant_assert_array[] = { [(unsigned char)256] = 1 };"
           " int nonconstant_assert_value(void);"
           " _Static_assert(sizeof(nonconstant_assert_array) == nonconstant_assert_value(), \"runtime value\");\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult deferred_assert_nonconstant_parse = c_parse(deferred_assert_nonconstant_temporary.arena, deferred_assert_nonconstant_tokens);
    CIRLowerResult deferred_assert_nonconstant_ir = c_lower_to_ir(deferred_assert_nonconstant_temporary.arena, S8("deferred-assert-nonconstant.c"),
                                                                    deferred_assert_nonconstant_tokens, deferred_assert_nonconstant_parse,
                                                                    target_native);
    BUSTER_TEST(arguments, deferred_assert_nonconstant_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, deferred_assert_nonconstant_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, deferred_assert_nonconstant_parse.deferred_static_assert_count == 1);
    BUSTER_TEST(arguments, deferred_assert_nonconstant_ir.diagnostic_count == 1);
    if (deferred_assert_nonconstant_ir.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, deferred_assert_nonconstant_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT);
    }
    scratch_end(deferred_assert_nonconstant_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_local_tls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena tls_temporary = scratch_begin(0, 0);
    CPreprocessResult tls_tokens = c_preprocess(
        tls_temporary.arena,
        S8("int tls_probe(void) {"
           " static _Thread_local int c_value = 1;"
           " static __thread int gnu_value = 2;"
           " c_value += 1; gnu_value += 1; return c_value + gnu_value; }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult tls_parse = c_parse(tls_temporary.arena, tls_tokens);
    CIRLowerResult tls_ir = c_lower_to_ir(tls_temporary.arena, S8("local-tls.c"), tls_tokens, tls_parse, target_native);
    BUSTER_TEST(arguments, tls_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, tls_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, tls_ir.diagnostic_count == 0);
    u32 tls_entities = 0;
    for (u32 entity_index = 0; entity_index < tls_parse.entity_count; entity_index += 1)
    {
        tls_entities += tls_parse.entities[entity_index].kind == C_ENTITY_LOCAL && tls_parse.entities[entity_index].is_thread_local;
    }
    BUSTER_TEST(arguments, tls_entities == 2);
    if (tls_ir.program)
    {
        u32 tls_globals = 0;
        IrModule* module = &tls_ir.program->modules[0];
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&tls_ir.program->symbols, global->symbol);
            if (symbol && string_starts_with_sequence(symbol->link_name, S8(".L.tls_probe.")))
            {
                tls_globals += 1;
                BUSTER_TEST(arguments, global->is_thread_local && symbol->is_thread_local);
            }
        }
        BUSTER_TEST(arguments, tls_globals == 2);
    }
    scratch_end(tls_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_invalid_local_static_initializer(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena invalid_static_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_static_tokens = c_preprocess(
        invalid_static_temporary.arena,
        S8("struct StaticPair { int first; int second; };"
           " extern int static_local_runtime(void);"
           " static int invalid_static_local(void) {"
           " static const struct StaticPair value = { static_local_runtime(), 2 };"
           " return value.first; }"
           " int main(void) { return invalid_static_local(); }\n"),
        (CPreprocessOptions){0});
    CParseResult invalid_static_parse = c_parse(invalid_static_temporary.arena, invalid_static_tokens);
    CIRLowerResult invalid_static_ir =
        c_lower_to_ir(invalid_static_temporary.arena, S8("invalid-local-static-initializer.c"), invalid_static_tokens, invalid_static_parse, target_native);
    BUSTER_TEST(arguments, invalid_static_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_static_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_static_ir.diagnostic_count == 1);
    if (invalid_static_ir.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, invalid_static_ir.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
        BUSTER_STRING_TEST(arguments, invalid_static_ir.diagnostics[0].message,
                           S8("in function 'invalid_static_local': could not lower static initializer for local 'value'"));
    }
    scratch_end(invalid_static_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_invalid_designators(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena invalid_designator_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_designator_tokens = c_preprocess(
        invalid_designator_temporary.arena,
        S8("static int invalid_designator_value;"
           " static int invalid_designator_nonconstant[] = { [invalid_designator_value] = 1 };"
           " static int invalid_designator_negative[] = { [-1] = 1 };"
           " static int invalid_designator_overflow[] = { [18446744073709551615ULL] = 1 };"
           " static int invalid_designator_empty_range[8] = { [5 ... 2] = 1 };"
           " static int invalid_designator_multiple_range[8] = { [1 ... 2 ... 3] = 1 };"
           " static int invalid_designator_negative_range[8] = { [-1 ... 2] = 1 };"
           " static int invalid_designator_outside_range[4] = { [2 ... 5] = 1 };"
           " static int invalid_designator_fixed_nonconstant[1] = { [invalid_designator_value] = 1 };"
           " static int invalid_designator_fixed_negative[1] = { [-1] = 1 };"
           " static int invalid_designator_fixed_overflow[1] = { [2] = 1 };\n"),
        (CPreprocessOptions){0});
    CParseResult invalid_designator_parse = c_parse(invalid_designator_temporary.arena, invalid_designator_tokens);
    CIRLowerResult invalid_designator_ir = c_lower_to_ir(invalid_designator_temporary.arena, S8("invalid-designators.c"), invalid_designator_tokens,
                                                         invalid_designator_parse, target_native);
    bool found_nonconstant = false;
    bool found_negative = false;
    bool found_overflow = false;
    bool found_empty_range = false;
    bool found_multiple_range = false;
    bool found_negative_range = false;
    bool found_outside_range = false;
    bool found_outside_bounds = false;
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_designator_ir.diagnostic_count; diagnostic_index += 1)
    {
        String8 message = invalid_designator_ir.diagnostics[diagnostic_index].message;
        found_nonconstant |= string_starts_with_sequence(message, S8("C IR lowering: array designator index is not an integer constant expression"));
        found_negative |= string_starts_with_sequence(message, S8("C IR lowering: array designator index is negative"));
        found_overflow |= string_starts_with_sequence(message, S8("C IR lowering: array designator index exceeds the target object size"));
        found_empty_range |= string_starts_with_sequence(message, S8("C IR lowering: array designator range is empty"));
        found_multiple_range |= string_starts_with_sequence(message, S8("C IR lowering: array designator range has multiple ellipses"));
        found_negative_range |= string_starts_with_sequence(message, S8("C IR lowering: array designator index is negative"));
        found_outside_range |= string_starts_with_sequence(message, S8("C IR lowering: array designator index is outside the array bounds"));
        found_outside_bounds |= string_starts_with_sequence(message, S8("C IR lowering: array designator index is outside the array bounds"));
    }
    BUSTER_TEST(arguments, invalid_designator_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_designator_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_designator_ir.diagnostic_count == 10);
    BUSTER_TEST(arguments, found_nonconstant);
    BUSTER_TEST(arguments, found_negative);
    BUSTER_TEST(arguments, found_overflow);
    BUSTER_TEST(arguments, found_empty_range);
    BUSTER_TEST(arguments, found_multiple_range);
    BUSTER_TEST(arguments, found_negative_range);
    BUSTER_TEST(arguments, found_outside_range);
    BUSTER_TEST(arguments, found_outside_bounds);
    scratch_end(invalid_designator_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_invalid_root_designators(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena invalid_root_designator_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_root_designator_tokens = c_preprocess(
        invalid_root_designator_temporary.arena,
        S8("struct InvalidRootDesignator { int x; };"
           " static struct InvalidRootDesignator fixed[1] = { .x = 1 };"
           " static struct InvalidRootDesignator inferred[] = { .x = 2 };"
           " static struct InvalidRootDesignator nested[1] = { { .x = 3 } };"
           " static struct InvalidRootDesignator indexed[1] = { [0].x = 4 };\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult invalid_root_designator_parse = c_parse(invalid_root_designator_temporary.arena, invalid_root_designator_tokens);
    CIRLowerResult invalid_root_designator_ir = c_lower_to_ir(invalid_root_designator_temporary.arena, S8("invalid-root-designators.c"),
                                                               invalid_root_designator_tokens, invalid_root_designator_parse, target_native);
    bool found_root_member_diagnostic = false;
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_root_designator_ir.diagnostic_count; diagnostic_index += 1)
    {
        found_root_member_diagnostic |=
            string_starts_with_sequence(invalid_root_designator_ir.diagnostics[diagnostic_index].message,
                                         S8("C IR lowering: array initializer requires an element designator before a member designator"));
    }
    BUSTER_TEST(arguments, invalid_root_designator_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_root_designator_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_root_designator_ir.diagnostic_count == 2);
    BUSTER_TEST(arguments, found_root_member_diagnostic);
    scratch_end(invalid_root_designator_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_invalid_block_tls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena invalid_block_tls_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_block_tls_tokens = c_preprocess(
        invalid_block_tls_temporary.arena,
        S8("int invalid_block_tls(void) { _Thread_local int value = 1; }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult invalid_block_tls_parse = c_parse(invalid_block_tls_temporary.arena, invalid_block_tls_tokens);
    BUSTER_TEST(arguments, invalid_block_tls_tokens.diagnostic_count == 0);
    bool found_invalid_block_tls_diagnostic = false;
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_block_tls_parse.diagnostic_count; diagnostic_index += 1)
    {
        CDiagnostic diagnostic = invalid_block_tls_parse.diagnostics[diagnostic_index];
        found_invalid_block_tls_diagnostic |= diagnostic.kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS &&
                                               string_equal(diagnostic.message, S8("block-scope thread-local declarations require static or extern"));
    }
    BUSTER_TEST(arguments, invalid_block_tls_parse.diagnostic_count >= 1);
    BUSTER_TEST(arguments, found_invalid_block_tls_diagnostic);
    scratch_end(invalid_block_tls_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_repeated_incomplete_arrays(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena repeated_array_temporary = scratch_begin(0, 0);
    CPreprocessResult repeated_array_tokens = c_preprocess(
        repeated_array_temporary.arena,
        S8("struct RepeatedArray { int values[2]; };"
           " static int repeated_values[] = { [3] = 9 };"
           " static struct RepeatedArray repeated_structs[] = { [2].values[1] = 7 };"
           " static char repeated_string[] = { \"xy\" };"
           " static int repeated_matrix[][2] = { 1, 2, 3, 4 };"
           " int main(void) { return repeated_values[3] == 9 && repeated_structs[2].values[1] == 7 &&"
           " repeated_string[1] == 'y' && repeated_matrix[1][1] == 4 ? 0 : 1; }\n"),
        (CPreprocessOptions){
            .target = target_native,
            .data_layout = target_data_layout(target_native),
        });
    CParseResult repeated_array_parse = c_parse(repeated_array_temporary.arena, repeated_array_tokens);
    CIRLowerResult repeated_array_ir = c_lower_to_ir(repeated_array_temporary.arena, S8("repeated-incomplete-arrays.c"), repeated_array_tokens,
                                                     repeated_array_parse, target_native);
    BUSTER_TEST(arguments, repeated_array_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, repeated_array_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, repeated_array_ir.diagnostic_count == 0);
    if (repeated_array_ir.program)
    {
        IrModule* module = &repeated_array_ir.program->modules[0];
        IrGlobal* globals[] = {0, 0, 0, 0};
        String8 names[] = {
            S8("repeated_values"),
            S8("repeated_structs"),
            S8("repeated_string"),
            S8("repeated_matrix"),
        };
        u64 expected_counts[] = {4, 3, 3, 2};
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&repeated_array_ir.program->symbols, global->symbol);
            if (!symbol)
            {
                continue;
            }
            for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(names); name_index += 1)
            {
                if (string_equal(symbol->name, names[name_index]))
                {
                    globals[name_index] = global;
                }
            }
        }
        for (u32 global_index = 0; global_index < BUSTER_ARRAY_LENGTH(globals); global_index += 1)
        {
            BUSTER_TEST(arguments, globals[global_index] != 0);
            if (globals[global_index])
            {
                IrType* type = ir_type_from_id(&repeated_array_ir.program->types, globals[global_index]->type);
                BUSTER_TEST(arguments, type && type->kind == IR_TYPE_ARRAY && type->element_count == expected_counts[global_index]);
            }
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(repeated_array_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(repeated_array_temporary);
    return result;
}

// The dispatched lexer (the compaction emitter on hosts that have one) must
// produce a byte-identical result to the scalar reference loop for every
// input. Token rows carry no pointers, so one memcmp covers the stream; the
// measurement struct and the diagnostic kinds and locations are compared
// field by field, because those are what the window pipeline reconstructs
// from masks rather than from the branches it replaced.
BUSTER_GLOBAL_LOCAL bool c_test_lex_paths_agree(Arena* arena, String8 source)
{
    u64 position = arena->position;
    CLexResult dispatched = c_lex(arena, source);
    CLexResult reference = c_lex_reference(arena, source);
    bool result = dispatched.token_count == reference.token_count && dispatched.diagnostic_count == reference.diagnostic_count &&
                  memcmp(&dispatched.metrics, &reference.metrics, sizeof(dispatched.metrics)) == 0 &&
                  dispatched.translated_source.length == reference.translated_source.length;
    if (result && dispatched.token_count)
    {
        result = memcmp(dispatched.tokens, reference.tokens, dispatched.token_count * sizeof(CToken)) == 0;
        result = result && dispatched.token_shapes && reference.token_shapes &&
                 memcmp(dispatched.token_shapes, reference.token_shapes, dispatched.token_count * sizeof(CTokenShape)) == 0;
    }
    for (u64 index = 0; result && index < dispatched.diagnostic_count; index += 1)
    {
        CDiagnostic left = dispatched.diagnostics[index];
        CDiagnostic right = reference.diagnostics[index];
        result = left.kind == right.kind && left.severity == right.severity && string_equal(left.message, right.message) &&
                 left.location.offset == right.location.offset && left.location.line == right.location.line && left.location.column == right.location.column;
    }
    arena->position = position;
    return result;
}

// Every construct the compaction emitter models with masks and every shape it
// escapes on, each slid across the 64-byte window boundary by a growing space
// prefix so no construct is only ever seen window-aligned.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_lex_differential(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;

    String8 differential_cases[] = {
        S8("int main(void) { return 0; }"),
        S8("a b c d e f g h i j k l m n o p q r s t u v w x y z"),
        S8("\"simple\""),
        S8("\"unterminated"),
        S8("\"unterminated\nafter\""),
        S8("\"esc\\\"aped\" x"),
        S8("\"bs at end\\"),
        S8("\"\\\\\" \"\\\\\\\"\" y"),
        S8("\"\""),
        S8("\"\"\"\""),
        S8("\"a\" \"b\" \"c\" // \"not a string\"\n\"d\""),
        S8("u8\"x\" u\"x\" U\"x\" L\"x\" u8'a' u'a' U'a' L'a'"),
        S8("au8\"x\" xu'a' Lx\"y\" u8x'z' _L'q'"),
        S8("'a' '' '\\'' '\\\\' 'ab' '\\n'"),
        S8("'unterminated; x"),
        S8("'\"' \"'\""),
        S8("s['a'] = '\\''"),
        S8("1'000 0x1'f 1'000'000u 12'34.5'6"),
        S8("1e5L'a' 0x1p'q' 9'"),
        S8("// comment with ' and \" inside\ncode"),
        S8("///\n////=\n//=\n"),
        S8("/* block */ x /*two*/ y"),
        S8("/**/ /***/ /* * / */ /*/ still open */ z"),
        S8("a/* first\nsecond */b\n"),
        S8("/* unterminated"),
        S8("/*\n\n\n*/"),
        S8("x /* mixed */ // trailing\ny"),
        S8("123 0x1f 017 0b101 1'000 0x 0b12 1..2 9.5.3"),
        S8("1.5e+3 1.5e3 1.e+5 1.e5 0x1.8p-3 0x1p3 1e+5 5.x 1.foo"),
        S8("0xZ 123abc 0b1cd 0x1fgh 0 00x1 9.5.3.7 1.5p3 0x1.8e+3 1___ 0b_1"),
        S8(".5 .5e-2 a.5 a.b x..5 ...5 1.e+ 3e+ 3e"),
        S8("<<= >>= ... -> ++ -- << >> <= >= == != && || *= /= %= += -= &= ^= |= ##"),
        S8("<: :> <% %> %: %:%: %:%:%: %::% <:> <%>"),
        S8("=== <<< >>>> .... ..= =>> <<== >>== ..."),
        S8("[](){}.&*+-~!/%<>^|?:;=,#@\\"),
        S8("a+=b-=c*=d/=e%=f&=g|=h^=i<<=j>>=k"),
        S8("/=/ //= x"),
        S8("p->q p ->q p-> q p - >q"),
        S8("a---b a+++b a<<<b a>>>b"),
        S8("`~ \x01\x02 \x7f\x1f"),
        S8("\x00 embedded"),
        S8("caf\xC3\xA9 x \xE2\x82\xAC \xF0\x9F\x98\x80 \x80 \xFF"),
        S8("\"utf8 caf\xC3\xA9 in string\" x"),
        S8("'\xC3\xA9'"),
        S8("$dollar a$b $ $$"),
        S8("a\\\nb"),
        S8("line\r\nnext\rlast\n"),
        S8("\r\n \n\r \r \n"),
        S8("  \t\v\f  \t\t\v\v\f\f  "),
        S8("\n\n\n\n"),
        S8("#include <stdio.h>\n#define M(x) ((x) + 1)\n#if defined(A) && !defined(B)\n#endif\n"),
        S8("struct S { int a : 3; unsigned b : 29; } s = { .a = -1, .b = 2u };"),
    };
    for (u64 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(differential_cases); case_index += 1)
    {
        String8 differential_case = differential_cases[case_index];
        for (u64 pad = 0; pad < 67; pad += 1)
        {
            u64 padded_length = pad + differential_case.length;
            char8* padded = arena_allocate(arena, char8, padded_length);
            memset(padded, ' ', pad);
            memcpy(padded + pad, differential_case.pointer, differential_case.length);
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){padded, padded_length}));
            arena->position = position;
        }
    }

    // Single items that cross or fill whole windows, which is the shape that
    // sends the emitter to the scalar whole-token fallback.
    {
        u64 run_lengths[] = {1, 61, 62, 63, 64, 65, 66, 127, 128, 200};
        for (u64 length_index = 0; length_index < BUSTER_ARRAY_LENGTH(run_lengths); length_index += 1)
        {
            u64 run_length = run_lengths[length_index];
            u64 buffer_length = run_length + 8;
            char8* buffer = arena_allocate(arena, char8, buffer_length);

            memset(buffer, 'a', run_length);
            buffer[run_length] = ';';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 1}));

            memset(buffer, ' ', run_length);
            buffer[run_length] = 'x';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 1}));

            memset(buffer, '\n', run_length);
            buffer[run_length] = 'x';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 1}));

            buffer[0] = '/';
            buffer[1] = '/';
            memset(buffer + 2, 'c', run_length);
            buffer[run_length + 2] = '\n';
            buffer[run_length + 3] = 'x';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 4}));

            buffer[0] = '/';
            buffer[1] = '*';
            memset(buffer + 2, 'c', run_length);
            buffer[run_length + 2] = '*';
            buffer[run_length + 3] = '/';
            buffer[run_length + 4] = 'x';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 5}));

            buffer[0] = '/';
            buffer[1] = '*';
            memset(buffer + 2, 'c', run_length);
            buffer[2 + run_length / 2] = '\n';
            buffer[run_length + 2] = '*';
            buffer[run_length + 3] = '/';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 4}));

            buffer[0] = '"';
            memset(buffer + 1, 'b', run_length);
            buffer[run_length + 1] = '"';
            buffer[run_length + 2] = ' ';
            buffer[run_length + 3] = 'y';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 4}));

            buffer[0] = '"';
            memset(buffer + 1, '\\', run_length);
            buffer[run_length + 1] = '"';
            buffer[run_length + 2] = 'y';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 3}));

            buffer[0] = '1';
            memset(buffer + 1, '0', run_length);
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 1}));

            memset(buffer, '.', run_length);
            buffer[run_length] = '5';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 1}));

            memset(buffer, '<', run_length);
            buffer[run_length] = '=';
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){buffer, run_length + 1}));

            arena->position = position;
        }
    }

    // The real corpus: every C fixture the driver tests compile, and on the
    // desktop platforms the frontend's own sources too — c.c is the largest
    // and most varied C file in the tree, so it is worth lexing whole.
    //
    // The Android package and the iOS bundle ship `tests/` and not `src/`, so
    // the source half is desktop-only. Both halves still assert the file
    // opened: a path that stops resolving must fail the gate rather than
    // silently shrink the corpus.
    {
        String8 corpus[] = {
#if !BUSTER_ANDROID && !BUSTER_IOS
            S8("src/buster/lib/compiler/frontend/c/c.c"),
            S8("src/buster/lib/compiler/frontend/c/c.h"),
            S8("src/buster/lib/base.h"),
            S8("src/buster/apps/ide/ide.c"),
#endif
            S8("tests/basic_c_compile.c"),
            S8("tests/basic_c_operations.c"),
            S8("tests/basic_c_stdatomic.c"),
            S8("tests/basic_c_generic.c"),
            S8("tests/basic_c_vector.c"),
            S8("tests/basic_c_asm.c"),
            S8("tests/basic_c_preprocessor_error.c"),
            S8("tests/basic_c_include.h"),
        };
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(corpus); index += 1)
        {
            FileMapRead source_file = file_map_read(arena, corpus[index], (FileReadOptions){0});
            String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);
            BUSTER_TEST(arguments, source.pointer != 0);
            if (source.pointer)
            {
                BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, source));
                // Then the head of the file at every offset modulo the window
                // width, so no construct is only ever seen window-aligned.
                for (u64 skip = 1; skip < 67 && skip < source.length; skip += 1)
                {
                    u64 slice = BUSTER_MIN(source.length - skip, BUSTER_KB(32));
                    BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){source.pointer + skip, slice}));
                }
            }
            file_map_unmap(source_file);
            arena->position = position;
        }
    }

    // Deterministic fuzz over the lexer's whole alphabet, which reaches the
    // adjacency cases no hand-written fixture thinks of.
    {
        static const char8 fuzz_alphabet[] = " \t\v\f\n\r\"'\\/*;?._$abcxyzeELpu8ABZ0189=<>!+-%&|^~@(){}[]:,#`";
        static const char8 fuzz_high_bytes[] = {(char8)0xC3u, (char8)0xA9u, (char8)0xF0u, (char8)0x80u, (char8)0x00u, (char8)0x7Fu};
        u64 fuzz_alphabet_length = BUSTER_ARRAY_LENGTH(fuzz_alphabet) - 1;
        u64 fuzz_lengths[] = {64, 256, 1024, 4096};
        u64 seed = 0x12345678u;
        for (u64 round = 0; round < 48; round += 1)
        {
            u64 fuzz_length = fuzz_lengths[round % BUSTER_ARRAY_LENGTH(fuzz_lengths)];
            char8* blob = arena_allocate(arena, char8, fuzz_length);
            for (u64 index = 0; index < fuzz_length; index += 1)
            {
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                u64 pick = (seed >> 33) % (fuzz_alphabet_length + BUSTER_ARRAY_LENGTH(fuzz_high_bytes));
                blob[index] = pick < fuzz_alphabet_length ? fuzz_alphabet[pick] : fuzz_high_bytes[pick - fuzz_alphabet_length];
            }
            BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, (String8){blob, fuzz_length}));
            arena->position = position;
        }
    }

    // The punctuator NFA's channels are hand-assigned while every spelling
    // table is derived from c_punctuator_spellings, so the two are reconciled
    // exhaustively inside the frontend: over every byte sequence the emitter
    // can classify, the maximal munch its channels take must be the one
    // c_punctuator_length takes. One assertion, half a million comparisons.
    BUSTER_TEST(arguments, c_test_lex_punctuator_nfa_mismatches() == 0);
    return result;
}

// A quoted literal of exactly `spelling_length` bytes, delimiters included,
// with escaped delimiters and escaped backslashes strewn through the body so
// any escape-aware rescan of the spelling is exercised, and the final escape
// pair flush against the closing delimiter. `decoded_out`, when given,
// receives the byte sequence the literal decodes to. The generator must
// produce text the lexer closes exactly at the last byte, so escape pairs
// stay clear of the tail where an overlap could end the literal early.
BUSTER_GLOBAL_LOCAL String8 c_test_giant_literal(Arena* arena, u64 spelling_length, char8 delimiter, String8* decoded_out)
{
    BUSTER_CHECK(spelling_length >= 8);
    char8* spelling = arena_allocate(arena, char8, spelling_length);
    u64 content_length = spelling_length - 2;
    char8* content = spelling + 1;
    spelling[0] = delimiter;
    spelling[spelling_length - 1] = delimiter;
    for (u64 index = 0; index < content_length; index += 1)
    {
        content[index] = (char8)('A' + index % 26);
    }
    for (u64 index = 0; index + 4 < content_length; index += 997)
    {
        content[index] = '\\';
        content[index + 1] = (char8)((index / 997) % 2 ? '\\' : delimiter);
    }
    content[content_length - 2] = '\\';
    content[content_length - 1] = delimiter;
    if (decoded_out)
    {
        char8* decoded = arena_allocate(arena, char8, content_length);
        u64 decoded_length = 0;
        for (u64 index = 0; index < content_length; index += 1)
        {
            if (content[index] == '\\')
            {
                index += 1;
            }
            decoded[decoded_length++] = content[index];
        }
        *decoded_out = (String8){decoded, decoded_length};
    }
    return (String8){spelling, spelling_length};
}

BUSTER_GLOBAL_LOCAL String8 c_test_concatenate(Arena* arena, String8* parts, u64 part_count)
{
    u64 length = 0;
    for (u64 index = 0; index < part_count; index += 1)
    {
        length += parts[index].length;
    }
    char8* bytes = arena_allocate(arena, char8, length);
    u64 output = 0;
    for (u64 index = 0; index < part_count; index += 1)
    {
        memcpy(bytes + output, parts[index].pointer, parts[index].length);
        output += parts[index].length;
    }
    return (String8){bytes, length};
}

BUSTER_GLOBAL_LOCAL CToken c_test_find_token_kind(CToken* tokens, u64 token_count, CTokenKind kind, bool* found)
{
    for (u64 index = 0; index < token_count; index += 1)
    {
        if (tokens[index].kind == kind)
        {
            *found = true;
            return tokens[index];
        }
    }
    *found = false;
    return (CToken){0};
}

// Tokens whose spellings reach and cross 0xFFFF bytes: the fixtures for the
// CToken u16 length escape. Every length assertion goes through
// c_token_spelling, never the raw field, so the same fixtures hold before
// and after the field narrows. Covered: the boundary spelling lengths just
// below, at, and above the escape through lex, preprocess, parse and IR
// decode; the SIMD/scalar lexer differential over each; a >64 KB character
// literal; an unterminated >64 KB literal (both lexer paths must agree on
// it); and the two synthesized-spelling producers of oversized string
// literals, stringify (#) and token paste (##).
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_oversized_token_spellings(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;

    u64 boundary_lengths[] = {65534, 65535, 65536, 70003};
    for (u64 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(boundary_lengths); case_index += 1)
    {
        u64 spelling_length = boundary_lengths[case_index];
        String8 decoded = {0};
        String8 literal = c_test_giant_literal(arena, spelling_length, '"', &decoded);
        String8 source_parts[] = {S8("static const char big[] = "), literal, S8(";\n")};
        String8 source = c_test_concatenate(arena, source_parts, BUSTER_ARRAY_LENGTH(source_parts));

        CLexResult lex = c_lex(arena, source);
        BUSTER_TEST(arguments, lex.diagnostic_count == 0);
        bool lex_found = false;
        CToken lex_token = c_test_find_token_kind(lex.tokens, lex.token_count, C_TOKEN_STRING_LITERAL, &lex_found);
        BUSTER_TEST(arguments, lex_found);
        if (lex_found)
        {
            BUSTER_STRING_TEST(arguments, c_token_spelling(lex.spelling_base, lex_token), literal);
        }
        BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, source));

        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lower = c_test_lower_source(arena, source, S8("oversized-literal.c"), target_native, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lower.diagnostic_count == 0);
        bool preprocessed_found = false;
        CToken preprocessed_token = c_test_find_token_kind(preprocess.tokens, preprocess.token_count, C_TOKEN_STRING_LITERAL, &preprocessed_found);
        BUSTER_TEST(arguments, preprocessed_found);
        if (preprocessed_found)
        {
            BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocessed_token), literal);
        }
        IrGlobal* big_global = 0;
        if (lower.program && lower.program->modules[0].global_count == 1)
        {
            big_global = lower.program->modules[0].globals;
        }
        BUSTER_TEST(arguments, big_global && big_global->bytes.pointer && big_global->bytes.length == decoded.length + 1);
        if (big_global && big_global->bytes.pointer && big_global->bytes.length == decoded.length + 1)
        {
            BUSTER_TEST(arguments, memcmp(big_global->bytes.pointer, decoded.pointer, decoded.length) == 0);
            BUSTER_TEST(arguments, big_global->bytes.pointer[decoded.length] == 0);
        }
        arena->position = position;
    }

    // A >64 KB character literal only has to lex: its spelling and the
    // escape-aware close must survive, and both lexer paths must agree.
    {
        String8 character_literal = c_test_giant_literal(arena, 70001, '\'', 0);
        CLexResult lex = c_lex(arena, character_literal);
        BUSTER_TEST(arguments, lex.diagnostic_count == 0);
        bool found = false;
        CToken token = c_test_find_token_kind(lex.tokens, lex.token_count, C_TOKEN_CHARACTER_LITERAL, &found);
        BUSTER_TEST(arguments, found);
        if (found)
        {
            BUSTER_STRING_TEST(arguments, c_token_spelling(lex.spelling_base, token), character_literal);
        }
        BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, character_literal));
        arena->position = position;
    }

    // An unterminated >64 KB string literal: the token cannot report its true
    // length once the field narrows, so the only stable contracts are the
    // diagnostic and the two lexer paths agreeing byte for byte.
    {
        u64 body_length = 70000;
        char8* bytes = arena_allocate(arena, char8, body_length + 2);
        bytes[0] = '"';
        for (u64 index = 0; index < body_length; index += 1)
        {
            bytes[1 + index] = (char8)('a' + index % 26);
        }
        bytes[body_length + 1] = '\n';
        String8 source = {bytes, body_length + 2};
        CLexResult lex = c_lex(arena, source);
        BUSTER_TEST(arguments, lex.diagnostic_count == 1);
        BUSTER_TEST(arguments, lex.diagnostic_count == 1 && lex.diagnostics[0].kind == C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL);
        BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, source));
        arena->position = position;
    }

    // Stringify: # over a giant string-literal argument escapes its quotes
    // and backslashes, so the synthesized spelling is oversized even before
    // the argument is. Decoding the stringified literal must give back the
    // argument's spelling exactly.
    {
        String8 inner = c_test_giant_literal(arena, 70003, '"', 0);
        u64 escaped_length = 0;
        for (u64 index = 0; index < inner.length; index += 1)
        {
            escaped_length += 1 + (u64)(inner.pointer[index] == '"' || inner.pointer[index] == '\\');
        }
        char8* expected_bytes = arena_allocate(arena, char8, escaped_length + 2);
        u64 output = 0;
        expected_bytes[output++] = '"';
        for (u64 index = 0; index < inner.length; index += 1)
        {
            if (inner.pointer[index] == '"' || inner.pointer[index] == '\\')
            {
                expected_bytes[output++] = '\\';
            }
            expected_bytes[output++] = inner.pointer[index];
        }
        expected_bytes[output++] = '"';
        String8 expected = {expected_bytes, output};
        String8 source_parts[] = {
            S8("#define STR(x) #x\n"
               "static const char stringified[] = STR("),
            inner,
            S8(");\n"),
        };
        String8 source = c_test_concatenate(arena, source_parts, BUSTER_ARRAY_LENGTH(source_parts));
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lower = c_test_lower_source(arena, source, S8("oversized-stringify.c"), target_native, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lower.diagnostic_count == 0);
        bool found = false;
        CToken token = c_test_find_token_kind(preprocess.tokens, preprocess.token_count, C_TOKEN_STRING_LITERAL, &found);
        BUSTER_TEST(arguments, found);
        if (found)
        {
            BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, token), expected);
        }
        IrGlobal* stringified_global = 0;
        if (lower.program && lower.program->modules[0].global_count == 1)
        {
            stringified_global = lower.program->modules[0].globals;
        }
        BUSTER_TEST(arguments, stringified_global && stringified_global->bytes.pointer && stringified_global->bytes.length == inner.length + 1);
        if (stringified_global && stringified_global->bytes.pointer && stringified_global->bytes.length == inner.length + 1)
        {
            BUSTER_TEST(arguments, memcmp(stringified_global->bytes.pointer, inner.pointer, inner.length) == 0);
        }
        arena->position = position;
    }

    // Token paste: u8 ## "..." runs the join-and-relex path with an
    // oversized result whose relexed length must agree with the join.
    {
        String8 decoded = {0};
        String8 literal = c_test_giant_literal(arena, 70003, '"', &decoded);
        String8 expected_parts[] = {S8("u8"), literal};
        String8 expected = c_test_concatenate(arena, expected_parts, BUSTER_ARRAY_LENGTH(expected_parts));
        String8 source_parts[] = {
            S8("#define GLUE(a, b) a##b\n"
               "static const char glued[] = GLUE(u8, "),
            literal,
            S8(");\n"),
        };
        String8 source = c_test_concatenate(arena, source_parts, BUSTER_ARRAY_LENGTH(source_parts));
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lower = c_test_lower_source(arena, source, S8("oversized-paste.c"), target_native, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lower.diagnostic_count == 0);
        bool found = false;
        CToken token = c_test_find_token_kind(preprocess.tokens, preprocess.token_count, C_TOKEN_STRING_LITERAL, &found);
        BUSTER_TEST(arguments, found);
        if (found)
        {
            BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, token), expected);
        }
        IrGlobal* glued_global = 0;
        if (lower.program && lower.program->modules[0].global_count == 1)
        {
            glued_global = lower.program->modules[0].globals;
        }
        BUSTER_TEST(arguments, glued_global && glued_global->bytes.pointer && glued_global->bytes.length == decoded.length + 1);
        if (glued_global && glued_global->bytes.pointer && glued_global->bytes.length == decoded.length + 1)
        {
            BUSTER_TEST(arguments, memcmp(glued_global->bytes.pointer, decoded.pointer, decoded.length) == 0);
        }
        arena->position = position;
    }

    // Only terminated literals carry the length escape: an identifier or a
    // preprocessing number at the sentinel is a hard implementation limit,
    // diagnosed at lex time, and both lexer paths must agree on that too.
    {
        u64 run_length = 65535;
        char8* identifier_bytes = arena_allocate(arena, char8, run_length);
        char8* number_bytes = arena_allocate(arena, char8, run_length);
        for (u64 index = 0; index < run_length; index += 1)
        {
            identifier_bytes[index] = (char8)('a' + index % 26);
            number_bytes[index] = (char8)('0' + index % 10);
        }
        String8 identifier_source = {identifier_bytes, run_length};
        String8 number_source = {number_bytes, run_length};
        CLexResult identifier_lex = c_lex(arena, identifier_source);
        BUSTER_TEST(arguments, identifier_lex.diagnostic_count == 1);
        BUSTER_TEST(arguments, identifier_lex.diagnostic_count == 1 && identifier_lex.diagnostics[0].kind == C_DIAGNOSTIC_TOKEN_TOO_LONG);
        BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, identifier_source));
        CLexResult number_lex = c_lex(arena, number_source);
        BUSTER_TEST(arguments, number_lex.diagnostic_count == 1);
        BUSTER_TEST(arguments, number_lex.diagnostic_count == 1 && number_lex.diagnostics[0].kind == C_DIAGNOSTIC_TOKEN_TOO_LONG);
        BUSTER_TEST(arguments, c_test_lex_paths_agree(arena, number_source));
        arena->position = position;
    }

    // Pasting two identifiers whose join crosses the sentinel relexes with
    // the too-long diagnostic, so the paste fails as invalid instead of
    // storing a sentinel only literals may carry.
    {
        u64 half_length = 40000;
        char8* half = arena_allocate(arena, char8, half_length);
        for (u64 index = 0; index < half_length; index += 1)
        {
            half[index] = (char8)('a' + index % 26);
        }
        String8 half_identifier = {half, half_length};
        String8 source_parts[] = {
            S8("#define GLUE(a, b) a##b\n"
               "int GLUE("),
            half_identifier,
            S8(", "),
            half_identifier,
            S8(");\n"),
        };
        String8 source = c_test_concatenate(arena, source_parts, BUSTER_ARRAY_LENGTH(source_parts));
        CPreprocessResult preprocess = c_preprocess(arena, source, (CPreprocessOptions){0});
        BUSTER_TEST(arguments, preprocess.error_count == 1);
        BUSTER_TEST(arguments, preprocess.diagnostic_count >= 1 && preprocess.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_TOKEN_PASTE);
        arena->position = position;
    }

    return result;
}

// Source measurement, checked against counts done by hand on the fixtures
// below. The two partitions are asserted on every fixture: they are what makes
// the numbers add up rather than merely look plausible.
BUSTER_GLOBAL_LOCAL void c_test_source_metrics_partitions(UnitTestArguments* arguments, UnitTestResult* outer_result, CSourceMetrics metrics)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, c_source_metrics_code_bytes(metrics) + metrics.comment_bytes + metrics.blank_bytes == metrics.translated_bytes);
    BUSTER_TEST(arguments, metrics.code_lines + (metrics.comment_lines - metrics.mixed_lines) + metrics.blank_lines == metrics.translated_lines);
    BUSTER_TEST(arguments, metrics.lines - metrics.spliced_lines == metrics.translated_lines);
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_source_metrics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // 49 bytes over five lines: code with a trailing comment, a blank line, a
    // two-line block comment, and a line whose literal is code.
    CLexResult mixed = c_lex(arguments->arena, S8("int a; // one\n"
                                                  "\n"
                                                  "/* two\n"
                                                  "   lines */\n"
                                                  "char* s = \"x\";\n"));
    BUSTER_TEST(arguments, mixed.diagnostic_count == 0);
    BUSTER_TEST(arguments, mixed.metrics.files == 1);
    BUSTER_TEST(arguments, mixed.metrics.bytes == 49);
    BUSTER_TEST(arguments, mixed.metrics.translated_bytes == 49);
    BUSTER_TEST(arguments, mixed.metrics.lines == 5);
    BUSTER_TEST(arguments, mixed.metrics.translated_lines == 5);
    BUSTER_TEST(arguments, mixed.metrics.spliced_lines == 0);
    BUSTER_TEST(arguments, mixed.metrics.code_lines == 2);
    BUSTER_TEST(arguments, mixed.metrics.comment_lines == 3);
    BUSTER_TEST(arguments, mixed.metrics.mixed_lines == 1);
    BUSTER_TEST(arguments, mixed.metrics.blank_lines == 1);
    BUSTER_TEST(arguments, mixed.metrics.comment_bytes == 24);
    BUSTER_TEST(arguments, mixed.metrics.blank_bytes == 9);
    BUSTER_TEST(arguments, mixed.metrics.literal_bytes == 3);
    BUSTER_TEST(arguments, mixed.metrics.comments == 2);
    BUSTER_TEST(arguments, mixed.metrics.tokens == 9);
    BUSTER_TEST(arguments, c_source_metrics_code_bytes(mixed.metrics) == 16);
    c_test_source_metrics_partitions(arguments, &result, mixed.metrics);

    // Two physical lines spliced into one logical line, with CRLF endings: the
    // file is seven bytes, the lexer scans three.
    CLexResult spliced = c_lex(arguments->arena, S8("a\\\r\nb\r\n"));
    BUSTER_TEST(arguments, spliced.diagnostic_count == 0);
    BUSTER_TEST(arguments, spliced.metrics.bytes == 7);
    BUSTER_TEST(arguments, spliced.metrics.translated_bytes == 3);
    BUSTER_TEST(arguments, spliced.metrics.lines == 2);
    BUSTER_TEST(arguments, spliced.metrics.translated_lines == 1);
    BUSTER_TEST(arguments, spliced.metrics.spliced_lines == 1);
    BUSTER_TEST(arguments, spliced.metrics.code_lines == 1);
    BUSTER_TEST(arguments, spliced.metrics.tokens == 1);
    BUSTER_TEST(arguments, c_source_metrics_code_bytes(spliced.metrics) == 2);
    c_test_source_metrics_partitions(arguments, &result, spliced.metrics);

    // An unterminated last line still ends a line.
    CLexResult unterminated = c_lex(arguments->arena, S8("x"));
    BUSTER_TEST(arguments, unterminated.metrics.bytes == 1);
    BUSTER_TEST(arguments, unterminated.metrics.lines == 1);
    BUSTER_TEST(arguments, unterminated.metrics.code_lines == 1);
    BUSTER_TEST(arguments, unterminated.metrics.blank_bytes == 0);
    BUSTER_TEST(arguments, unterminated.metrics.tokens == 1);
    c_test_source_metrics_partitions(arguments, &result, unterminated.metrics);

    CLexResult empty = c_lex(arguments->arena, S8(""));
    BUSTER_TEST(arguments, empty.metrics.files == 1);
    BUSTER_TEST(arguments, empty.metrics.lines == 0);
    BUSTER_TEST(arguments, empty.metrics.translated_lines == 0);
    BUSTER_TEST(arguments, empty.metrics.tokens == 0);
    BUSTER_TEST(arguments, empty.token_count == 1);
    BUSTER_TEST(arguments, empty.token_shapes && empty.token_shapes[0] == C_TOKEN_END_OF_FILE);
    c_test_source_metrics_partitions(arguments, &result, empty.metrics);

    CLexResult malformed = c_lex(arguments->arena, S8("/* unterminated"));
    BUSTER_TEST(arguments, malformed.token_count != 0 && malformed.token_shapes != 0);
    for (u32 token_index = 0; token_index < malformed.token_count; token_index += 1)
    {
        BUSTER_TEST(arguments, malformed.token_shapes[token_index] == c_token_shape_from_token(malformed.tokens[token_index]));
    }

    // Without includes the two aggregates are the same one file, and the
    // preprocessor's totals are the root lex's.
    CPreprocessResult preprocess = c_preprocess(arguments->arena,
                                                S8("#define ONE 1\n"
                                                   "int v = ONE; // used\n"),
                                                (CPreprocessOptions){0});
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_lexed.files == 1);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_unique.files == 1);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_lexed.bytes == 35);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_unique.bytes == c_preprocess_detail(preprocess)->source_lexed.bytes);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_lexed.lines == 2);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_lexed.code_lines == 2);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_lexed.comments == 1);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->source_lexed.mixed_lines == 1);
    c_test_source_metrics_partitions(arguments, &result, c_preprocess_detail(preprocess)->source_lexed);
    c_test_source_metrics_partitions(arguments, &result, c_preprocess_detail(preprocess)->source_unique);

    // The other side of the frontend: the directive and the comment are gone,
    // ONE expanded once, and `int v = 1 ;` is what the parser receives. The
    // end marker is in token_count but not in the measured token total.
    BUSTER_TEST(arguments, preprocess.token_count == 6);
    BUSTER_TEST(arguments, c_preprocess_token_shapes(&preprocess) != 0);
    for (u32 token_index = 0; token_index < preprocess.token_count; token_index += 1)
    {
        BUSTER_TEST(arguments, c_preprocess_token_shape(&preprocess, token_index) == c_token_shape_from_token(preprocess.tokens[token_index]));
    }
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->preprocessed.tokens == 5);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->preprocessed.bytes == 7);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->preprocessed.definitions == 1);
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->preprocessed.expansions == 1);
    // Every translated file lands in the spelling space, plus the prelude and
    // anything preprocessing synthesized.
    BUSTER_TEST(arguments, c_preprocess_detail(preprocess)->preprocessed.spelling_bytes >= c_preprocess_detail(preprocess)->source_lexed.translated_bytes);

    // An unreferenced macro is defined but never expanded, and a function-like
    // macro used twice expands twice.
    CPreprocessResult expansions = c_preprocess(arguments->arena,
                                                S8("#define UNUSED 9\n"
                                                   "#define TWICE(x) x + x\n"
                                                   "int a = TWICE(1);\n"
                                                   "int b = TWICE(2);\n"),
                                                (CPreprocessOptions){0});
    BUSTER_TEST(arguments, expansions.diagnostic_count == 0);
    BUSTER_TEST(arguments, c_preprocess_detail(expansions)->preprocessed.definitions == 2);
    BUSTER_TEST(arguments, c_preprocess_detail(expansions)->preprocessed.expansions == 2);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_lex_preprocess(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);

    {
        u64 arena_position = arguments->arena->position;
        u64 boundary_capacity = 192;
        char8* boundary = arena_allocate(arguments->arena, char8, boundary_capacity);
        memset(boundary, 'a', boundary_capacity);
        u64 plain_lengths[] = {0, 1, 7, 8, 9, 63, 64, 65, 127, 128, 129};
        for (u64 length_index = 0; length_index < BUSTER_ARRAY_LENGTH(plain_lengths); length_index += 1)
        {
            u64 length = plain_lengths[length_index];
            BUSTER_TEST(arguments, c_test_translate_source_paths_agree(arguments->arena, (String8){.pointer = boundary, .length = length}));
        }

        char8 candidates[] = {'\r', '\n', '\\'};
        for (u64 candidate_index = 0; candidate_index < BUSTER_ARRAY_LENGTH(candidates); candidate_index += 1)
        {
            for (u64 offset = 0; offset <= 130; offset += 1)
            {
                boundary[offset] = candidates[candidate_index];
                BUSTER_TEST(arguments,
                            c_test_translate_source_paths_agree(arguments->arena, (String8){.pointer = boundary, .length = boundary_capacity}));
                boundary[offset] = 'a';
            }
        }

        static const char8 fuzz_alphabet[] = {'a', 'b', ' ', '\t', '/', '*', '"', '\'', '\\', '\r', '\n', 0, (char8)0x80u, (char8)0xffu};
        u64 fuzz_lengths[] = {255, 256, 511, 512, 1024};
        u64 fuzz_capacity = fuzz_lengths[BUSTER_ARRAY_LENGTH(fuzz_lengths) - 1];
        char8* fuzz = arena_allocate(arguments->arena, char8, fuzz_capacity);
        u64 seed = UINT64_C(0x12345678);
        for (u64 round = 0; round < BUSTER_ARRAY_LENGTH(fuzz_lengths); round += 1)
        {
            u64 fuzz_length = fuzz_lengths[round];
            for (u64 index = 0; index < fuzz_length; index += 1)
            {
                seed = seed * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                fuzz[index] = fuzz_alphabet[(seed >> 33) % BUSTER_ARRAY_LENGTH(fuzz_alphabet)];
            }
            BUSTER_TEST(arguments, c_test_translate_source_paths_agree(arguments->arena, (String8){.pointer = fuzz, .length = fuzz_length}));
        }
        arguments->arena->position = arena_position;
    }

    CLexResult basic = c_lex(arguments->arena, S8("int ma\\\r\nin(void) // comment\r\n"
                                                  "{ return 0; }\r\n"));
    BUSTER_TEST(arguments, basic.diagnostic_count == 0);
    BUSTER_TEST(arguments, basic.token_count == 13);
    c_test_token(arguments, &result, basic, 0, C_TOKEN_IDENTIFIER, S8("int"));
    c_test_token(arguments, &result, basic, 1, C_TOKEN_IDENTIFIER, S8("main"));
    c_test_token(arguments, &result, basic, 2, C_TOKEN_PUNCTUATOR, S8("("));
    c_test_token(arguments, &result, basic, 3, C_TOKEN_IDENTIFIER, S8("void"));
    c_test_token(arguments, &result, basic, 4, C_TOKEN_PUNCTUATOR, S8(")"));
    if (basic.token_count >= 5)
    {
        BUSTER_TEST(arguments, c_lex_token_location(&basic, basic.tokens[1]).line == 1);
        BUSTER_TEST(arguments, c_lex_token_location(&basic, basic.tokens[4]).line == 2);
    }

    CLexResult tokens = c_lex(arguments->arena, S8("u8\"x\" L'a' .5e+2 0x1p-3 "
                                                   "<<= >>= ... -> ++ -- <% %> %: %:%: $gnu @ \\t\n"));
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    c_test_token(arguments, &result, tokens, 0, C_TOKEN_STRING_LITERAL, S8("u8\"x\""));
    c_test_token(arguments, &result, tokens, 1, C_TOKEN_CHARACTER_LITERAL, S8("L'a'"));
    c_test_token(arguments, &result, tokens, 2, C_TOKEN_PREPROCESSING_NUMBER, S8(".5e+2"));
    c_test_token(arguments, &result, tokens, 3, C_TOKEN_PREPROCESSING_NUMBER, S8("0x1p-3"));
    c_test_token(arguments, &result, tokens, 4, C_TOKEN_PUNCTUATOR, S8("<<="));
    c_test_token(arguments, &result, tokens, 13, C_TOKEN_PUNCTUATOR, S8("%:%:"));
    c_test_token(arguments, &result, tokens, 14, C_TOKEN_IDENTIFIER, S8("$gnu"));
    c_test_token(arguments, &result, tokens, 15, C_TOKEN_PUNCTUATOR, S8("@"));
    c_test_token(arguments, &result, tokens, 16, C_TOKEN_PUNCTUATOR, S8("\\"));

    CLexResult comments = c_lex(arguments->arena, S8("a/* first\nsecond */b\n"));
    BUSTER_TEST(arguments, comments.diagnostic_count == 0);
    BUSTER_TEST(arguments, comments.token_count == 5);
    c_test_token(arguments, &result, comments, 0, C_TOKEN_IDENTIFIER, S8("a"));
    c_test_token(arguments, &result, comments, 1, C_TOKEN_NEWLINE, S8("\n"));
    c_test_token(arguments, &result, comments, 2, C_TOKEN_IDENTIFIER, S8("b"));

    CLexResult invalid = c_lex(arguments->arena, S8("\"unterminated\n/* open"));
    BUSTER_TEST(arguments, invalid.diagnostic_count == 2);
    if (invalid.diagnostic_count == 2)
    {
        BUSTER_TEST(arguments, invalid.diagnostics[0].kind == C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL);
        BUSTER_TEST(arguments, invalid.diagnostics[1].kind == C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT);
    }

    CPreprocessResult preprocess = c_preprocess(arguments->arena,
                                                S8("#define ANSWER 40\n"
                                                   "#define TWO 2\n"
                                                   "#define NESTED ANSWER\n"
                                                   "NESTED + TWO\n"
                                                   "#undef TWO\n"
                                                   "TWO\n"),
                                                (CPreprocessOptions){0});
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, preprocess.token_count == 5);
    c_test_preprocessed_token(arguments, &result, preprocess, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("40"));
    c_test_preprocessed_token(arguments, &result, preprocess, 1, C_TOKEN_PUNCTUATOR, S8("+"));
    c_test_preprocessed_token(arguments, &result, preprocess, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));
    c_test_preprocessed_token(arguments, &result, preprocess, 3, C_TOKEN_IDENTIFIER, S8("TWO"));

    CPreprocessorDefinition command_definition = {
        .name = S8("COMMAND_VALUE"),
        .value = S8("9"),
    };
    CPreprocessResult command_preprocess = c_preprocess(arguments->arena, S8("COMMAND_VALUE\n"),
                                                        (CPreprocessOptions){
                                                            .definitions = &command_definition,
                                                            .definition_count = 1,
                                                        });
    BUSTER_TEST(arguments, command_preprocess.diagnostic_count == 0);
    c_test_preprocessed_token(arguments, &result, command_preprocess, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("9"));

    // An empty value is a replacement list with nothing in it, not a missing
    // one: `-DNAME=` expands to nothing, and the `1` a valueless `-DNAME` means
    // is the driver's default, resolved before the option reaches here.
    CPreprocessorDefinition empty_definition = {
        .name = S8("COMMAND_EMPTY"),
    };
    CPreprocessResult empty_preprocess = c_preprocess(arguments->arena, S8("int probe = 0 COMMAND_EMPTY;\n"),
                                                      (CPreprocessOptions){
                                                          .definitions = &empty_definition,
                                                          .definition_count = 1,
                                                      });
    BUSTER_TEST(arguments, empty_preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, empty_preprocess.token_count == 6);
    c_test_preprocessed_token(arguments, &result, empty_preprocess, 3, C_TOKEN_PREPROCESSING_NUMBER, S8("0"));
    c_test_preprocessed_token(arguments, &result, empty_preprocess, 4, C_TOKEN_PUNCTUATOR, S8(";"));

    CPreprocessResult function_macro = c_preprocess(arguments->arena,
                                                    S8("#define ADD(x, y) x + y\n"
                                                       "ADD(1, 2)\n"),
                                                    (CPreprocessOptions){0});
    BUSTER_TEST(arguments, function_macro.diagnostic_count == 0);
    BUSTER_TEST(arguments, function_macro.token_count == 4);
    c_test_preprocessed_token(arguments, &result, function_macro, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));
    c_test_preprocessed_token(arguments, &result, function_macro, 1, C_TOKEN_PUNCTUATOR, S8("+"));
    c_test_preprocessed_token(arguments, &result, function_macro, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));

    CPreprocessResult repeated_parameter_macro = c_preprocess(arguments->arena,
                                                              S8("#define LENGTH(T, count) "
                                                                 "((count) / (sizeof(T) * 8) + "
                                                                 "((count) % (sizeof(T) * 8) != 0))\n"
                                                                 "#define ARRAY(T, N, count) "
                                                                 "T N[LENGTH(T, count)]\n"
                                                                 "#define WORD_ARRAY(N, Count) "
                                                                 "ARRAY(unsigned long, N, "
                                                                 "(unsigned long)(Count))\n"
                                                                 "WORD_ARRAY(flags, 3);\n"),
                                                              (CPreprocessOptions){0});
    BUSTER_TEST(arguments, repeated_parameter_macro.diagnostic_count == 0);
    CParseResult repeated_parameter_parse = c_parse(arguments->arena, repeated_parameter_macro);
    BUSTER_TEST(arguments, repeated_parameter_parse.diagnostic_count == 0);

    CPreprocessResult multiline_macro = c_preprocess(arguments->arena,
                                                     S8("#define DECLARE(type, name) type name\n"
                                                        "DECLARE(\n"
                                                        "    unsigned long,\n"
                                                        "    multiline_value\n"
                                                        ");\n"),
                                                     (CPreprocessOptions){0});
    BUSTER_TEST(arguments, multiline_macro.diagnostic_count == 0);
    CParseResult multiline_macro_parse = c_parse(arguments->arena, multiline_macro);
    BUSTER_TEST(arguments, multiline_macro_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, multiline_macro_parse.declaration_count == 1);

    CPreprocessResult variadic_macro = c_preprocess(arguments->arena,
                                                    S8("#define TAIL(first, ...) __VA_ARGS__\n"
                                                       "TAIL(0, 1, 2)\n"),
                                                    (CPreprocessOptions){0});
    BUSTER_TEST(arguments, variadic_macro.diagnostic_count == 0);
    BUSTER_TEST(arguments, variadic_macro.token_count == 4);
    c_test_preprocessed_token(arguments, &result, variadic_macro, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));
    c_test_preprocessed_token(arguments, &result, variadic_macro, 1, C_TOKEN_PUNCTUATOR, S8(","));
    c_test_preprocessed_token(arguments, &result, variadic_macro, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));

    CPreprocessResult macro_operators = c_preprocess(arguments->arena,
                                                     S8("#define STRINGIFY(value) #value\n"
                                                        "#define PASTE(left, right) left ## right\n"
                                                        "#define OBJECT_PASTE object ## _name\n"
                                                        "#define GNU_TAIL(first, ...) "
                                                        "first, ## __VA_ARGS__\n"
                                                        "STRINGIFY(hello + \"world\")\n"
                                                        "PASTE(name, _suffix)\n"
                                                        "OBJECT_PASTE\n"
                                                        "GNU_TAIL(1)\n"),
                                                     (CPreprocessOptions){0});
    BUSTER_TEST(arguments, macro_operators.diagnostic_count == 0);
    BUSTER_TEST(arguments, macro_operators.token_count == 5);
    c_test_preprocessed_token(arguments, &result, macro_operators, 0, C_TOKEN_STRING_LITERAL, S8("\"hello + \\\"world\\\"\""));
    c_test_preprocessed_token(arguments, &result, macro_operators, 1, C_TOKEN_IDENTIFIER, S8("name_suffix"));
    c_test_preprocessed_token(arguments, &result, macro_operators, 2, C_TOKEN_IDENTIFIER, S8("object_name"));
    c_test_preprocessed_token(arguments, &result, macro_operators, 3, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));

    CPreprocessResult macro_argument_prescan = c_preprocess(arguments->arena,
                                                            S8("#define AFTERX(value) X_ ## value\n"
                                                               "#define XAFTERX(value) AFTERX(value)\n"
                                                               "#define BUFSIZE TABLESIZE\n"
                                                               "#define TABLESIZE 1024\n"
                                                               "XAFTERX(BUFSIZE)\n"
                                                               "AFTERX(BUFSIZE)\n"),
                                                            (CPreprocessOptions){0});
    BUSTER_TEST(arguments, macro_argument_prescan.diagnostic_count == 0);
    BUSTER_TEST(arguments, macro_argument_prescan.token_count == 3);
    c_test_preprocessed_token(arguments, &result, macro_argument_prescan, 0, C_TOKEN_IDENTIFIER, S8("X_1024"));
    c_test_preprocessed_token(arguments, &result, macro_argument_prescan, 1, C_TOKEN_IDENTIFIER, S8("X_BUFSIZE"));

    CPreprocessResult conditional = c_preprocess(arguments->arena,
                                                 S8("#define ENABLED 1\n"
                                                    "#if defined(ENABLED) && "
                                                    "(ENABLED + 1 == 2)\n"
                                                    "kept\n"
                                                    "#else\n"
                                                    "dropped\n"
                                                    "#endif\n"
                                                    "#ifndef MISSING\n"
                                                    "also_kept\n"
                                                    "#endif\n"
                                                    "#if defined(MISSING) ? 0 : 1\n"
                                                    "conditional_kept\n"
                                                    "#endif\n"),
                                                 (CPreprocessOptions){0});
    BUSTER_TEST(arguments, conditional.diagnostic_count == 0);
    BUSTER_TEST(arguments, conditional.token_count == 4);
    c_test_preprocessed_token(arguments, &result, conditional, 0, C_TOKEN_IDENTIFIER, S8("kept"));
    c_test_preprocessed_token(arguments, &result, conditional, 1, C_TOKEN_IDENTIFIER, S8("also_kept"));
    c_test_preprocessed_token(arguments, &result, conditional, 2, C_TOKEN_IDENTIFIER, S8("conditional_kept"));

    CPreprocessResult directive_in_expression = c_preprocess(arguments->arena,
                                                             S8("int selected = (\n"
                                                                "#if 0\n"
                                                                "    1\n"
                                                                "#else\n"
                                                                "    2\n"
                                                                "#endif\n"
                                                                ");\n"),
                                                             (CPreprocessOptions){0});
    BUSTER_TEST(arguments, directive_in_expression.diagnostic_count == 0);
    CParseResult directive_in_expression_parse = c_parse(arguments->arena, directive_in_expression);
    BUSTER_TEST(arguments, directive_in_expression_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, directive_in_expression_parse.declaration_count == 1);

    CPreprocessResult macro_introduced_defined = c_preprocess(arguments->arena,
                                                              S8("#define IS_DRIVERKIT() defined(__DRIVERKIT_VERSION_MIN_REQUIRED)\n"
                                                                 "#if !IS_DRIVERKIT()\n"
                                                                 "int not_driverkit;\n"
                                                                 "#endif\n"),
                                                              (CPreprocessOptions){0});
    BUSTER_TEST(arguments, macro_introduced_defined.diagnostic_count == 0);
    CParseResult macro_introduced_defined_parse = c_parse(arguments->arena, macro_introduced_defined);
    BUSTER_TEST(arguments, macro_introduced_defined_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, macro_introduced_defined_parse.declaration_count == 1);

    CPreprocessResult inactive_objective_c = c_preprocess(arguments->arena,
                                                          S8("#if 0\n"
                                                             "@class Protocol;\n"
                                                             "#endif\n"
                                                             "int plain_c;\n"),
                                                          (CPreprocessOptions){0});
    BUSTER_TEST(arguments, inactive_objective_c.diagnostic_count == 0);
    CParseResult inactive_objective_c_parse = c_parse(arguments->arena, inactive_objective_c);
    BUSTER_TEST(arguments, inactive_objective_c_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, inactive_objective_c_parse.declaration_count == 1);

    CPreprocessResult pragma_pack = c_preprocess(arguments->arena,
                                                 S8("#pragma pack(push, 4)\n"
                                                    "typedef struct { char byte; long long value; } Packed;\n"
                                                    "#pragma pack(pop)\n"
                                                    "typedef struct { char byte; long long value; } Natural;\n"
                                                    "_Static_assert(sizeof(Packed) == 12, \"packed layout\");\n"
                                                    "_Static_assert(sizeof(Natural) == 16, \"natural layout\");\n"),
                                                 (CPreprocessOptions){0});
    BUSTER_TEST(arguments, pragma_pack.diagnostic_count == 0);
    bool packed_alignment_seen = false;
    bool natural_alignment_seen = false;
    for (u32 token_index = 0; token_index < pragma_pack.token_count; token_index += 1)
    {
        char8 const* token_spelling_base = pragma_pack.spelling_base;
        CToken token = pragma_pack.tokens[token_index];
        packed_alignment_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("Packed")) && c_preprocess_pack_alignment(&pragma_pack, token_index) == 4;
        natural_alignment_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("Natural")) && c_preprocess_pack_alignment(&pragma_pack, token_index) == 0;
    }
    BUSTER_TEST(arguments, packed_alignment_seen);
    BUSTER_TEST(arguments, natural_alignment_seen);
    CParseResult pragma_pack_parse = c_parse(arguments->arena, pragma_pack);
    BUSTER_TEST(arguments, pragma_pack_parse.diagnostic_count == 0);

    CPreprocessorDefinition diagnostic_text = {
        .name = S8("DIAGNOSTIC_TEXT"),
        .value = S8("expanded warning"),
    };
    CPreprocessResult preprocess_diagnostics = c_preprocess(arguments->arena,
                                                            S8("#warning direct warning\n"
                                                               "#warning DIAGNOSTIC_TEXT\n"
                                                               "#if 0\n"
                                                               "#error inactive error\n"
                                                               "#warning inactive warning\n"
                                                               "#endif\n"
                                                               "int diagnostic_value;\n"),
                                                            (CPreprocessOptions){
                                                                .definitions = &diagnostic_text,
                                                                .definition_count = 1,
                                                            });
    BUSTER_TEST(arguments, preprocess_diagnostics.diagnostic_count == 2);
    BUSTER_TEST(arguments, preprocess_diagnostics.error_count == 0);
    BUSTER_TEST(arguments, preprocess_diagnostics.warning_count == 2);
    if (preprocess_diagnostics.diagnostic_count == 2)
    {
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[0].kind == C_DIAGNOSTIC_PREPROCESSOR_WARNING);
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[0].severity == C_DIAGNOSTIC_WARNING);
        BUSTER_STRING_TEST(arguments, preprocess_diagnostics.diagnostics[0].message, S8("direct warning"));
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[1].kind == C_DIAGNOSTIC_PREPROCESSOR_WARNING);
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[1].severity == C_DIAGNOSTIC_WARNING);
        BUSTER_STRING_TEST(arguments, preprocess_diagnostics.diagnostics[1].message, S8("DIAGNOSTIC_TEXT"));
    }
    CPreprocessResult preprocess_error = c_preprocess(arguments->arena,
                                                      S8("#define ERROR_TEXT expanded error\n"
                                                         "#error ERROR_TEXT\n"
                                                         "#warning trailing warning\n"
                                                         "int after_error;\n"),
                                                      (CPreprocessOptions){0});
    BUSTER_TEST(arguments, preprocess_error.diagnostic_count == 2);
    BUSTER_TEST(arguments, preprocess_error.error_count == 1);
    BUSTER_TEST(arguments, preprocess_error.warning_count == 1);
    if (preprocess_error.diagnostic_count == 2)
    {
        BUSTER_TEST(arguments, preprocess_error.diagnostics[0].kind == C_DIAGNOSTIC_PREPROCESSOR_ERROR);
        BUSTER_TEST(arguments, preprocess_error.diagnostics[0].severity == C_DIAGNOSTIC_ERROR);
        BUSTER_STRING_TEST(arguments, preprocess_error.diagnostics[0].message, S8("ERROR_TEXT"));
    }

    CPreprocessResult expanded_pragmas = c_preprocess(arguments->arena,
                                                      S8("#define PACK_DIRECTIVE \"pack(push, 4)\"\n"
                                                         "#define SAVED_VALUE 1\n"
                                                         "#define SAVED_FUNCTION(value) value + 1\n"
                                                         "_Pragma(PACK_DIRECTIVE)\n"
                                                         "_Pragma(\"push_macro(\\\"SAVED_VALUE\\\")\")\n"
                                                         "_Pragma(\"push_macro(\\\"SAVED_FUNCTION\\\")\")\n"
                                                         "#define SAVED_VALUE 2\n"
                                                         "#define SAVED_FUNCTION(value, extra) value + extra\n"
                                                         "typedef struct { char byte; long long value; } ExpandedPacked;\n"
                                                         "_Pragma(\"pack(push, 8)\") typedef struct { char byte; long long value; } ExpandedInlinePacked; _Pragma(\"pack(pop)\")\n"
                                                         "_Pragma(\"pop_macro(\\\"SAVED_FUNCTION\\\")\")\n"
                                                         "_Pragma(\"pop_macro(\\\"SAVED_VALUE\\\")\")\n"
                                                         "SAVED_VALUE SAVED_FUNCTION(2)\n"
                                                         "_Pragma(\"pack(pop)\")\n"
                                                         "typedef struct { char byte; long long value; } ExpandedNatural;\n"),
                                                      (CPreprocessOptions){0});
    BUSTER_TEST(arguments, expanded_pragmas.diagnostic_count == 0);
    BUSTER_TEST(arguments, expanded_pragmas.token_count != 0);
    bool expanded_packed_alignment_seen = false;
    bool expanded_inline_packed_alignment_seen = false;
    bool expanded_natural_alignment_seen = false;
    bool restored_value_seen = false;
    bool restored_function_seen = false;
    for (u32 token_index = 0; token_index < expanded_pragmas.token_count; token_index += 1)
    {
        char8 const* token_spelling_base = expanded_pragmas.spelling_base;
        CToken token = expanded_pragmas.tokens[token_index];
        expanded_packed_alignment_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("ExpandedPacked")) && c_preprocess_pack_alignment(&expanded_pragmas, token_index) == 4;
        expanded_inline_packed_alignment_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("ExpandedInlinePacked")) && c_preprocess_pack_alignment(&expanded_pragmas, token_index) == 8;
        expanded_natural_alignment_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("ExpandedNatural")) && c_preprocess_pack_alignment(&expanded_pragmas, token_index) == 0;
        restored_value_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("1"));
        restored_function_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("+"));
        BUSTER_TEST(arguments, token.kind != C_TOKEN_PRAGMA);
    }
    BUSTER_TEST(arguments, expanded_packed_alignment_seen);
    BUSTER_TEST(arguments, expanded_inline_packed_alignment_seen);
    BUSTER_TEST(arguments, expanded_natural_alignment_seen);
    BUSTER_TEST(arguments, restored_value_seen);
    BUSTER_TEST(arguments, restored_function_seen);

    CPreprocessResult windows_pragmas = c_preprocess(arguments->arena,
                                                     S8("__pragma(pack(push, 2))\n"
                                                        "#pragma GCC diagnostic push\n"
                                                        "#pragma clang diagnostic ignored \"-Wunknown\"\n"
                                                        "#pragma visibility push(default)\n"
                                                        "#pragma warning(disable: 4100)\n"
                                                        "#pragma comment(lib, \"ignored\")\n"
                                                        "#pragma region ignored\n"
                                                        "#pragma endregion\n"
                                                        "#pragma omp parallel\n"
                                                        "typedef struct { char byte; long long value; } WindowsPacked;\n"
                                                        "__pragma(pack(pop))\n"
                                                        "typedef struct { char byte; long long value; } WindowsNatural;\n"),
                                                     (CPreprocessOptions){
                                                         .target = {
                                                             .cpu_arch = CPU_ARCH_X86_64,
                                                             .os = OPERATING_SYSTEM_WINDOWS,
                                                         },
                                                     });
    BUSTER_TEST(arguments, windows_pragmas.diagnostic_count == 0);
    bool windows_packed_alignment_seen = false;
    bool windows_natural_alignment_seen = false;
    for (u32 token_index = 0; token_index < windows_pragmas.token_count; token_index += 1)
    {
        char8 const* token_spelling_base = windows_pragmas.spelling_base;
        CToken token = windows_pragmas.tokens[token_index];
        windows_packed_alignment_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("WindowsPacked")) && c_preprocess_pack_alignment(&windows_pragmas, token_index) == 2;
        windows_natural_alignment_seen |= string_equal(c_token_spelling(token_spelling_base, token), S8("WindowsNatural")) && c_preprocess_pack_alignment(&windows_pragmas, token_index) == 0;
    }
    BUSTER_TEST(arguments, windows_packed_alignment_seen);
    BUSTER_TEST(arguments, windows_natural_alignment_seen);

    CPreprocessResult cross_target_clang_macros = c_preprocess(arguments->arena,
                                                                S8("#if defined(__clang__) && __clang_major__ == 18\n"
                                                                   "int clang_compatibility;\n"
                                                                   "#else\n"
                                                                   "#error missing clang compatibility macros\n"
                                                                   "#endif\n"),
                                                                (CPreprocessOptions){
                                                                    .target = {
                                                                        .cpu_arch = CPU_ARCH_X86_64,
                                                                        .os = OPERATING_SYSTEM_LINUX,
                                                                    },
                                                                });
    BUSTER_TEST(arguments, cross_target_clang_macros.diagnostic_count == 0);

    CPreprocessResult unmatched_conditional = c_preprocess(arguments->arena, S8("#if 1\nvalue\n"), (CPreprocessOptions){0});
    BUSTER_TEST(arguments, unmatched_conditional.diagnostic_count == 1);
    if (unmatched_conditional.diagnostic_count)
    {
        BUSTER_TEST(arguments, unmatched_conditional.diagnostics[0].kind == C_DIAGNOSTIC_UNMATCHED_CONDITIONAL);
    }

    CPreprocessResult builtins = c_preprocess(arguments->arena,
                                              S8("__STDC__ __STDC_VERSION__\n"
                                                 "__LINE__ __FILE__\n"
                                                 "__GNUC__ __GNUC_MINOR__ "
                                                 "__GNUC_PATCHLEVEL__\n"),
                                              (CPreprocessOptions){
                                                  .source_path = S8("builtins.c"),
                                              });
    BUSTER_TEST(arguments, builtins.diagnostic_count == 0);
    BUSTER_TEST(arguments, builtins.token_count == 8);
    c_test_preprocessed_token(arguments, &result, builtins, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));
    c_test_preprocessed_token(arguments, &result, builtins, 1, C_TOKEN_PREPROCESSING_NUMBER, S8("201710L"));
    c_test_preprocessed_token(arguments, &result, builtins, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));
    c_test_preprocessed_token(arguments, &result, builtins, 3, C_TOKEN_STRING_LITERAL, S8("\"builtins.c\""));
    c_test_preprocessed_token(arguments, &result, builtins, 4, C_TOKEN_PREPROCESSING_NUMBER, S8("4"));
    c_test_preprocessed_token(arguments, &result, builtins, 5, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));
    c_test_preprocessed_token(arguments, &result, builtins, 6, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));

    // C99 is the dialect a libc's own build asks for: musl's makefile passes
    // -std=c99, and its headers read __STDC_VERSION__ to decide which
    // declarations exist. The GNU flavour of the same year has to keep the
    // extension keywords the strict one does not.
    CPreprocessResult c99_version = c_preprocess(arguments->arena, S8("__STDC_VERSION__\n"),
                                                 (CPreprocessOptions){
                                                     .source_path = S8("c99-version.c"),
                                                     .dialect = C_PREPROCESS_DIALECT_C99,
                                                 });
    BUSTER_TEST(arguments, c99_version.diagnostic_count == 0);
    c_test_preprocessed_token(arguments, &result, c99_version, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("199901L"));
    CPreprocessResult gnu99_version = c_preprocess(arguments->arena, S8("__STDC_VERSION__\n"),
                                                   (CPreprocessOptions){
                                                       .source_path = S8("gnu99-version.c"),
                                                       .dialect = C_PREPROCESS_DIALECT_GNU99,
                                                   });
    BUSTER_TEST(arguments, gnu99_version.diagnostic_count == 0);
    c_test_preprocessed_token(arguments, &result, gnu99_version, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("199901L"));

    CPreprocessResult floating_builtins = c_preprocess(arguments->arena,
                                                       S8("__DBL_EPSILON__\n"),
                                                       (CPreprocessOptions){
                                                           .source_path = S8("floating-builtins.c"),
                                                       });
    BUSTER_TEST(arguments, floating_builtins.diagnostic_count == 0);
    BUSTER_TEST(arguments, floating_builtins.token_count == 2);
    c_test_preprocessed_token(arguments, &result, floating_builtins, 0, C_TOKEN_PREPROCESSING_NUMBER,
                               S8("2.220446049250313080847263336181640625e-16"));

    CPreprocessResult floating_mantissa_builtin = c_preprocess(arguments->arena,
                                                                S8("__DBL_MANT_DIG__\n"),
                                                                (CPreprocessOptions){
                                                                    .source_path = S8("floating-mantissa-builtins.c"),
                                                                });
    BUSTER_TEST(arguments, floating_mantissa_builtin.diagnostic_count == 0);
    BUSTER_TEST(arguments, floating_mantissa_builtin.token_count == 2);
    c_test_preprocessed_token(arguments, &result, floating_mantissa_builtin, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("53"));

    CPreprocessResult floating_decimal_exponent_builtin = c_preprocess(arguments->arena,
                                                                        S8("__DBL_MAX_10_EXP__\n"),
                                                                        (CPreprocessOptions){
                                                                            .source_path = S8("floating-decimal-exponent-builtins.c"),
                                                                        });
    BUSTER_TEST(arguments, floating_decimal_exponent_builtin.diagnostic_count == 0);
    BUSTER_TEST(arguments, floating_decimal_exponent_builtin.token_count == 2);
    c_test_preprocessed_token(arguments, &result, floating_decimal_exponent_builtin, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("308"));

    CPreprocessResult aarch64_android_builtins = c_preprocess(arguments->arena,
                                                              S8("#if defined(__ANDROID__) && "
                                                                 "defined(__linux__) && "
                                                                 "defined(__ELF__) && "
                                                                 "__ANDROID_API__ == 35 && "
                                                                 "__ANDROID_MIN_SDK_VERSION__ == 35 && "
                                                                 "__LP64__ && _LP64 && "
                                                                 "__SIZEOF_POINTER__ == 8 && "
                                                                 "__POINTER_WIDTH__ == 64 && "
                                                                 "__BYTE_ORDER__ == "
                                                                 "__ORDER_LITTLE_ENDIAN__\n"
                                                                 "typedef __CHAR8_TYPE__ TargetChar8;\n"
                                                                 "typedef __CHAR16_TYPE__ TargetChar16;\n"
                                                                 "typedef __CHAR32_TYPE__ TargetChar32;\n"
                                                                 "_Static_assert("
                                                                 "sizeof(TargetChar8) == 1,"
                                                                 " \"char8 width\");\n"
                                                                 "_Static_assert("
                                                                 "sizeof(TargetChar16) == 2,"
                                                                 " \"char16 width\");\n"
                                                                 "_Static_assert("
                                                                 "sizeof(TargetChar32) == 4,"
                                                                 " \"char32 width\");\n"
                                                                 "#else\n"
                                                                 "_Static_assert(0, \"target data model\");\n"
                                                                 "#endif\n"),
                                                              (CPreprocessOptions){
                                                                  .target =
                                                                      {
                                                                          .cpu_arch = CPU_ARCH_AARCH64,
                                                                          .os = OPERATING_SYSTEM_ANDROID,
                                                                          .os_version_major = 35,
                                                                      },
                                                              });
    BUSTER_TEST(arguments, aarch64_android_builtins.diagnostic_count == 0);
    CParseResult aarch64_android_builtins_parse = c_parse(arguments->arena, aarch64_android_builtins);
    BUSTER_TEST(arguments, aarch64_android_builtins_parse.diagnostic_count == 0);

    CPreprocessResult x64_windows_builtins = c_preprocess(arguments->arena,
                                                          S8("#if defined(_WIN64) && "
                                                             "!defined(__LP64__) && "
                                                             "!defined(_LP64) && "
                                                             "__LONG_WIDTH__ == 32 && "
                                                             "__SIZEOF_POINTER__ == 8\n"
                                                             "int windows_data_model;\n"
                                                             "#else\n"
                                                             "_Static_assert(0, \"target data model\");\n"
                                                             "#endif\n"),
                                                          (CPreprocessOptions){
                                                              .target =
                                                                  {
                                                                      .cpu_arch = CPU_ARCH_X86_64,
                                                                      .os = OPERATING_SYSTEM_WINDOWS,
                                                                  },
                                                          });
    BUSTER_TEST(arguments, x64_windows_builtins.diagnostic_count == 0);
    CParseResult x64_windows_builtins_parse = c_parse(arguments->arena, x64_windows_builtins);
    BUSTER_TEST(arguments, x64_windows_builtins_parse.diagnostic_count == 0);

    CPreprocessResult aarch64_macos_builtins = c_preprocess(arguments->arena,
                                                            S8("#if defined(__APPLE__) && "
                                                               "defined(__MACH__) && "
                                                               "defined(__BUSTER__) && "
                                                               "defined(__BUSTER_TARGET_MACOS__) && "
                                                               "__APPLE_CC__ >= 6000 && "
                                                               "defined(__arm64__) && "
                                                               "__has_builtin(__is_target_arch) && "
                                                               "__is_target_arch(arm64) && "
                                                               "__is_target_vendor(apple) && "
                                                               "__is_target_os(macos) && "
                                                               "!__is_target_os(ios) && "
                                                               "!__is_target_environment(simulator)\n"
                                                               "int macos_target;\n"
                                                               "#else\n"
                                                               "_Static_assert(0, \"macOS target macros\");\n"
                                                               "#endif\n"),
                                                            (CPreprocessOptions){
                                                                .target =
                                                                    {
                                                                        .cpu_arch = CPU_ARCH_AARCH64,
                                                                        .os = OPERATING_SYSTEM_MACOS,
                                                                    },
                                                            });
    BUSTER_TEST(arguments, aarch64_macos_builtins.diagnostic_count == 0);
    CParseResult aarch64_macos_builtins_parse = c_parse(arguments->arena, aarch64_macos_builtins);
    BUSTER_TEST(arguments, aarch64_macos_builtins_parse.diagnostic_count == 0);

    CPreprocessResult include = c_preprocess(arguments->arena,
                                             S8("#include \"basic_c_include.h\"\n"
                                                "INCLUDED_VALUE\n"),
                                             (CPreprocessOptions){
                                                 .source_path = S8("tests/include_test.c"),
                                             });
    BUSTER_TEST(arguments, include.diagnostic_count == 0);
    BUSTER_TEST(arguments, include.token_count == 2);
    c_test_preprocessed_token(arguments, &result, include, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("37"));
    CPreprocessResult import = c_preprocess(arguments->arena,
                                            S8("#import \"basic_c_include.h\"\n"
                                               "#import \"basic_c_include.h\"\n"
                                               "INCLUDED_VALUE\n"),
                                            (CPreprocessOptions){
                                                .source_path = S8("tests/import_test.c"),
                                            });
    BUSTER_TEST(arguments, import.diagnostic_count == 0);
    BUSTER_TEST(arguments, import.token_count == 2);
    c_test_preprocessed_token(arguments, &result, import, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("37"));
    // The multiple-include optimization: a header whose whole body sits in
    // one `#ifndef` guard is lexed once however often it is included, so the
    // lexed aggregate counts the root plus one lex and no attribution row
    // repeats.
    CPreprocessResult guarded = c_preprocess(arguments->arena,
                                             S8("#include \"basic_c_guarded_include.h\"\n"
                                                "#include \"basic_c_guarded_include.h\"\n"
                                                "GUARDED_VALUE\n"),
                                             (CPreprocessOptions){
                                                 .source_path = S8("tests/guarded_include_test.c"),
                                             });
    BUSTER_TEST(arguments, guarded.diagnostic_count == 0);
    BUSTER_TEST(arguments, guarded.token_count == 2);
    c_test_preprocessed_token(arguments, &result, guarded, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("41"));
    BUSTER_TEST(arguments, c_preprocess_detail(guarded)->source_lexed.files == 2);
    BUSTER_TEST(arguments, c_preprocess_detail(guarded)->source_unique.files == 2);
    BUSTER_TEST(arguments, c_preprocess_detail(guarded)->lexed_file_count == 2);
    BUSTER_TEST(arguments, c_preprocess_detail(guarded)->lexed_files[0].lex_count == 1);
    BUSTER_TEST(arguments, c_preprocess_detail(guarded)->lexed_files[1].lex_count == 1);
    // Undefining the guard macro re-arms the header: the suppression tests
    // the macro at each inclusion, so the third include is lexed again and
    // the macro it defines comes back.
    CPreprocessResult unguarded = c_preprocess(arguments->arena,
                                               S8("#include \"basic_c_guarded_include.h\"\n"
                                                  "#undef BASIC_C_GUARDED_INCLUDE_H\n"
                                                  "#undef GUARDED_VALUE\n"
                                                  "#include \"basic_c_guarded_include.h\"\n"
                                                  "GUARDED_VALUE\n"),
                                               (CPreprocessOptions){
                                                   .source_path = S8("tests/unguarded_include_test.c"),
                                               });
    BUSTER_TEST(arguments, unguarded.diagnostic_count == 0);
    BUSTER_TEST(arguments, unguarded.token_count == 2);
    c_test_preprocessed_token(arguments, &result, unguarded, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("41"));
    BUSTER_TEST(arguments, c_preprocess_detail(unguarded)->source_lexed.files == 3);
    BUSTER_TEST(arguments, c_preprocess_detail(unguarded)->source_unique.files == 2);
    // A token after the guard's #endif breaks the whole-file shape, so the
    // header is re-lexed at every inclusion and its declaration repeats.
    CPreprocessResult partial_guard = c_preprocess(arguments->arena,
                                                   S8("#include \"basic_c_partial_guard_include.h\"\n"
                                                      "#include \"basic_c_partial_guard_include.h\"\n"),
                                                   (CPreprocessOptions){
                                                       .source_path = S8("tests/partial_guard_include_test.c"),
                                                   });
    BUSTER_TEST(arguments, partial_guard.diagnostic_count == 0);
    BUSTER_TEST(arguments, partial_guard.token_count == 7);
    BUSTER_TEST(arguments, c_preprocess_detail(partial_guard)->source_lexed.files == 3);
    BUSTER_TEST(arguments, c_preprocess_detail(partial_guard)->source_unique.files == 2);
    CPreprocessResult builtin_headers = c_preprocess(arguments->arena,
                                                     S8("#include <stdbool.h>\n"
                                                        "#include <stdalign.h>\n"
                                                        "#include <stdarg.h>\n"
                                                        "#include <stddef.h>\n"
                                                        "#include <limits.h>\n"
                                                        "alignas(8) int aligned_value;\n"
                                                        "size_t builtin_size;\n"
                                                        "ptrdiff_t builtin_difference;\n"
                                                        "max_align_t builtin_alignment;\n"
                                                        "_Static_assert(CHAR_BIT == 8,"
                                                        " \"character width\");\n"
                                                        "_Static_assert(INT_MAX == 2147483647,"
                                                        " \"integer maximum\");\n"
                                                        "int variadic_value(bool enabled, ...) {\n"
                                                        "    va_list arguments;\n"
                                                        "    va_start(arguments, enabled);\n"
                                                        "    int value = va_arg(arguments, int);\n"
                                                        "    va_end(arguments);\n"
                                                        "    return enabled ? value : false;\n"
                                                        "}\n"),
                                                     (CPreprocessOptions){
                                                         .source_path = S8("builtin-headers.c"),
                                                     });
    BUSTER_TEST(arguments, builtin_headers.diagnostic_count == 0);
    CParseResult builtin_headers_parse = c_parse(arguments->arena, builtin_headers);
    BUSTER_TEST(arguments, builtin_headers_parse.diagnostic_count == 0);

    FileMapRead hermetic_c_source_map = file_map_read(arguments->arena, S8("tests/fuzz/valid_c.c"), (FileReadOptions){0});
    String8 hermetic_c_source = BYTE_SLICE_TO_STRING(8, hermetic_c_source_map.bytes);
    BUSTER_TEST(arguments, hermetic_c_source.pointer != 0);
    CPreprocessResult hermetic_builtin_headers = c_preprocess(arguments->arena, hermetic_c_source,
                                                              (CPreprocessOptions){
                                                                  .source_path = S8("fuzz.c"),
                                                                  .target = target_native,
                                                                  .data_layout = target_data_layout(target_native),
                                                                  .disable_external_includes = true,
                                                              });
    BUSTER_TEST(arguments, hermetic_builtin_headers.diagnostic_count == 0);
    CParserResult hermetic_syntax = c_parse_ast(arguments->arena, hermetic_builtin_headers);
    BUSTER_TEST(arguments, hermetic_syntax.diagnostic_count == 0);
    CIRLowerResult hermetic_ir = c_analyze(arguments->arena, S8("fuzz.c"), hermetic_builtin_headers, hermetic_syntax, target_native);
    BUSTER_TEST(arguments, hermetic_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, hermetic_ir.program != 0);
    if (hermetic_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(hermetic_ir.program, &hermetic_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    file_map_unmap(hermetic_c_source_map);
    CPreprocessResult hermetic_blocked_headers = c_preprocess(arguments->arena,
                                                              S8("#include <stddef.h>\n"
                                                                 "#include \"basic_c_include.h\"\n"
                                                                 "#include <basic_c_include.h>\n"
                                                                 "#include \"/definitely/missing/buster-header.h\"\n"
                                                                 "size_t value;\n"),
                                                              (CPreprocessOptions){
                                                                  .source_path = S8("fuzz.c"),
                                                                  .target = target_native,
                                                                  .data_layout = target_data_layout(target_native),
                                                                  .disable_external_includes = true,
                                                              });
    BUSTER_TEST(arguments, hermetic_blocked_headers.diagnostic_count == 3);
    for (u64 diagnostic_index = 0; diagnostic_index < hermetic_blocked_headers.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, hermetic_blocked_headers.diagnostics[diagnostic_index].kind == C_DIAGNOSTIC_INCLUDE_NOT_FOUND);
    }
    String8 feature_include_paths[] = {
        S8("tests"),
        S8("tests/include_first"),
        S8("tests/include_second"),
    };
    CPreprocessResult feature_queries = c_preprocess(arguments->arena,
                                                     S8("#define HEADER_AVAILABLE(header)"
                                                        " __has_include(header)\n"
                                                        "#if HEADER_AVAILABLE("
                                                        "\"basic_c_include.h\")\n"
                                                        "11\n"
                                                        "#else\n"
                                                        "12\n"
                                                        "#endif\n"
                                                        "#if __has_include("
                                                        "<basic_c_include.h>)\n"
                                                        "21\n"
                                                        "#else\n"
                                                        "22\n"
                                                        "#endif\n"
                                                        "#if __has_include("
                                                        "\"missing_buster_header.h\")\n"
                                                        "31\n"
                                                        "#else\n"
                                                        "32\n"
                                                        "#endif\n"
                                                        "#if __has_builtin("
                                                        "__builtin_debugtrap) &&"
                                                        " !__has_builtin("
                                                        "__builtin_not_implemented)\n"
                                                        "51\n"
                                                        "#else\n"
                                                        "52\n"
                                                        "#endif\n"
                                                        "#if __has_attribute(vector_size)"
                                                        " && !__has_feature("
                                                        "not_implemented)\n"
                                                        "61\n"
                                                        "#else\n"
                                                        "62\n"
                                                        "#endif\n"
                                                        "#include <feature_next.h>\n"
                                                        "#include <actual_next.h>\n"),
                                                     (CPreprocessOptions){
                                                         .include_paths = feature_include_paths,
                                                         .source_path = S8("tests/feature_queries.c"),
                                                         .include_path_count = BUSTER_ARRAY_LENGTH(feature_include_paths),
                                                     });
    BUSTER_TEST(arguments, feature_queries.diagnostic_count == 0);
    BUSTER_TEST(arguments, feature_queries.token_count == 8);
    String8 feature_query_values[] = {
        S8("11"), S8("21"), S8("32"), S8("51"), S8("61"), S8("41"), S8("73"),
    };
    for (u32 query_index = 0; query_index < BUSTER_ARRAY_LENGTH(feature_query_values); query_index += 1)
    {
        c_test_preprocessed_token(arguments, &result, feature_queries, query_index, C_TOKEN_PREPROCESSING_NUMBER, feature_query_values[query_index]);
    }
    String8 builtin_include_next_system_paths[] = {
        S8("tests/include_second"),
    };
    CPreprocessResult builtin_include_next = c_preprocess(arguments->arena,
                                                           S8("#include <buster_test_builtin_include_next.h>\n"),
                                                           (CPreprocessOptions){
                                                               .system_include_paths = builtin_include_next_system_paths,
                                                               .source_path = S8("tests/builtin_include_next.c"),
                                                               .system_include_path_count = BUSTER_ARRAY_LENGTH(builtin_include_next_system_paths),
                                                           });
    BUSTER_TEST(arguments, builtin_include_next.diagnostic_count == 0);
    BUSTER_TEST(arguments, builtin_include_next.token_count == 2);
    c_test_preprocessed_token(arguments, &result, builtin_include_next, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("79"));
    CPreprocessResult included_warning_growth = c_preprocess(arguments->arena,
                                                             S8("#include \"basic_c_preprocessor_warning_include.h\"\n"
                                                                "int included_warning_fixture;\n"),
                                                             (CPreprocessOptions){
                                                                 .source_path = S8("tests/basic_c_preprocessor_warning_include.c"),
                                                             });
    BUSTER_TEST(arguments, included_warning_growth.diagnostic_count == 65);
    BUSTER_TEST(arguments, included_warning_growth.error_count == 0);
    BUSTER_TEST(arguments, included_warning_growth.warning_count == 65);
    if (included_warning_growth.diagnostic_count == 65)
    {
        BUSTER_STRING_TEST(arguments, included_warning_growth.diagnostics[0].message, S8("included warning 00"));
        BUSTER_STRING_TEST(arguments, included_warning_growth.diagnostics[64].message, S8("included warning 64"));
    }
    CPreprocessResult line_remapping = c_preprocess(arguments->arena,
                                                    S8("#define REMAPPED_LINE 200\n"
                                                       "#line REMAPPED_LINE \"generated.c\"\n"
                                                       "__LINE__ __FILE__ token\n"
                                                       "#line 7\n"
                                                       "__LINE__ __FILE__ token\n"),
                                                    (CPreprocessOptions){
                                                        .source_path = S8("tests/original.c"),
                                                    });
    BUSTER_TEST(arguments, line_remapping.diagnostic_count == 0);
    BUSTER_TEST(arguments, line_remapping.token_count == 7);
    String8 line_remapping_values[] = {
        S8("200"), S8("\"generated.c\""), S8("token"), S8("7"), S8("\"generated.c\""), S8("token"),
    };
    CTokenKind line_remapping_kinds[] = {
        C_TOKEN_PREPROCESSING_NUMBER, C_TOKEN_STRING_LITERAL, C_TOKEN_IDENTIFIER, C_TOKEN_PREPROCESSING_NUMBER, C_TOKEN_STRING_LITERAL, C_TOKEN_IDENTIFIER,
    };
    for (u32 remapping_index = 0; remapping_index < BUSTER_ARRAY_LENGTH(line_remapping_values); remapping_index += 1)
    {
        c_test_preprocessed_token(arguments, &result, line_remapping, remapping_index, line_remapping_kinds[remapping_index],
                                  line_remapping_values[remapping_index]);
    }
    if (line_remapping.token_count >= 6)
    {
        BUSTER_TEST(arguments, c_preprocess_token_location(&line_remapping, line_remapping.tokens[0]).line == 200);
        BUSTER_TEST(arguments, c_preprocess_token_location(&line_remapping, line_remapping.tokens[2]).line == 200);
        BUSTER_TEST(arguments, c_preprocess_token_location(&line_remapping, line_remapping.tokens[3]).line == 7);
        BUSTER_TEST(arguments, c_preprocess_token_location(&line_remapping, line_remapping.tokens[5]).line == 7);
    }
    CPreprocessResult gnu_line_markers = c_preprocess(arguments->arena,
                                                      S8("# 42 \"generated.i\" 1 3 4\n"
                                                         "__LINE__ __FILE__ token\n"
                                                         "# 9 \"original.c\" 2\n"
                                                         "__LINE__ __FILE__ token\n"),
                                                      (CPreprocessOptions){
                                                          .source_path = S8("tests/preprocessed.i"),
                                                      });
    BUSTER_TEST(arguments, gnu_line_markers.diagnostic_count == 0);
    BUSTER_TEST(arguments, gnu_line_markers.token_count == 7);
    String8 gnu_line_marker_values[] = {
        S8("42"), S8("\"generated.i\""), S8("token"), S8("9"), S8("\"original.c\""), S8("token"),
    };
    for (u32 marker_index = 0; marker_index < BUSTER_ARRAY_LENGTH(gnu_line_marker_values); marker_index += 1)
    {
        c_test_preprocessed_token(arguments, &result, gnu_line_markers, marker_index,
                                  marker_index == 1 || marker_index == 4   ? C_TOKEN_STRING_LITERAL
                                  : marker_index == 2 || marker_index == 5 ? C_TOKEN_IDENTIFIER
                                                                           : C_TOKEN_PREPROCESSING_NUMBER,
                                  gnu_line_marker_values[marker_index]);
    }
    if (gnu_line_markers.token_count >= 6)
    {
        BUSTER_TEST(arguments, c_preprocess_token_location(&gnu_line_markers, gnu_line_markers.tokens[0]).line == 42);
        BUSTER_TEST(arguments, c_preprocess_token_location(&gnu_line_markers, gnu_line_markers.tokens[3]).line == 9);
    }
    CPreprocessResult invalid_line = c_preprocess(arguments->arena,
                                                  S8("#line 0\n"
                                                     "#line 2 \"generated.c\" extra\n"
                                                     "# 3 \"generated.i\" 5\n"),
                                                  (CPreprocessOptions){
                                                      .source_path = S8("tests/invalid_line.c"),
                                                  });
    BUSTER_TEST(arguments, invalid_line.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_line.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_line.diagnostics[diagnostic_index].kind == C_DIAGNOSTIC_INVALID_LINE);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_position_index_tiles(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 source_capacity = BUSTER_MB(1);
    char8* source_bytes = arena_allocate(arguments->arena, char8, source_capacity);
    u64 source_length = 0;
    for (u32 item = 0; item < 400; item += 1)
    {
        if (item % 5 == 0)
        {
            c_test_append_source(source_bytes, source_capacity, &source_length,
                                 string_format(arguments->arena, S8("typedef int vec_{u32} __attribute__((vector_size(16)));\n"), item));
        }
        if (item % 7 == 0)
        {
            c_test_append_source(source_bytes, source_capacity, &source_length,
                                 string_format(arguments->arena, S8("_Alignas(16) int aligned_{u32};\n"), item));
        }
        c_test_append_source(source_bytes, source_capacity, &source_length,
                             string_format(arguments->arena, S8("int function_{u32}(int x) {{ label_{u32}: return x; }}\n"), item, item));
    }
    CPreprocessResult preprocess = c_preprocess(arguments->arena, (String8){.pointer = source_bytes, .length = source_length}, (CPreprocessOptions){0});
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, preprocess.token_count > UINT64_C(4096));
    CParseResult parse = c_parse(arguments->arena, preprocess);
    c_parse_position_index_ensure(&parse, preprocess);
    CTokenPositionIndex* indexed = parse.position_index;
    BUSTER_TEST(arguments, indexed && indexed->built);
    if (!indexed || !indexed->built || preprocess.token_count > UINT32_MAX)
    {
        return result;
    }
    u32 token_count = (u32)preprocess.token_count;
    u32* expected_matching = arena_allocate(arguments->arena, u32, token_count ? token_count : 1);
    u32* expected_vector_size = arena_allocate(arguments->arena, u32, token_count ? token_count : 1);
    u32* expected_alignas = arena_allocate(arguments->arena, u32, token_count ? token_count : 1);
    u32* expected_labels = arena_allocate(arguments->arena, u32, token_count ? token_count : 1);
    u32* expected_attributes = arena_allocate(arguments->arena, u32, token_count ? token_count : 1);
    u32* expected_stack_positions = arena_allocate(arguments->arena, u32, token_count ? token_count : 1);
    CPunctuator* expected_stack_openings = arena_allocate(arguments->arena, CPunctuator, token_count ? token_count : 1);
    memset(expected_matching, 0xff, sizeof(*expected_matching) * token_count);
    u32 vector_size_count = 0;
    u32 alignas_count = 0;
    u32 label_count = 0;
    u32 attribute_count = 0;
    u32 stack_count = 0;
    u32 mismatch_count = 0;
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        CTokenShape shape = c_preprocess_token_shape(&preprocess, token_index);
        if (shape == C_TOKEN_IDENTIFIER)
        {
            String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]);
            if (string_equal(spelling, S8("vector_size")))
            {
                expected_vector_size[vector_size_count++] = token_index;
            }
            if (string_equal(spelling, S8("_Alignas")))
            {
                expected_alignas[alignas_count++] = token_index;
            }
            if (string_equal(spelling, S8("__attribute__")) || string_equal(spelling, S8("__attribute")))
            {
                expected_attributes[attribute_count++] = token_index;
            }
            if (token_index + 1 < token_count &&
                c_token_shape_punctuator(c_preprocess_token_shape(&preprocess, token_index + 1)) == C_PUNCTUATOR_COLON)
            {
                expected_labels[label_count++] = token_index;
            }
            continue;
        }
        CPunctuator punctuator = c_token_shape_punctuator(shape);
        if (punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS || punctuator == C_PUNCTUATOR_LEFT_BRACKET || punctuator == C_PUNCTUATOR_LEFT_BRACE)
        {
            expected_stack_positions[stack_count] = token_index;
            expected_stack_openings[stack_count] = punctuator;
            stack_count += 1;
            continue;
        }
        CPunctuator expected = punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS ? C_PUNCTUATOR_LEFT_PARENTHESIS
                               : punctuator == C_PUNCTUATOR_RIGHT_BRACKET   ? C_PUNCTUATOR_LEFT_BRACKET
                                                                             : punctuator == C_PUNCTUATOR_RIGHT_BRACE ? C_PUNCTUATOR_LEFT_BRACE : C_PUNCTUATOR_NONE;
        if (expected == C_PUNCTUATOR_NONE)
        {
            continue;
        }
        if (!stack_count || expected_stack_openings[stack_count - 1] != expected)
        {
            mismatch_count += 1;
            stack_count = 0;
        }
        else
        {
            expected_matching[expected_stack_positions[--stack_count]] = token_index;
        }
    }
    mismatch_count += stack_count;
    BUSTER_TEST(arguments, indexed->vector_size_count == vector_size_count);
    BUSTER_TEST(arguments, indexed->alignas_count == alignas_count);
    BUSTER_TEST(arguments, indexed->label_candidate_count == label_count);
    BUSTER_TEST(arguments, indexed->attribute_count == attribute_count);
    BUSTER_TEST(arguments, indexed->delimiter_mismatch_count == mismatch_count);
    if (indexed->vector_size_count == vector_size_count)
    {
        BUSTER_TEST(arguments, memcmp(indexed->vector_size_positions, expected_vector_size, sizeof(*expected_vector_size) * vector_size_count) == 0);
    }
    if (indexed->alignas_count == alignas_count)
    {
        BUSTER_TEST(arguments, memcmp(indexed->alignas_positions, expected_alignas, sizeof(*expected_alignas) * alignas_count) == 0);
    }
    if (indexed->label_candidate_count == label_count)
    {
        BUSTER_TEST(arguments, memcmp(indexed->label_candidate_positions, expected_labels, sizeof(*expected_labels) * label_count) == 0);
    }
    if (indexed->attribute_count == attribute_count)
    {
        BUSTER_TEST(arguments, memcmp(indexed->attribute_positions, expected_attributes, sizeof(*expected_attributes) * attribute_count) == 0);
    }
    BUSTER_TEST(arguments, memcmp(indexed->matching_delimiters, expected_matching, sizeof(*expected_matching) * token_count) == 0);

    // Run the same index through the scalar/reference shape fallback. The
    // production sidecar is present above; clearing only this private pointer
    // makes c_parse_position_index_build derive shapes from CToken without
    // changing the token stream or the expected populations.
    CTokenPositionIndex scalar_index = {0};
    CTokenPositionIndex* saved_index = parse.position_index;
    CTokenShape* saved_shapes = preprocess.recovery ? preprocess.recovery->token_shapes : 0;
    parse.position_index = &scalar_index;
    if (preprocess.recovery)
    {
        preprocess.recovery->token_shapes = 0;
    }
    c_parse_position_index_ensure(&parse, preprocess);
    if (preprocess.recovery)
    {
        preprocess.recovery->token_shapes = saved_shapes;
    }
    parse.position_index = saved_index;
    BUSTER_TEST(arguments, scalar_index.built);
    BUSTER_TEST(arguments, scalar_index.vector_size_count == vector_size_count);
    BUSTER_TEST(arguments, scalar_index.alignas_count == alignas_count);
    BUSTER_TEST(arguments, scalar_index.label_candidate_count == label_count);
    BUSTER_TEST(arguments, scalar_index.attribute_count == attribute_count);
    BUSTER_TEST(arguments, scalar_index.delimiter_mismatch_count == mismatch_count);
    if (scalar_index.vector_size_count == vector_size_count)
    {
        BUSTER_TEST(arguments, memcmp(scalar_index.vector_size_positions, expected_vector_size, sizeof(*expected_vector_size) * vector_size_count) == 0);
    }
    if (scalar_index.alignas_count == alignas_count)
    {
        BUSTER_TEST(arguments, memcmp(scalar_index.alignas_positions, expected_alignas, sizeof(*expected_alignas) * alignas_count) == 0);
    }
    if (scalar_index.label_candidate_count == label_count)
    {
        BUSTER_TEST(arguments, memcmp(scalar_index.label_candidate_positions, expected_labels, sizeof(*expected_labels) * label_count) == 0);
    }
    if (scalar_index.attribute_count == attribute_count)
    {
        BUSTER_TEST(arguments, memcmp(scalar_index.attribute_positions, expected_attributes, sizeof(*expected_attributes) * attribute_count) == 0);
    }
    BUSTER_TEST(arguments, memcmp(scalar_index.matching_delimiters, expected_matching, sizeof(*expected_matching) * token_count) == 0);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_semantic_basics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    CPreprocessResult local_declaration_tokens = c_preprocess(arguments->arena,
                                                              S8("typedef struct SignalInfo {\n"
                                                                 "    int code;\n"
                                                                 "    union {\n"
                                                                 "        int value;\n"
                                                                 "        struct {\n"
                                                                 "            void *lower;\n"
                                                                 "            void *upper;\n"
                                                                 "        } bounds;\n"
                                                                 "    } fields;\n"
                                                                 "} SignalInfo;\n"
                                                                 "typedef unsigned char Byte;\n"
                                                                 "typedef unsigned short Wide, *WidePointer, **WidePointerPointer;\n"
                                                                 "static Byte const byte_table[2];\n"
                                                                 "typedef int Callback("
                                                                 "int, void *user_data);\n"
                                                                 "typedef int (*CallbackPointer)(int);\n"
                                                                 "int signal_offset(void)\n"
                                                                 "{\n"
                                                                 "    return __builtin_offsetof("
                                                                 "SignalInfo, code);\n"
                                                                 "}\n"
                                                                 "int recursive_input(int value)\n"
                                                                 "{\n"
                                                                 "    if (value)\n"
                                                                 "        return recursive_input(value - 1);\n"
                                                                 "    return value;\n"
                                                                 "}\n"
                                                                 "int local_declarations(void)\n"
                                                                 "{\n"
                                                                 "    typedef struct LocalPair {\n"
                                                                 "        int left;\n"
                                                                 "        int right;\n"
                                                                 "    } LocalPair;\n"
                                                                 "    typedef enum Local {\n"
                                                                 "        LOCAL_FIRST = 1u << 2,\n"
                                                                 "        LOCAL_SECOND,\n"
                                                                 "        LOCAL_THIRD ="
                                                                 " (unsigned int)LOCAL_SECOND << 1\n"
                                                                 "    } Local;\n"
                                                                 "    _Static_assert("
                                                                 "LOCAL_THIRD == 10,"
                                                                 " \"local enum value\");\n"
                                                                 "    int first = 1,"
                                                                 " second = first + 1,"
                                                                 " *pointer = &second;\n"
                                                                 "    __attribute__((unused))"
                                                                 " Local value = LOCAL_SECOND;\n"
                                                                 "    int offset = __builtin_offsetof("
                                                                 "SignalInfo, code);\n"
                                                                 "    Callback *callback;\n"
                                                                 "    LocalPair pair;\n"
                                                                 "    return value + *pointer;\n"
                                                                 "}\n"),
                                                              (CPreprocessOptions){0});
    CParseResult local_declarations = c_parse(arguments->arena, local_declaration_tokens);
    BUSTER_TEST(arguments, local_declarations.diagnostic_count == 0);
    CPreprocessResult interposed_attribute_tokens = c_preprocess(arguments->arena,
                                                                 S8("static __attribute__((always_inline))"
                                                                    " inline int attributed_add("
                                                                    "int x, int y)"
                                                                    "{ return x + y; }\n"),
                                                                 (CPreprocessOptions){0});
    CParseResult interposed_attribute_parse = c_parse(arguments->arena, interposed_attribute_tokens);
    BUSTER_TEST(arguments, interposed_attribute_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, interposed_attribute_parse.declaration_count == 1);
    if (interposed_attribute_parse.declaration_count == 1)
    {
        CDeclaration attributed = interposed_attribute_parse.declarations[0];
        BUSTER_TEST(arguments, attributed.kind == C_DECLARATION_FUNCTION);
        BUSTER_TEST(arguments, attributed.parameter_count == 2);
        BUSTER_TEST(arguments, attributed.type.value != C_ID_UNDERLYING_INVALID);
    }
    for (u32 use_index = 0; use_index < interposed_attribute_parse.identifier_use_count; use_index += 1)
    {
        CIdentifierUse use = interposed_attribute_parse.identifier_uses[use_index];
        BUSTER_TEST(arguments, use.entity.value != C_ID_UNDERLYING_INVALID);
    }
    bool found_callback_typedef = false;
    bool found_callback_pointer_typedef = false;
    bool found_signal_info_typedef = false;
    bool found_const_typedef_object = false;
    bool found_local_typedef = false;
    bool found_local_pair_typedef = false;
    bool found_local_first = false;
    bool found_local_second = false;
    bool found_local_third = false;
    bool found_wide = false;
    bool found_wide_pointer = false;
    bool found_wide_pointer_pointer = false;
    bool found_first = false;
    bool found_second = false;
    bool found_pointer = false;
    bool found_callback = false;
    for (u32 entity_index = 0; entity_index < local_declarations.entity_count; entity_index += 1)
    {
        CEntity* entity = &local_declarations.entities[entity_index];
        found_callback_typedef |= entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, S8("Callback"));
        found_callback_pointer_typedef |=
            entity->kind == C_ENTITY_TYPEDEF && entity->type.value != C_ID_UNDERLYING_INVALID && string_equal(entity->name, S8("CallbackPointer"));
        if (entity->kind == C_ENTITY_TYPEDEF && entity->type.value < local_declarations.type_count)
        {
            CType* type = &local_declarations.types[entity->type.value];
            found_wide |= string_equal(entity->name, S8("Wide")) && type->kind == C_TYPE_UNSIGNED_SHORT;
            if (string_equal(entity->name, S8("WidePointer")) && type->kind == C_TYPE_POINTER &&
                type->element_type.value < local_declarations.type_count)
            {
                found_wide_pointer = local_declarations.types[type->element_type.value].kind == C_TYPE_UNSIGNED_SHORT;
            }
            if (string_equal(entity->name, S8("WidePointerPointer")) && type->kind == C_TYPE_POINTER &&
                type->element_type.value < local_declarations.type_count)
            {
                CType* pointer = &local_declarations.types[type->element_type.value];
                found_wide_pointer_pointer = pointer->kind == C_TYPE_POINTER && pointer->element_type.value < local_declarations.type_count &&
                                             local_declarations.types[pointer->element_type.value].kind == C_TYPE_UNSIGNED_SHORT;
            }
        }
        found_signal_info_typedef |=
            entity->kind == C_ENTITY_TYPEDEF && entity->type.value != C_ID_UNDERLYING_INVALID && string_equal(entity->name, S8("SignalInfo"));
        if (string_equal(entity->name, S8("byte_table")) && entity->type.value < local_declarations.type_count)
        {
            CType* array_type = &local_declarations.types[entity->type.value];
            if (array_type->kind == C_TYPE_ARRAY && array_type->element_type.value < local_declarations.type_count)
            {
                found_const_typedef_object = local_declarations.types[array_type->element_type.value].is_const;
            }
        }
        found_local_typedef |= entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, S8("Local"));
        found_local_pair_typedef |= entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, S8("LocalPair"));
        found_local_first |= entity->kind == C_ENTITY_ENUMERATOR && string_equal(entity->name, S8("LOCAL_FIRST")) && entity->constant_value == 4;
        found_local_second |= entity->kind == C_ENTITY_ENUMERATOR && string_equal(entity->name, S8("LOCAL_SECOND")) && entity->constant_value == 5;
        found_local_third |= entity->kind == C_ENTITY_ENUMERATOR && string_equal(entity->name, S8("LOCAL_THIRD")) && entity->constant_value == 10;
        found_first |= string_equal(entity->name, S8("first"));
        found_second |= string_equal(entity->name, S8("second"));
        found_pointer |= string_equal(entity->name, S8("pointer"));
        found_callback |= string_equal(entity->name, S8("callback"));
    }
    BUSTER_TEST(arguments, found_callback_typedef);
    BUSTER_TEST(arguments, found_callback_pointer_typedef);
    BUSTER_TEST(arguments, found_signal_info_typedef);
    BUSTER_TEST(arguments, found_const_typedef_object);
    BUSTER_TEST(arguments, found_local_typedef);
    BUSTER_TEST(arguments, found_local_pair_typedef);
    BUSTER_TEST(arguments, found_local_first);
    BUSTER_TEST(arguments, found_local_second);
    BUSTER_TEST(arguments, found_local_third);
    BUSTER_TEST(arguments, found_wide);
    BUSTER_TEST(arguments, found_wide_pointer);
    BUSTER_TEST(arguments, found_wide_pointer_pointer);
    BUSTER_TEST(arguments, found_first);
    BUSTER_TEST(arguments, found_second);
    BUSTER_TEST(arguments, found_pointer);
    BUSTER_TEST(arguments, found_callback);

    CPreprocessResult static_assert_tokens = c_preprocess(arguments->arena,
                                                          S8("enum StaticValue {\n"
                                                             "    STATIC_VALUE = 3\n"
                                                             "};\n"
                                                             "typedef struct StaticPair {\n"
                                                             "    int values[(2)];\n"
                                                             "} StaticPair;\n"
                                                             "typedef union StaticLayout {\n"
                                                             "    struct { int first; int second; };\n"
                                                             "    int values[4];\n"
                                                             "} StaticLayout;\n"
                                                             "_Static_assert("
                                                             "STATIC_VALUE == 3,"
                                                             " \"enum value\");\n"
                                                             "_Static_assert("
                                                             "sizeof(StaticPair) == 8,"
                                                             " \"pair size\");\n"
                                                             "_Static_assert("
                                                             "sizeof(StaticLayout) == 16,"
                                                             " \"anonymous layout size\");\n"
                                                             "int member_static_assert(void) {\n"
                                                             "    StaticPair pair = {0};\n"
                                                             "    _Static_assert("
                                                             "sizeof(pair.values[0]) == 4,"
                                                             " \"member element size\");\n"
                                                             "    return 0;\n"
                                                             "}\n"),
                                                          (CPreprocessOptions){0});
    CParserResult static_assert_syntax = c_parse_ast(arguments->arena, static_assert_tokens);
    u32 parsed_global_static_assert_count = 0;
    u32 parsed_local_static_assert_count = 0;
    for (CParserDeclaration* syntax_declaration = static_assert_syntax.first_declaration; syntax_declaration; syntax_declaration = syntax_declaration->next)
    {
        parsed_global_static_assert_count += syntax_declaration->kind == C_PARSER_DECLARATION_STATIC_ASSERT;
        if (syntax_declaration->kind == C_PARSER_DECLARATION_FUNCTION)
        {
            for (CParserStatement* statement = syntax_declaration->first_statement; statement; statement = statement->next)
            {
                parsed_local_static_assert_count += statement->kind == C_PARSER_STATEMENT_STATIC_ASSERT;
            }
        }
    }
    BUSTER_TEST(arguments, static_assert_syntax.diagnostic_count == 0);
    BUSTER_TEST(arguments, parsed_global_static_assert_count == 3);
    BUSTER_TEST(arguments, parsed_local_static_assert_count == 1);
    CParseResult static_assert_parse = c_parse(arguments->arena, static_assert_tokens);
    BUSTER_TEST(arguments, static_assert_parse.diagnostic_count == 0);

    CPreprocessResult failed_static_assert_tokens = c_preprocess(arguments->arena, S8("_Static_assert(0, \"expected failure\");\n"), (CPreprocessOptions){0});
    CParserResult failed_static_assert_syntax = c_parse_ast(arguments->arena, failed_static_assert_tokens);
    BUSTER_TEST(arguments, failed_static_assert_syntax.diagnostic_count == 0);
    CIRLowerResult failed_static_assert_analysis =
        c_analyze(arguments->arena, S8("failed-static-assert.c"), failed_static_assert_tokens, failed_static_assert_syntax, target_native);
    BUSTER_TEST(arguments, failed_static_assert_analysis.diagnostic_count == 1);
    CParseResult failed_static_assert_parse = c_parse(arguments->arena, failed_static_assert_tokens);
    BUSTER_TEST(arguments, failed_static_assert_parse.diagnostic_count == 1);
    if (failed_static_assert_parse.diagnostic_count)
    {
        BUSTER_TEST(arguments, failed_static_assert_parse.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
        BUSTER_STRING_TEST(arguments, failed_static_assert_parse.diagnostics[0].message,
                           S8("static assertion failed: "
                              "\"expected failure\""));
    }

    TemporalArena mismatched_delimiter_temporary = scratch_begin(0, 0);
    CPreprocessResult mismatched_delimiter_tokens = c_preprocess(mismatched_delimiter_temporary.arena,
                                                                 S8("int main(void) { return (0; }\n"),
                                                                 (CPreprocessOptions){0});
    CParserResult mismatched_delimiter_syntax = c_parse_ast(mismatched_delimiter_temporary.arena, mismatched_delimiter_tokens);
    CIRLowerResult mismatched_delimiter_ir =
        c_analyze(mismatched_delimiter_temporary.arena, S8("mismatched-delimiter.c"), mismatched_delimiter_tokens, mismatched_delimiter_syntax, target_native);
    BUSTER_TEST(arguments, mismatched_delimiter_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, mismatched_delimiter_syntax.diagnostic_count == 0);
    BUSTER_TEST(arguments, mismatched_delimiter_ir.diagnostic_count == 1);
    if (mismatched_delimiter_ir.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, mismatched_delimiter_ir.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
        BUSTER_STRING_TEST(arguments, mismatched_delimiter_ir.diagnostics[0].message,
                           S8("in function 'main': function body has mismatched delimiters"));
    }
    scratch_end(mismatched_delimiter_temporary);

    CPreprocessResult declaration_tokens = c_preprocess(arguments->arena,
                                                        S8("typedef unsigned long Size;\n"
                                                           "extern int value;\n"
                                                           "static int add(int left, int right);\n"
                                                           "int main(int count, char **values)\n"
                                                           "{\n"
                                                           "    if (count) { return add(count, 1); }\n"
                                                           "    return values != 0;\n"
                                                           "}\n"),
                                                        (CPreprocessOptions){0});
    CParseResult declarations = c_parse(arguments->arena, declaration_tokens);
    BUSTER_TEST(arguments, declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, declarations.declaration_count == 4);
    BUSTER_TEST(arguments, declarations.entity_count == 8);
    BUSTER_TEST(arguments, declarations.scope_count == 4);
    BUSTER_TEST(arguments, declarations.scope_count == 4 && declarations.scopes[0].entity_count == 4);
    if (declarations.declaration_count == 4)
    {
        BUSTER_TEST(arguments, declarations.declarations[0].kind == C_DECLARATION_TYPEDEF);
        BUSTER_TEST(arguments, declarations.declarations[0].entity.value == 0);
        BUSTER_TEST(arguments, declarations.entities[0].kind == C_ENTITY_TYPEDEF);
        BUSTER_TEST(arguments, declarations.entities[0].declaration_index == 0);
        BUSTER_TEST(arguments, declarations.entities[0].scope.value == 0);
        BUSTER_STRING_TEST(arguments, declarations.declarations[0].name, S8("Size"));
        CType* size_type = c_type_from_id(&declarations, declarations.declarations[0].type);
        BUSTER_TEST(arguments, size_type && size_type->kind == C_TYPE_UNSIGNED_LONG);
        BUSTER_TEST(arguments, declarations.declarations[1].kind == C_DECLARATION_OBJECT);
        BUSTER_STRING_TEST(arguments, declarations.declarations[1].name, S8("value"));
        CType* value_type = c_type_from_id(&declarations, declarations.declarations[1].type);
        BUSTER_TEST(arguments, value_type && value_type->kind == C_TYPE_INT);
        BUSTER_TEST(arguments, declarations.declarations[2].kind == C_DECLARATION_FUNCTION);
        BUSTER_TEST(arguments, !declarations.declarations[2].is_definition);
        CType* add_type = c_type_from_id(&declarations, declarations.declarations[2].type);
        BUSTER_TEST(arguments, add_type && add_type->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, add_type && add_type->parameter_count == 2);
        BUSTER_TEST(arguments, declarations.declarations[2].parameter_count == 2);
        BUSTER_TEST(arguments, declarations.declarations[2].scope.value != C_ID_UNDERLYING_INVALID);
        if (add_type && add_type->parameter_count == 2)
        {
            CParameter left = declarations.parameters[add_type->parameter_start];
            CParameter right = declarations.parameters[add_type->parameter_start + 1];
            BUSTER_STRING_TEST(arguments, left.name, S8("left"));
            BUSTER_STRING_TEST(arguments, right.name, S8("right"));
            CType* left_type = c_type_from_id(&declarations, left.type);
            CType* right_type = c_type_from_id(&declarations, right.type);
            BUSTER_TEST(arguments, left_type && left_type->kind == C_TYPE_INT);
            BUSTER_TEST(arguments, right_type && right_type->kind == C_TYPE_INT);
            BUSTER_TEST(arguments, left.entity.value != C_ID_UNDERLYING_INVALID);
            BUSTER_TEST(arguments, right.entity.value != C_ID_UNDERLYING_INVALID);
            BUSTER_TEST(arguments, declarations.entities[left.entity.value].scope.value == declarations.declarations[2].scope.value);
        }
        BUSTER_STRING_TEST(arguments, declarations.declarations[3].name, S8("main"));
        BUSTER_TEST(arguments, declarations.declarations[3].is_definition);
        BUSTER_TEST(arguments, declarations.declarations[3].body_token_count != 0);
        CType* main_type = c_type_from_id(&declarations, declarations.declarations[3].type);
        BUSTER_TEST(arguments, main_type && main_type->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, main_type && main_type->parameter_count == 2);
        if (main_type && main_type->parameter_count == 2)
        {
            CParameter values = declarations.parameters[main_type->parameter_start + 1];
            CType* outer_pointer = c_type_from_id(&declarations, values.type);
            CType* inner_pointer = outer_pointer ? c_type_from_id(&declarations, outer_pointer->element_type) : 0;
            CType* character = inner_pointer ? c_type_from_id(&declarations, inner_pointer->element_type) : 0;
            BUSTER_STRING_TEST(arguments, values.name, S8("values"));
            BUSTER_TEST(arguments, outer_pointer && outer_pointer->kind == C_TYPE_POINTER);
            BUSTER_TEST(arguments, inner_pointer && inner_pointer->kind == C_TYPE_POINTER);
            BUSTER_TEST(arguments, character && character->kind == C_TYPE_CHAR);
        }
    }

    CPreprocessResult block_extern_cleanup_tokens = c_preprocess(arguments->arena,
                                                                 S8("extern int cleanup_file_callback(int *);\n"
                                                                    "int cleanup_callback(int *value) { return *value; }\n"
                                                                    "int main(void) {\n"
                                                                    "    extern int cleanup_callback(int *);\n"
                                                                    "    int value __attribute__((__cleanup__(cleanup_callback))) = 1;\n"
                                                                    "    int file_value __attribute__((cleanup(cleanup_file_callback))) = 2;\n"
                                                                    "    return value + file_value;\n"
                                                                    "}\n"),
                                                                 (CPreprocessOptions){
                                                                     .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                     .target = target_native,
                                                                     .data_layout = target_data_layout(target_native),
                                                                 });
    CParseResult block_extern_cleanup_parse = c_parse(arguments->arena, block_extern_cleanup_tokens);
    BUSTER_TEST(arguments, block_extern_cleanup_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, block_extern_cleanup_parse.diagnostic_count == 0);
    CEntityId block_extern_callback = c_test_find_local_entity(&block_extern_cleanup_parse, S8("cleanup_callback"), C_SCOPE_ID_INVALID);
    BUSTER_TEST(arguments, block_extern_callback.value != C_ID_UNDERLYING_INVALID);
    if (block_extern_callback.value != C_ID_UNDERLYING_INVALID)
    {
        CEntity* callback_entity = &block_extern_cleanup_parse.entities[block_extern_callback.value];
        CType* callback_type = c_type_from_id(&block_extern_cleanup_parse, callback_entity->type);
        BUSTER_TEST(arguments, callback_entity->scope.value != 0);
        BUSTER_TEST(arguments, callback_type && callback_type->kind == C_TYPE_FUNCTION);
    }
    CEntityId block_extern_value = c_test_find_local_entity(&block_extern_cleanup_parse, S8("value"), C_SCOPE_ID_INVALID);
    BUSTER_TEST(arguments, block_extern_value.value != C_ID_UNDERLYING_INVALID);
    if (block_extern_value.value != C_ID_UNDERLYING_INVALID && block_extern_callback.value != C_ID_UNDERLYING_INVALID)
    {
        CEntity* value_entity = &block_extern_cleanup_parse.entities[block_extern_value.value];
        BUSTER_TEST(arguments, value_entity->has_cleanup);
        BUSTER_TEST(arguments, value_entity->cleanup_function.value == block_extern_callback.value);
    }
    CEntityId file_value = c_test_find_local_entity(&block_extern_cleanup_parse, S8("file_value"), C_SCOPE_ID_INVALID);
    CEntityId file_callback = c_parse_lookup_entity(&block_extern_cleanup_parse, (CScopeId){.value = 0}, S8("cleanup_file_callback"));
    BUSTER_TEST(arguments, file_value.value != C_ID_UNDERLYING_INVALID);
    BUSTER_TEST(arguments, file_callback.value != C_ID_UNDERLYING_INVALID);
    if (file_value.value != C_ID_UNDERLYING_INVALID && file_callback.value != C_ID_UNDERLYING_INVALID)
    {
        CEntity* file_value_entity = &block_extern_cleanup_parse.entities[file_value.value];
        BUSTER_TEST(arguments, file_value_entity->has_cleanup);
        BUSTER_TEST(arguments, file_value_entity->cleanup_function.value == file_callback.value);
    }
    CIRLowerResult block_extern_cleanup_ir = c_lower_to_ir(arguments->arena, S8("block-extern-cleanup.c"), block_extern_cleanup_tokens,
                                                            block_extern_cleanup_parse, target_native);
    BUSTER_TEST(arguments, block_extern_cleanup_ir.diagnostic_count == 0);
    if (block_extern_cleanup_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(block_extern_cleanup_ir.program, &block_extern_cleanup_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult external_cleanup_tokens = c_preprocess(arguments->arena,
                                                             S8("int main(void) {\n"
                                                                "    extern int cleanup_external(int *);\n"
                                                                "    int value __attribute__((cleanup(cleanup_external))) = 1;\n"
                                                                "    return value;\n"
                                                                "}\n"),
                                                             (CPreprocessOptions){
                                                                 .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                 .target = target_native,
                                                                 .data_layout = target_data_layout(target_native),
                                                             });
    CParseResult external_cleanup_parse = c_parse(arguments->arena, external_cleanup_tokens);
    BUSTER_TEST(arguments, external_cleanup_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, external_cleanup_parse.diagnostic_count == 0);
    CIRLowerResult external_cleanup_ir = c_lower_to_ir(arguments->arena, S8("external-cleanup.c"), external_cleanup_tokens, external_cleanup_parse, target_native);
    BUSTER_TEST(arguments, external_cleanup_ir.diagnostic_count == 0);
    if (external_cleanup_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(external_cleanup_ir.program, &external_cleanup_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }

    CPreprocessResult cleanup_conversion_tokens = c_preprocess(arguments->arena,
                                                               S8("int cleanup_nonvoid(int *value) { return *value; }\n"
                                                                  "void cleanup_const_accept(const int *value) { (void)value; }\n"
                                                                  "void cleanup_void_accept(void *value) { (void)value; }\n"
                                                                  "void cleanup_variadic_accept(int *value, ...) { (void)value; }\n"
                                                                  "void cleanup_array_accept(int value[1]) { (void)value; }\n"
                                                                  "int main(void) {\n"
                                                                  "    int first __attribute__((cleanup(cleanup_nonvoid))) = 1;\n"
                                                                  "    int second __attribute__((cleanup(cleanup_const_accept))) = 2;\n"
                                                                  "    int third __attribute__((cleanup(cleanup_void_accept))) = 3;\n"
                                                                  "    int fourth __attribute__((cleanup(cleanup_variadic_accept))) = 4;\n"
                                                                  "    int fifth __attribute__((cleanup(cleanup_array_accept))) = 5;\n"
                                                                  "    return first + second + third + fourth;\n"
                                                                  "}\n"),
                                                               (CPreprocessOptions){
                                                                   .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                   .target = target_native,
                                                                   .data_layout = target_data_layout(target_native),
                                                               });
    CParseResult cleanup_conversion_parse = c_parse(arguments->arena, cleanup_conversion_tokens);
    BUSTER_TEST(arguments, cleanup_conversion_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, cleanup_conversion_parse.diagnostic_count == 0);
    CIRLowerResult cleanup_conversion_ir = c_lower_to_ir(arguments->arena, S8("cleanup-conversions.c"), cleanup_conversion_tokens,
                                                         cleanup_conversion_parse, target_native);
    BUSTER_TEST(arguments, cleanup_conversion_ir.diagnostic_count == 0);
    if (cleanup_conversion_ir.program)
    {
        IrFunction* main_function = 0;
        for (u32 function_index = 0; function_index < cleanup_conversion_ir.program->modules[0].function_count; function_index += 1)
        {
            IrFunction* candidate = &cleanup_conversion_ir.program->modules[0].functions[function_index];
            if (string_equal(candidate->name, S8("main")))
            {
                main_function = candidate;
                break;
            }
        }
        u32 cleanup_call_count = 0;
        if (main_function)
        {
            for (u32 instruction_index = 0; instruction_index < main_function->instruction_count; instruction_index += 1)
            {
                cleanup_call_count += main_function->instructions[instruction_index].opcode == IR_OPCODE_CALL;
            }
        }
        BUSTER_TEST(arguments, main_function != 0);
        BUSTER_TEST(arguments, cleanup_call_count == 5);
        BUSTER_TEST(arguments, ir_validate_canonical_module(cleanup_conversion_ir.program, &cleanup_conversion_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }

    CPreprocessResult dead_cleanup_tokens = c_preprocess(arguments->arena,
                                                         S8("static void dead_cleanup(int *value) { (void)value; }\n"
                                                            "static int dead_owner(void) {\n"
                                                            "    int value __attribute__((cleanup(dead_cleanup))) = 1;\n"
                                                            "    return value;\n"
                                                            "}\n"
                                                            "int main(void) { return 0; }\n"),
                                                         (CPreprocessOptions){
                                                             .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                             .target = target_native,
                                                             .data_layout = target_data_layout(target_native),
                                                         });
    CParseResult dead_cleanup_parse = c_parse(arguments->arena, dead_cleanup_tokens);
    BUSTER_TEST(arguments, dead_cleanup_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, dead_cleanup_parse.diagnostic_count == 0);
    CIRLowerResult dead_cleanup_ir = c_lower_to_ir(arguments->arena, S8("dead-cleanup.c"), dead_cleanup_tokens, dead_cleanup_parse, target_native);
    BUSTER_TEST(arguments, dead_cleanup_ir.diagnostic_count == 0);
    if (dead_cleanup_ir.program)
    {
        bool dead_owner_emitted = false;
        bool dead_callback_emitted = false;
        IrModule* dead_module = &dead_cleanup_ir.program->modules[0];
        for (u32 function_index = 0; function_index < dead_module->function_count; function_index += 1)
        {
            IrFunction* function = &dead_module->functions[function_index];
            dead_owner_emitted |= string_equal(function->name, S8("dead_owner"));
            dead_callback_emitted |= string_equal(function->name, S8("dead_cleanup"));
        }
        BUSTER_TEST(arguments, !dead_owner_emitted);
        BUSTER_TEST(arguments, !dead_callback_emitted);
        BUSTER_TEST(arguments, ir_validate_canonical_module(dead_cleanup_ir.program, dead_module).error == IR_VALIDATION_NONE);
    }

    c_test_cleanup_diagnostic(arguments, &result,
                              S8("void cleanup_strict(int *value) { (void)value; }\n"
                                 "int main(void) { int value __attribute__((cleanup(cleanup_strict))) = 1; return value; }\n"),
                              C_PREPROCESS_DIALECT_C17, S8("GNU cleanup attribute is only available in GNU dialects"));
    c_test_cleanup_diagnostic(arguments, &result,
                              S8("void cleanup_placement(int *value) { (void)value; }\n"
                                 "int value __attribute__((cleanup(cleanup_placement)));\n"),
                              C_PREPROCESS_DIALECT_GNU23, S8("GNU cleanup attribute may only be applied to an automatic block-scope object"));
    // The same placement diagnostic reached through a spelling the intern
    // pass never saw: a pasted token carries symbol 0, so this is what holds
    // c_token_in_well_known_set's spelling fallback honest for the attribute
    // keywords.
    c_test_cleanup_diagnostic(arguments, &result,
                              S8("#define C_TEST_PASTE(first, second) first##second\n"
                                 "void cleanup_pasted(int *value) { (void)value; }\n"
                                 "int value C_TEST_PASTE(__attri, bute__)((cleanup(cleanup_pasted)));\n"),
                              C_PREPROCESS_DIALECT_GNU23, S8("GNU cleanup attribute may only be applied to an automatic block-scope object"));
    c_test_cleanup_diagnostic(arguments, &result,
                              S8("void cleanup_arity(int *value, int extra) { (void)value; (void)extra; }\n"
                                 "int main(void) { int value __attribute__((cleanup(cleanup_arity))) = 1; return value; }\n"),
                              C_PREPROCESS_DIALECT_GNU23, S8("cleanup function must take exactly one parameter"));
    c_test_cleanup_diagnostic(arguments,
                              &result,
                              S8("void cleanup_pointer(float *value) { (void)value; }\n"
                                 "int main(void) { int value __attribute__((cleanup(cleanup_pointer))) = 1; return value; }\n"),
                              C_PREPROCESS_DIALECT_GNU23, S8("cleanup function parameter must be a pointer to the declared variable type"));
    c_test_cleanup_diagnostic(arguments,
                              &result,
                              S8("void cleanup_malformed(int *value) { (void)value; }\n"
                                 "int main(void) { int value __attribute__((cleanup())) = 1; return value; }\n"),
                              C_PREPROCESS_DIALECT_GNU23, S8("cleanup attribute requires exactly one function argument"));
    c_test_cleanup_diagnostic(arguments,
                              &result,
                              S8("void cleanup_multiple(int *value) { (void)value; }\n"
                                 "int main(void) { int value __attribute__((cleanup(cleanup_multiple))) __attribute__((cleanup(cleanup_multiple))) = 1; return value; }\n"),
                              C_PREPROCESS_DIALECT_GNU23, S8("cleanup attribute requires exactly one function argument"));
    c_test_cleanup_diagnostic(arguments,
                              &result,
                              S8("int main(void) {\n"
                                 "    int value __attribute__((cleanup(cleanup_late))) = 1;\n"
                                 "    void cleanup_late(int *);\n"
                                 "    return value;\n"
                                 "}\n"),
                              C_PREPROCESS_DIALECT_GNU23, S8("cleanup attribute argument must name a function"));
    CPreprocessResult shadow_cleanup_tokens = c_preprocess(arguments->arena,
                                                            S8("extern void cleanup_shadow(int *);\n"
                                                               "int main(void) {\n"
                                                               "    int value __attribute__((cleanup(cleanup_shadow))) = 1;\n"
                                                               "    extern void cleanup_shadow(int *);\n"
                                                               "    return value;\n"
                                                               "}\n"),
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                .target = target_native,
                                                                .data_layout = target_data_layout(target_native),
                                                            });
    CParseResult shadow_cleanup_parse = c_parse(arguments->arena, shadow_cleanup_tokens);
    CEntityId shadow_value = c_test_find_local_entity(&shadow_cleanup_parse, S8("value"), C_SCOPE_ID_INVALID);
    CEntityId shadow_callback = c_parse_lookup_entity(&shadow_cleanup_parse, (CScopeId){.value = 0}, S8("cleanup_shadow"));
    BUSTER_TEST(arguments, shadow_cleanup_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, shadow_cleanup_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, shadow_value.value != C_ID_UNDERLYING_INVALID);
    BUSTER_TEST(arguments, shadow_callback.value != C_ID_UNDERLYING_INVALID);
    if (shadow_value.value != C_ID_UNDERLYING_INVALID && shadow_callback.value != C_ID_UNDERLYING_INVALID)
    {
        BUSTER_TEST(arguments, shadow_cleanup_parse.entities[shadow_value.value].has_cleanup);
        BUSTER_TEST(arguments, shadow_cleanup_parse.entities[shadow_value.value].cleanup_function.value == shadow_callback.value);
    }

    CPreprocessResult array_declaration_tokens = c_preprocess(arguments->arena,
                                                              S8("int matrix[2][3];\n"
                                                                 "int sum(int values[static 4], "
                                                                 "int table[][3]);\n"),
                                                              (CPreprocessOptions){0});
    CParseResult array_declarations = c_parse(arguments->arena, array_declaration_tokens);
    BUSTER_TEST(arguments, array_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, array_declarations.declaration_count == 2);
    if (array_declarations.declaration_count == 2)
    {
        CType* matrix_outer = c_type_from_id(&array_declarations, array_declarations.declarations[0].type);
        CType* matrix_inner = matrix_outer ? c_type_from_id(&array_declarations, matrix_outer->element_type) : 0;
        CType* matrix_element = matrix_inner ? c_type_from_id(&array_declarations, matrix_inner->element_type) : 0;
        BUSTER_TEST(arguments, matrix_outer && matrix_outer->kind == C_TYPE_ARRAY);
        BUSTER_TEST(arguments, matrix_inner && matrix_inner->kind == C_TYPE_ARRAY);
        BUSTER_TEST(arguments, matrix_element && matrix_element->kind == C_TYPE_INT);
        if (matrix_outer && matrix_inner)
        {
            CArrayBound outer_bound = array_declarations.array_bounds[matrix_outer->array_bound];
            CArrayBound inner_bound = array_declarations.array_bounds[matrix_inner->array_bound];
            BUSTER_TEST(arguments, outer_bound.token_count == 1);
            BUSTER_TEST(arguments, inner_bound.token_count == 1);
            BUSTER_STRING_TEST(arguments, c_token_spelling(array_declaration_tokens.spelling_base, array_declaration_tokens.tokens[outer_bound.token_start]), S8("2"));
            BUSTER_STRING_TEST(arguments, c_token_spelling(array_declaration_tokens.spelling_base, array_declaration_tokens.tokens[inner_bound.token_start]), S8("3"));
        }
        CType* sum_type = c_type_from_id(&array_declarations, array_declarations.declarations[1].type);
        BUSTER_TEST(arguments, sum_type && sum_type->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, sum_type && sum_type->parameter_count == 2);
        if (sum_type && sum_type->parameter_count == 2)
        {
            CType* values_type = c_type_from_id(&array_declarations, array_declarations.parameters[sum_type->parameter_start].type);
            BUSTER_TEST(arguments, values_type && values_type->kind == C_TYPE_ARRAY);
            BUSTER_TEST(arguments, values_type && array_declarations.array_bounds[values_type->array_bound].is_static);
            CType* table_outer = c_type_from_id(&array_declarations, array_declarations.parameters[sum_type->parameter_start + 1].type);
            CType* table_inner = table_outer ? c_type_from_id(&array_declarations, table_outer->element_type) : 0;
            BUSTER_TEST(arguments, table_outer && table_outer->kind == C_TYPE_ARRAY);
            BUSTER_TEST(arguments, table_inner && table_inner->kind == C_TYPE_ARRAY);
            BUSTER_TEST(arguments, table_outer && array_declarations.array_bounds[table_outer->array_bound].token_count == 0);
        }
    }

    CPreprocessResult callback_tokens = c_preprocess(arguments->arena,
                                                     S8("typedef int (*Callback)"
                                                        "(int value, void *context);\n"
                                                        "int (*rows)[4];\n"),
                                                     (CPreprocessOptions){0});
    CParseResult callback_declarations = c_parse(arguments->arena, callback_tokens);
    BUSTER_TEST(arguments, callback_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, callback_declarations.declaration_count == 2);
    if (callback_declarations.declaration_count == 2)
    {
        BUSTER_STRING_TEST(arguments, callback_declarations.declarations[0].name, S8("Callback"));
        BUSTER_TEST(arguments, callback_declarations.declarations[0].kind == C_DECLARATION_TYPEDEF);
        CType* callback_pointer = c_type_from_id(&callback_declarations, callback_declarations.declarations[0].type);
        CType* callback_function = callback_pointer ? c_type_from_id(&callback_declarations, callback_pointer->element_type) : 0;
        BUSTER_TEST(arguments, callback_pointer && callback_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, callback_function && callback_function->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, callback_function && callback_function->parameter_count == 2);
        if (callback_function && callback_function->parameter_count == 2)
        {
            CType* context_pointer = c_type_from_id(&callback_declarations, callback_declarations.parameters[callback_function->parameter_start + 1].type);
            CType* context_element = context_pointer ? c_type_from_id(&callback_declarations, context_pointer->element_type) : 0;
            BUSTER_TEST(arguments, context_pointer && context_pointer->kind == C_TYPE_POINTER);
            BUSTER_TEST(arguments, context_element && context_element->kind == C_TYPE_VOID);
        }
        BUSTER_STRING_TEST(arguments, callback_declarations.declarations[1].name, S8("rows"));
        CType* rows_pointer = c_type_from_id(&callback_declarations, callback_declarations.declarations[1].type);
        CType* rows_array = rows_pointer ? c_type_from_id(&callback_declarations, rows_pointer->element_type) : 0;
        CType* rows_element = rows_array ? c_type_from_id(&callback_declarations, rows_array->element_type) : 0;
        BUSTER_TEST(arguments, rows_pointer && rows_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, rows_array && rows_array->kind == C_TYPE_ARRAY);
        BUSTER_TEST(arguments, rows_element && rows_element->kind == C_TYPE_INT);
    }

    CPreprocessResult typedef_name_callback_tokens = c_preprocess(arguments->arena,
                                                                  S8("typedef int CallbackResult;\n"
                                                                     "typedef CallbackResult (*AliasCallback)(int value);\n"
                                                                     "AliasCallback callback;\n"),
                                                                  (CPreprocessOptions){0});
    CParserResult typedef_name_callback_syntax = c_parse_ast(arguments->arena, typedef_name_callback_tokens);
    BUSTER_TEST(arguments, typedef_name_callback_syntax.diagnostic_count == 0);
    BUSTER_TEST(arguments, typedef_name_callback_syntax.declaration_count == 3);
    if (typedef_name_callback_syntax.declaration_count == 3)
    {
        CParserDeclaration* alias_callback_syntax = typedef_name_callback_syntax.first_declaration;
        alias_callback_syntax = alias_callback_syntax ? alias_callback_syntax->next : 0;
        BUSTER_TEST(arguments, alias_callback_syntax && alias_callback_syntax->name_token < typedef_name_callback_tokens.token_count);
        if (alias_callback_syntax && alias_callback_syntax->name_token < typedef_name_callback_tokens.token_count)
        {
            BUSTER_STRING_TEST(arguments, c_token_spelling(typedef_name_callback_tokens.spelling_base, typedef_name_callback_tokens.tokens[alias_callback_syntax->name_token]), S8("AliasCallback"));
        }
    }
    CParseResult typedef_name_callback_declarations = c_parse(arguments->arena, typedef_name_callback_tokens);
    BUSTER_TEST(arguments, typedef_name_callback_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, typedef_name_callback_declarations.declaration_count == 3);
    if (typedef_name_callback_declarations.declaration_count == 3)
    {
        BUSTER_STRING_TEST(arguments, typedef_name_callback_declarations.declarations[1].name, S8("AliasCallback"));
        BUSTER_TEST(arguments, typedef_name_callback_declarations.declarations[1].kind == C_DECLARATION_TYPEDEF);
        CType* alias_callback_pointer = c_type_from_id(&typedef_name_callback_declarations, typedef_name_callback_declarations.declarations[1].type);
        CType* alias_callback_function = alias_callback_pointer ? c_type_from_id(&typedef_name_callback_declarations, alias_callback_pointer->element_type) : 0;
        BUSTER_TEST(arguments, alias_callback_pointer && alias_callback_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, alias_callback_function && alias_callback_function->kind == C_TYPE_FUNCTION);
        BUSTER_STRING_TEST(arguments, typedef_name_callback_declarations.declarations[2].name, S8("callback"));
    }

    CPreprocessResult qualified_callback_tokens = c_preprocess(arguments->arena,
                                                               S8("typedef struct QualifiedContext"
                                                                  " { int value; } QualifiedContext;\n"
                                                                  "typedef void *"
                                                                  "(*PointerReturningCallback)"
                                                                  "(void *context);\n"
                                                                  "int qualified_parameters("
                                                                  "const QualifiedContext *left,"
                                                                  " QualifiedContext const *right);\n"),
                                                               (CPreprocessOptions){0});
    CParseResult qualified_callback_declarations = c_parse(arguments->arena, qualified_callback_tokens);
    BUSTER_TEST(arguments, qualified_callback_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, qualified_callback_declarations.declaration_count == 3);
    if (qualified_callback_declarations.declaration_count == 3)
    {
        CType* callback_pointer = c_type_from_id(&qualified_callback_declarations, qualified_callback_declarations.declarations[1].type);
        CType* callback_function = callback_pointer ? c_type_from_id(&qualified_callback_declarations, callback_pointer->element_type) : 0;
        CType* callback_return = callback_function ? c_type_from_id(&qualified_callback_declarations, callback_function->return_type) : 0;
        CType* callback_return_element = callback_return ? c_type_from_id(&qualified_callback_declarations, callback_return->element_type) : 0;
        BUSTER_TEST(arguments, callback_pointer && callback_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, callback_function && callback_function->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, callback_return && callback_return->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, callback_return_element && callback_return_element->kind == C_TYPE_VOID);

        CType* qualified_function = c_type_from_id(&qualified_callback_declarations, qualified_callback_declarations.declarations[2].type);
        BUSTER_TEST(arguments, qualified_function && qualified_function->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, qualified_function && qualified_function->parameter_count == 2);
        if (qualified_function && qualified_function->parameter_count == 2)
        {
            for (u32 parameter_index = 0; parameter_index < 2; parameter_index += 1)
            {
                CType* pointer = c_type_from_id(&qualified_callback_declarations,
                                                qualified_callback_declarations.parameters[qualified_function->parameter_start + parameter_index].type);
                CType* element = pointer ? c_type_from_id(&qualified_callback_declarations, pointer->element_type) : 0;
                BUSTER_TEST(arguments, pointer && pointer->kind == C_TYPE_POINTER);
                BUSTER_TEST(arguments, element && element->kind == C_TYPE_STRUCT && element->is_const);
            }
        }
    }

    CPreprocessResult redeclaration_tokens = c_preprocess(arguments->arena,
                                                          S8("int add(int, int);\n"
                                                             "int add(int left, int right)"
                                                             " { return left + right; }\n"),
                                                          (CPreprocessOptions){0});
    CParseResult redeclarations = c_parse(arguments->arena, redeclaration_tokens);
    BUSTER_TEST(arguments, redeclarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, redeclarations.declaration_count == 2);
    BUSTER_TEST(arguments, redeclarations.scopes[0].entity_count == 1);
    BUSTER_TEST(arguments, redeclarations.declaration_count == 2 && redeclarations.declarations[0].entity.value == redeclarations.declarations[1].entity.value);
    BUSTER_TEST(arguments, redeclarations.scopes[0].entity_count == 1 && redeclarations.entities[0].is_definition);

    CPreprocessResult conflicting_tokens = c_preprocess(arguments->arena, S8("int value;\nlong value;\n"), (CPreprocessOptions){0});
    CParseResult conflicting = c_parse(arguments->arena, conflicting_tokens);
    BUSTER_TEST(arguments, conflicting.diagnostic_count == 1);
    BUSTER_TEST(arguments, conflicting.diagnostic_count == 1 && conflicting.diagnostics[0].kind == C_DIAGNOSTIC_CONFLICTING_DECLARATION);
    BUSTER_TEST(arguments, conflicting.entity_count == 1);

    CPreprocessResult overload_tokens = c_preprocess(arguments->arena,
                                                     S8("int select_value(int value)"
                                                        " __asm__(\"select_signed\");\n"
                                                        "int select_value(unsigned value)"
                                                        " __attribute__((__overloadable__))"
                                                        " __asm__(\"select_unsigned\");\n"
                                                        "int select_both(void) {\n"
                                                        "    return select_value(1) +"
                                                        " select_value(1U);\n"
                                                        "}\n"),
                                                     (CPreprocessOptions){0});
    CParseResult overload_parse = c_parse(arguments->arena, overload_tokens);
    BUSTER_TEST(arguments, overload_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, overload_parse.declaration_count == 3);
    BUSTER_TEST(arguments, overload_parse.entity_count >= 3);
    if (overload_parse.declaration_count == 3)
    {
        BUSTER_TEST(arguments, overload_parse.declarations[0].entity.value != overload_parse.declarations[1].entity.value);
    }
    CIRLowerResult overload_ir = c_lower_to_ir(arguments->arena, S8("overload.c"), overload_tokens, overload_parse, target_native);
    BUSTER_TEST(arguments, overload_ir.diagnostic_count == 0);
    if (overload_ir.program)
    {
        bool found_signed_call = false;
        bool found_unsigned_call = false;
        IrModule* module = &overload_ir.program->modules[0];
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction instruction = function->instructions[instruction_index];
                if (instruction.opcode != IR_OPCODE_CALL)
                {
                    continue;
                }
                IrSymbol* symbol = ir_symbol_from_id(&overload_ir.program->symbols, instruction.symbol);
                found_signed_call |= symbol && string_equal(symbol->link_name, S8("select_signed"));
                found_unsigned_call |= symbol && string_equal(symbol->link_name, S8("select_unsigned"));
            }
        }
        BUSTER_TEST(arguments, found_signed_call);
        BUSTER_TEST(arguments, found_unsigned_call);
        IrValidationResult validation = ir_validate_canonical_module(overload_ir.program, module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }

    CPreprocessResult redefinition_tokens = c_preprocess(arguments->arena,
                                                         S8("int value = 1;\n"
                                                            "int value = 2;\n"),
                                                         (CPreprocessOptions){0});
    CParseResult redefinition = c_parse(arguments->arena, redefinition_tokens);
    BUSTER_TEST(arguments, redefinition.diagnostic_count == 1);
    BUSTER_TEST(arguments, redefinition.diagnostic_count == 1 && redefinition.diagnostics[0].kind == C_DIAGNOSTIC_REDEFINITION);
    BUSTER_TEST(arguments, redefinition.entity_count == 1);

    CPreprocessResult ir_tokens = c_preprocess(arguments->arena, S8("int main(void) { return 1 + 2 * 3; }\n"), (CPreprocessOptions){0});
    CParseResult ir_parse = c_parse(arguments->arena, ir_tokens);
    CIRLowerResult c_ir = c_lower_to_ir(arguments->arena, S8("test.c"), ir_tokens, ir_parse, target_native);
    BUSTER_TEST(arguments, c_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, c_ir.program != 0);
    if (c_ir.program)
    {
        BUSTER_TEST(arguments, c_ir.program->module_count == 1);
        BUSTER_TEST(arguments, c_ir.program->types.count >= C_TYPE_LONG_DOUBLE);
        BUSTER_TEST(arguments, c_ir.program->symbols.count == 1);
        BUSTER_TEST(arguments, c_ir.program->modules[0].function_count == 1);
        IrFunction* function = c_ir.program->modules[0].functions;
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, function->block_count == 1);
        BUSTER_TEST(arguments, function->instruction_count == 6);
        BUSTER_TEST(arguments, function->instructions[0].opcode == IR_OPCODE_CONSTANT_INTEGER);
        BUSTER_TEST(arguments, function->instructions[0].immediates[0] == 1);
        BUSTER_TEST(arguments, function->instructions[3].opcode == IR_OPCODE_BINARY);
        BUSTER_TEST(arguments, function->instructions[3].binary_operation == IR_BINARY_INTEGER_MULTIPLY);
        BUSTER_TEST(arguments, function->instructions[4].binary_operation == IR_BINARY_INTEGER_ADD);
        BUSTER_TEST(arguments, function->instructions[5].opcode == IR_OPCODE_RETURN);
        BUSTER_TEST(arguments, function->values[0].canonical_type.value != IR_ID_UNDERLYING_INVALID);
        IrValidationResult validation = ir_validate_canonical_module(c_ir.program, &c_ir.program->modules[0]);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
        // The canonical validator owes codegen the same ownership proof as the
        // analysis-backed one: codegen walks these chains with no guard of its
        // own and indexes the dense array by what they hand it.
        IrBlock* block = function->blocks;
        IrInstructionId saved_next = function->instructions[block->last_instruction.value].next;
        function->instructions[block->last_instruction.value].next = block->first_instruction;
        BUSTER_TEST(arguments, ir_validate_canonical_module(c_ir.program, &c_ir.program->modules[0]).error == IR_VALIDATION_INSTRUCTION_OWNERSHIP);
        function->instructions[block->last_instruction.value].next = saved_next;

        IrInstructionId saved_first = block->first_instruction;
        block->first_instruction = function->instructions[saved_first.value].next;
        IrValidationResult unowned = ir_validate_canonical_module(c_ir.program, &c_ir.program->modules[0]);
        BUSTER_TEST(arguments, unowned.error == IR_VALIDATION_INSTRUCTION_OWNERSHIP);
        BUSTER_TEST(arguments, unowned.instruction.value == saved_first.value);
        block->first_instruction = saved_first;

        IrInstructionId saved_last = block->last_instruction;
        block->last_instruction = block->first_instruction;
        BUSTER_TEST(arguments, ir_validate_canonical_module(c_ir.program, &c_ir.program->modules[0]).error == IR_VALIDATION_INSTRUCTION_OWNERSHIP);
        block->last_instruction = saved_last;
        BUSTER_TEST(arguments, ir_validate_canonical_module(c_ir.program, &c_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult nullability_tokens = c_preprocess(arguments->arena,
                                                        S8("void *select_pointer("
                                                           "const void * _Nonnull first,"
                                                           " void * _Nullable second,"
                                                           " void * _Null_unspecified third,"
                                                           " int (*_Null_unspecified callback)"
                                                           "(void * _Nonnull,"
                                                           " char * _Nullable)) {\n"
                                                           "    return second ? second :"
                                                           " (third ? third : (void *)first);\n"
                                                           "}\n"),
                                                        (CPreprocessOptions){0});
    CParseResult nullability_parse = c_parse(arguments->arena, nullability_tokens);
    BUSTER_TEST(arguments, nullability_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, nullability_parse.declaration_count == 1);
    BUSTER_TEST(arguments, nullability_parse.parameter_count == 6);
    if (nullability_parse.parameter_count == 6)
    {
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[0].name, S8("first"));
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[1].name, S8("second"));
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[2].name, S8("third"));
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[3].name, S8("callback"));
    }
    CIRLowerResult nullability_ir = c_lower_to_ir(arguments->arena, S8("nullability.c"), nullability_tokens, nullability_parse, target_native);
    BUSTER_TEST(arguments, nullability_ir.diagnostic_count == 0);
    if (nullability_ir.program)
    {
        IrValidationResult validation = ir_validate_canonical_module(nullability_ir.program, &nullability_ir.program->modules[0]);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    CPreprocessResult attributed_enum_tokens = c_preprocess(arguments->arena,
                                                            S8("enum NativeStrategy {\n"
                                                               "    NATIVE_SEAMLESS = 0,\n"
                                                               "    NATIVE_ALWAYS = 1\n"
                                                               "} __attribute__((availability("
                                                               "android, strict, introduced = 31)));\n"
                                                               "static inline int clear_rate(void) {\n"
                                                               "    return NATIVE_SEAMLESS;\n"
                                                               "}\n"),
                                                            (CPreprocessOptions){0});
    CParseResult attributed_enum_parse = c_parse(arguments->arena, attributed_enum_tokens);
    BUSTER_TEST(arguments, attributed_enum_parse.diagnostic_count == 0);
    CEntityId attributed_enumerator = c_parse_lookup_entity(&attributed_enum_parse, (CScopeId){.value = 0}, S8("NATIVE_SEAMLESS"));
    BUSTER_TEST(arguments, attributed_enumerator.value != C_ID_UNDERLYING_INVALID);
    if (attributed_enumerator.value != C_ID_UNDERLYING_INVALID)
    {
        BUSTER_TEST(arguments, attributed_enum_parse.entities[attributed_enumerator.value].kind == C_ENTITY_ENUMERATOR);
    }
    CPreprocessResult aggregate_local_tokens = c_preprocess(arguments->arena,
                                                            S8("typedef unsigned long u64;\n"
                                                               "typedef unsigned char u8;\n"
                                                               "struct Inner { u64 x; };\n"
                                                               "struct Outer {"
                                                               " u64 first;"
                                                               " struct Inner inner;"
                                                               " u8 reserved[4];"
                                                               "};\n"
                                                               "struct View {"
                                                               " u8 *pointer;"
                                                               " u64 length;"
                                                               "};\n"
                                                               "int fill(struct Outer *out,"
                                                               " _Bool offset) {\n"
                                                               " u8 local[3];\n"
                                                               " _Static_assert("
                                                               "sizeof(local) == 3, \"array\");\n"
                                                               " struct Outer value ="
                                                               " { .first = 7 };\n"
                                                               " struct View view = {"
                                                               " (u8 *)out + offset, 1 };\n"
                                                               " *out = value;\n"
                                                               " return (int)("
                                                               "sizeof(out->inner.x) +"
                                                               " sizeof(local[0]) +"
                                                               " (view.pointer != (u8 *)out));\n"
                                                               "}\n"),
                                                            (CPreprocessOptions){0});
    CParseResult aggregate_local_parse = c_parse(arguments->arena, aggregate_local_tokens);
    CIRLowerResult aggregate_local_ir = c_lower_to_ir(arguments->arena, S8("aggregate-local.c"), aggregate_local_tokens, aggregate_local_parse, target_native);
    BUSTER_TEST(arguments, aggregate_local_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_local_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_local_ir.program != 0);
    if (aggregate_local_ir.program)
    {
        IrModule* aggregate_local_module = &aggregate_local_ir.program->modules[0];
        BUSTER_TEST(arguments, aggregate_local_module->function_count == 1);
        IrFunction* fill = aggregate_local_module->functions;
        BUSTER_TEST(arguments, fill->state == IR_FUNCTION_LOWERED);
        u32 aggregate_count = 0;
        u32 store_count = 0;
        for (u32 instruction_index = 0; instruction_index < fill->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = fill->instructions[instruction_index].opcode;
            aggregate_count += opcode == IR_OPCODE_AGGREGATE || opcode == IR_OPCODE_ARRAY;
            store_count += opcode == IR_OPCODE_STORE;
        }
        BUSTER_TEST(arguments, aggregate_count >= 3);
        BUSTER_TEST(arguments, store_count >= 2);
        IrValidationResult validation = ir_validate_canonical_module(aggregate_local_ir.program, aggregate_local_module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    CPreprocessResult multi_for_tokens = c_preprocess(arguments->arena,
                                                      S8("int sum(void) {\n"
                                                         " int total = 0;\n"
                                                         " for (unsigned long i = 0,"
                                                         " reverse_i = 5;"
                                                         " i < reverse_i;"
                                                         " i += 1, reverse_i -= 1) {\n"
                                                         "  total += (int)(i + reverse_i);\n"
                                                         " }\n"
                                                         " return total;\n"
                                                         "}\n"),
                                                      (CPreprocessOptions){0});
    CParseResult multi_for_parse = c_parse(arguments->arena, multi_for_tokens);
    CIRLowerResult multi_for_ir = c_lower_to_ir(arguments->arena, S8("multi-for.c"), multi_for_tokens, multi_for_parse, target_native);
    BUSTER_TEST(arguments, multi_for_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, multi_for_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, multi_for_ir.program != 0);
    if (multi_for_ir.program)
    {
        IrModule* multi_for_module = &multi_for_ir.program->modules[0];
        IrFunction* sum = multi_for_module->functions;
        BUSTER_TEST(arguments, multi_for_module->function_count == 1);
        BUSTER_TEST(arguments, sum->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, sum->local_count == 3);
        BUSTER_TEST(arguments, sum->block_count >= 4);
        BUSTER_TEST(arguments, ir_validate_canonical_module(multi_for_ir.program, multi_for_module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult math_builtin_tokens = c_preprocess(arguments->arena,
                                                         S8("float lower(float value) {\n"
                                                            " return __builtin_floorf(value);\n"
                                                            "}\n"
                                                            "double power(double left,"
                                                            " double right) {\n"
                                                            " return __builtin_pow(left, right);\n"
                                                            "}\n"
                                                            "double huge(void) { return __builtin_huge_val(); }\n"),
                                                         (CPreprocessOptions){0});
    CParseResult math_builtin_parse = c_parse(arguments->arena, math_builtin_tokens);
    CIRLowerResult math_builtin_ir = c_lower_to_ir(arguments->arena, S8("math-builtins.c"), math_builtin_tokens, math_builtin_parse, target_native);
    BUSTER_TEST(arguments, math_builtin_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_builtin_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_builtin_ir.program != 0);
    if (math_builtin_ir.program)
    {
        IrModule* math_module = &math_builtin_ir.program->modules[0];
        u32 call_count = 0;
        bool found_huge_constant = false;
        for (u32 function_index = 0; function_index < math_module->function_count; function_index += 1)
        {
            IrFunction* function = &math_module->functions[function_index];
            BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                call_count += instruction->opcode == IR_OPCODE_CALL;
                found_huge_constant |= instruction->opcode == IR_OPCODE_CONSTANT_FLOAT && instruction->immediate_count == 1 && instruction->immediates &&
                                       instruction->immediates[0] == UINT64_C(0x7ff0000000000000);
            }
        }
        bool found_floorf = false;
        bool found_pow = false;
        for (u32 symbol_index = 0; symbol_index < math_builtin_ir.program->symbols.count; symbol_index += 1)
        {
            IrSymbol* symbol = &math_builtin_ir.program->symbols.symbols[symbol_index];
            if (symbol->kind != IR_SYMBOL_FUNCTION || symbol->linkage != IR_LINKAGE_IMPORT)
            {
                continue;
            }
            found_floorf |= string_equal(symbol->link_name, S8("floorf"));
            found_pow |= string_equal(symbol->link_name, S8("pow"));
        }
        BUSTER_TEST(arguments, call_count == 2);
        BUSTER_TEST(arguments, found_huge_constant);
        BUSTER_TEST(arguments, found_floorf);
        BUSTER_TEST(arguments, found_pow);
        BUSTER_TEST(arguments, ir_validate_canonical_module(math_builtin_ir.program, math_module).error == IR_VALIDATION_NONE);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_typedef_fallback_lookup(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(
        temporary.arena,
        S8("int TypeName;\n"
           "int typedef_fallback_scope(void) {\n"
           "    typedef long TypeName;\n"
           "    {\n"
           "        typedef short TypeName;\n"
           "        TypeName const value;\n"
           "        return sizeof(value);\n"
           "    }\n"
           "}\n"),
        (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    CEntityId value_id = C_ENTITY_ID_INVALID;
    CEntityId oldest_typedef = C_ENTITY_ID_INVALID;
    CEntityId newest_typedef = C_ENTITY_ID_INVALID;
    for (u32 entity_index = 0; entity_index < parse.entity_count; entity_index += 1)
    {
        CEntity* entity = &parse.entities[entity_index];
        if (entity->kind == C_ENTITY_LOCAL && string_equal(entity->name, S8("value")))
        {
            value_id = (CEntityId){.value = entity_index};
        }
        if (entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, S8("TypeName")))
        {
            if (oldest_typedef.value == C_ID_UNDERLYING_INVALID)
            {
                oldest_typedef = (CEntityId){.value = entity_index};
            }
            newest_typedef = (CEntityId){.value = entity_index};
        }
    }
    BUSTER_TEST(arguments, value_id.value != C_ID_UNDERLYING_INVALID);
    BUSTER_TEST(arguments, oldest_typedef.value != C_ID_UNDERLYING_INVALID);
    BUSTER_TEST(arguments, newest_typedef.value != C_ID_UNDERLYING_INVALID);
    BUSTER_TEST(arguments, oldest_typedef.value != newest_typedef.value);
    CEntityId fallback_typedef = c_parse_lookup_typedef_name_fallback(&parse, S8("TypeName"));
    BUSTER_TEST(arguments, fallback_typedef.value == oldest_typedef.value);
    if (value_id.value < parse.entity_count)
    {
        // The declaration resolves in its own block, so the inner short
        // typedef wins over both the enclosing long one and the file-scope
        // object of the same name.  The fallback below still answers with the
        // oldest typedef, which is what this test pins; it is only consulted
        // when the scoped lookup finds nothing at all.
        CType* value_type = c_type_from_id(&parse, parse.entities[value_id.value].type);
        BUSTER_TEST(arguments, value_type && value_type->kind == C_TYPE_SHORT && value_type->is_const);
        CEntityId scoped_name = c_parse_lookup_entity(&parse, parse.entities[value_id.value].scope, S8("TypeName"));
        BUSTER_TEST(arguments, scoped_name.value == newest_typedef.value);
        if (scoped_name.value < parse.entity_count)
        {
            CType* scoped_type = c_type_from_id(&parse, parse.entities[scoped_name.value].type);
            BUSTER_TEST(arguments, scoped_type && scoped_type->kind == C_TYPE_SHORT);
        }
    }

    // A partial result may have no typedef buckets at all. The fallback must
    // retain the old ascending entity walk rather than probing a null table.
    CEntityId* saved_typedef_buckets = parse.typedef_lookup_buckets;
    u32 saved_bucket_count = parse.entity_lookup_bucket_count;
    parse.typedef_lookup_buckets = 0;
    BUSTER_TEST(arguments, c_parse_lookup_typedef_name_fallback(&parse, S8("TypeName")).value == oldest_typedef.value);
    parse.typedef_lookup_buckets = saved_typedef_buckets;
    parse.entity_lookup_bucket_count = 0;
    BUSTER_TEST(arguments, c_parse_lookup_typedef_name_fallback(&parse, S8("TypeName")).value == oldest_typedef.value);
    parse.entity_lookup_bucket_count = saved_bucket_count;

    if (saved_typedef_buckets && saved_bucket_count)
    {
        u32 type_name_bucket = (u32)c_parse_name_hash(c_parse_name_symbol(&parse, S8("TypeName")), S8("TypeName")) & (saved_bucket_count - 1);
        CEntityId saved_bucket_head = saved_typedef_buckets[type_name_bucket];
        saved_typedef_buckets[type_name_bucket] = (CEntityId){.value = parse.entity_count};
        // An out-of-range chain id is malformed, so the helper must discard
        // any partial result and recover the oldest exact typedef by scan.
        BUSTER_TEST(arguments, c_parse_lookup_typedef_name_fallback(&parse, S8("TypeName")).value == oldest_typedef.value);
        saved_typedef_buckets[type_name_bucket] = saved_bucket_head;

        if (newest_typedef.value < parse.entity_count)
        {
            CEntityId saved_next = parse.entities[newest_typedef.value].next_typedef_in_lookup;
            saved_typedef_buckets[type_name_bucket] = newest_typedef;
            parse.entities[newest_typedef.value].next_typedef_in_lookup = newest_typedef;
            // A self-cycle must be bounded and must fall back to the same
            // oldest exact typedef rather than looping or returning a prefix.
            BUSTER_TEST(arguments, c_parse_lookup_typedef_name_fallback(&parse, S8("TypeName")).value == oldest_typedef.value);
            parse.entities[newest_typedef.value].next_typedef_in_lookup = saved_next;
            saved_typedef_buckets[type_name_bucket] = saved_bucket_head;
        }
    }
    scratch_end(temporary);

    TemporalArena malformed_temporary = scratch_begin(0, 0);
    CPreprocessResult malformed_tokens = c_preprocess(malformed_temporary.arena,
                                                       S8("int malformed_typedef_fallback(void) { UnknownType value; return 0; }\n"),
                                                       (CPreprocessOptions){0});
    CParseResult malformed_parse = c_parse(malformed_temporary.arena, malformed_tokens);
    BUSTER_TEST(arguments, malformed_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, malformed_parse.diagnostic_count != 0);
    scratch_end(malformed_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_global_types(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena global_temporary = scratch_begin(0, 0);
    Arena* global_arena = global_temporary.arena;
    CPreprocessResult global_tokens = c_preprocess(global_arena,
                                                   S8("extern int imported;\n"
                                                      "static int counter;\n"
                                                      "const int base = 6;\n"
                                                      "const int answer = base * (8 - 1);\n"
                                                      "double ratio = -2.5;\n"
                                                      "static const char label[] = \"ok\\n\";\n"
                                                      "int address_target = 7;\n"
                                                      "int *address_pointer = &address_target;\n"
                                                      "int *null_pointer = 0;\n"
                                                      "int use(void)"
                                                      " { counter += answer;"
                                                      " return counter + answer + imported + '\\n'; }\n"
                                                      "char *text(void) { return \"hi\"; }\n"),
                                                   (CPreprocessOptions){0});
    CParseResult global_parse = c_parse(global_arena, global_tokens);
    CIRLowerResult global_ir = c_lower_to_ir(global_arena, S8("globals.c"), global_tokens, global_parse, target_native);
    BUSTER_TEST(arguments, global_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, global_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, global_ir.program != 0);
    if (global_ir.program)
    {
        IrModule* global_module = &global_ir.program->modules[0];
        BUSTER_TEST(arguments, global_ir.program->symbols.count == 12);
        BUSTER_TEST(arguments, global_module->global_count == 9);
        BUSTER_TEST(arguments, global_module->function_count == 2);
        BUSTER_TEST(arguments, global_module->globals[0].initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
        BUSTER_TEST(arguments, global_module->globals[1].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
        BUSTER_TEST(arguments, global_module->globals[1].initializer_bits == 6);
        BUSTER_TEST(arguments, global_module->globals[1].is_read_only);
        BUSTER_TEST(arguments, global_module->globals[2].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
        BUSTER_TEST(arguments, global_module->globals[2].initializer_bits == 42);
        BUSTER_TEST(arguments, global_module->globals[2].is_read_only);
        BUSTER_TEST(arguments, global_module->globals[3].initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
        BUSTER_TEST(arguments, global_module->globals[4].initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
        BUSTER_TEST(arguments, global_module->globals[4].bytes.length == 4);
        BUSTER_TEST(arguments, global_module->globals[4].bytes.pointer[0] == 'o' && global_module->globals[4].bytes.pointer[1] == 'k' &&
                                   global_module->globals[4].bytes.pointer[2] == '\n' && global_module->globals[4].bytes.pointer[3] == 0);
        BUSTER_TEST(arguments, global_module->globals[5].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
        BUSTER_TEST(arguments, global_module->globals[6].initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
        BUSTER_TEST(arguments, global_module->globals[6].initializer_symbol.value == global_module->globals[5].symbol.value);
        BUSTER_TEST(arguments, global_module->globals[7].initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
        BUSTER_TEST(arguments, global_module->globals[8].initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
        IrSymbol* imported_symbol = ir_symbol_from_id(&global_ir.program->symbols, (IrSymbolId){.value = 0});
        IrSymbol* counter_symbol = ir_symbol_from_id(&global_ir.program->symbols, global_module->globals[0].symbol);
        BUSTER_TEST(arguments, imported_symbol && imported_symbol->kind == IR_SYMBOL_DATA && imported_symbol->linkage == IR_LINKAGE_IMPORT &&
                                   !imported_symbol->is_definition);
        BUSTER_TEST(arguments, counter_symbol && counter_symbol->linkage == IR_LINKAGE_INTERNAL);
        u32 global_reference_count = 0;
        IrFunction* use = global_module->functions;
        for (u32 instruction_index = 0; instruction_index < use->instruction_count; instruction_index += 1)
        {
            global_reference_count += use->instructions[instruction_index].opcode == IR_OPCODE_GLOBAL;
        }
        BUSTER_TEST(arguments, global_reference_count == 5);
        IrFunction* text = global_module->functions + 1;
        u32 index_count = 0;
        u32 address_count = 0;
        for (u32 instruction_index = 0; instruction_index < text->instruction_count; instruction_index += 1)
        {
            index_count += text->instructions[instruction_index].opcode == IR_OPCODE_INDEX;
            address_count += text->instructions[instruction_index].opcode == IR_OPCODE_ADDRESS_OF;
        }
        BUSTER_TEST(arguments, index_count == 1);
        BUSTER_TEST(arguments, address_count == 1);
        IrValidationResult validation = ir_validate_canonical_module(global_ir.program, global_module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    CPreprocessResult forward_tokens = c_preprocess(global_arena,
                                                    S8("extern int later;\n"
                                                       "int *before = &later;\n"
                                                       "int later = 9;\n"),
                                                    (CPreprocessOptions){0});
    CParseResult forward_parse = c_parse(global_arena, forward_tokens);
    CIRLowerResult forward_ir = c_lower_to_ir(global_arena, S8("forward-globals.c"), forward_tokens, forward_parse, target_native);
    BUSTER_TEST(arguments, forward_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, forward_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, forward_ir.program != 0);
    if (forward_ir.program)
    {
        IrModule* forward_module = &forward_ir.program->modules[0];
        BUSTER_TEST(arguments, forward_module->global_count == 2);
        if (forward_module->global_count == 2)
        {
            BUSTER_TEST(arguments, forward_module->globals[0].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
            BUSTER_TEST(arguments, forward_module->globals[1].initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
            BUSTER_TEST(arguments, forward_module->globals[1].initializer_symbol.value == forward_module->globals[0].symbol.value);
        }
    }
    scratch_end(global_temporary);
    CPreprocessResult void_tokens = c_preprocess(arguments->arena, S8("void reset(void) { return; }\n"), (CPreprocessOptions){0});
    CParseResult void_parse = c_parse(arguments->arena, void_tokens);
    CIRLowerResult void_ir = c_lower_to_ir(arguments->arena, S8("void.c"), void_tokens, void_parse, target_native);
    BUSTER_TEST(arguments, void_ir.diagnostic_count == 0);
    if (void_ir.program)
    {
        IrFunction* function = void_ir.program->modules[0].functions;
        IrType* function_type = ir_type_from_id(&void_ir.program->types, function->canonical_type);
        IrType* return_type = function_type ? ir_type_from_id(&void_ir.program->types, function_type->return_type) : 0;
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, return_type && return_type->kind == IR_TYPE_VOID);
        BUSTER_TEST(arguments, function->instruction_count == 1);
        BUSTER_TEST(arguments, function->instructions[0].opcode == IR_OPCODE_RETURN);
        BUSTER_TEST(arguments, function->instructions[0].operand_count == 0);
        IrValidationResult validation = ir_validate_canonical_module(void_ir.program, &void_ir.program->modules[0]);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    Arena* scalar_conflicts[] = {
        arguments->arena,
    };
    TemporalArena scalar_temporary = scratch_begin(scalar_conflicts, BUSTER_ARRAY_LENGTH(scalar_conflicts));
    Arena* scalar_arena = scalar_temporary.arena;
    CPreprocessResult scalar_tokens = c_preprocess(scalar_arena,
                                                   S8("long widen(long value);\n"
                                                      "unsigned short narrow("
                                                      "unsigned char value);\n"
                                                      "double blend(float left,"
                                                      " double right);\n"),
                                                   (CPreprocessOptions){0});
    CParseResult scalar_parse = c_parse(scalar_arena, scalar_tokens);
    Target lp64_target = target_native;
    lp64_target.os = OPERATING_SYSTEM_LINUX;
    CIRLowerResult lp64_ir = c_lower_to_ir(scalar_arena, S8("scalar-lp64.c"), scalar_tokens, scalar_parse, lp64_target);
    BUSTER_TEST(arguments, lp64_ir.diagnostic_count == 0);
    if (lp64_ir.program)
    {
        IrFunction* functions = lp64_ir.program->modules[0].functions;
        IrType* widen_type = ir_type_from_id(&lp64_ir.program->types, functions[0].canonical_type);
        IrType* widen_return = ir_type_from_id(&lp64_ir.program->types, widen_type->return_type);
        IrType* widen_parameter = ir_type_from_id(&lp64_ir.program->types, widen_type->parameter_types[0]);
        BUSTER_TEST(arguments, widen_return->kind == IR_TYPE_INTEGER);
        BUSTER_TEST(arguments, widen_return->bit_width == 64);
        BUSTER_TEST(arguments, widen_return->is_signed);
        BUSTER_TEST(arguments, widen_parameter->bit_width == 64);
        IrType* narrow_type = ir_type_from_id(&lp64_ir.program->types, functions[1].canonical_type);
        IrType* narrow_return = ir_type_from_id(&lp64_ir.program->types, narrow_type->return_type);
        IrType* narrow_parameter = ir_type_from_id(&lp64_ir.program->types, narrow_type->parameter_types[0]);
        BUSTER_TEST(arguments, narrow_return->bit_width == 16);
        BUSTER_TEST(arguments, !narrow_return->is_signed);
        BUSTER_TEST(arguments, narrow_parameter->bit_width == 8);
        BUSTER_TEST(arguments, !narrow_parameter->is_signed);
        IrType* blend_type = ir_type_from_id(&lp64_ir.program->types, functions[2].canonical_type);
        IrType* blend_return = ir_type_from_id(&lp64_ir.program->types, blend_type->return_type);
        IrType* blend_left = ir_type_from_id(&lp64_ir.program->types, blend_type->parameter_types[0]);
        IrType* blend_right = ir_type_from_id(&lp64_ir.program->types, blend_type->parameter_types[1]);
        BUSTER_TEST(arguments, blend_return->kind == IR_TYPE_FLOAT);
        BUSTER_TEST(arguments, blend_return->bit_width == 64);
        BUSTER_TEST(arguments, blend_left->bit_width == 32);
        BUSTER_TEST(arguments, blend_right->bit_width == 64);
    }
    Target llp64_target = target_native;
    llp64_target.os = OPERATING_SYSTEM_WINDOWS;
    CIRLowerResult llp64_ir = c_lower_to_ir(scalar_arena, S8("scalar-llp64.c"), scalar_tokens, scalar_parse, llp64_target);
    BUSTER_TEST(arguments, llp64_ir.diagnostic_count == 0);
    if (llp64_ir.program)
    {
        IrFunction* widen = llp64_ir.program->modules[0].functions;
        IrType* function_type = ir_type_from_id(&llp64_ir.program->types, widen->canonical_type);
        IrType* return_type = ir_type_from_id(&llp64_ir.program->types, function_type->return_type);
        IrType* parameter_type = ir_type_from_id(&llp64_ir.program->types, function_type->parameter_types[0]);
        BUSTER_TEST(arguments, return_type->bit_width == 32);
        BUSTER_TEST(arguments, parameter_type->bit_width == 32);
    }
    CPreprocessResult integer_body_tokens = c_preprocess(scalar_arena,
                                                         S8("unsigned long combine("
                                                            "unsigned short left,"
                                                            " unsigned long right)\n"
                                                            "{\n"
                                                            "    unsigned char narrow"
                                                            " = left;\n"
                                                            "    unsigned long widened"
                                                            " = narrow;\n"
                                                            "    return widened / right;\n"
                                                            "}\n"),
                                                         (CPreprocessOptions){0});
    CParseResult integer_body_parse = c_parse(scalar_arena, integer_body_tokens);
    CIRLowerResult integer_body_ir = c_lower_to_ir(scalar_arena, S8("integer-body.c"), integer_body_tokens, integer_body_parse, lp64_target);
    BUSTER_TEST(arguments, integer_body_ir.diagnostic_count == 0);
    if (integer_body_ir.program)
    {
        IrFunction* function = integer_body_ir.program->modules[0].functions;
        u32 truncate_count = 0;
        u32 extend_count = 0;
        u32 unsigned_divide_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = &function->instructions[instruction_index];
            truncate_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE;
            extend_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND;
            unsigned_divide_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_UNSIGNED_DIVIDE;
        }
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, truncate_count == 1);
        BUSTER_TEST(arguments, extend_count == 1);
        BUSTER_TEST(arguments, unsigned_divide_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(integer_body_ir.program, &integer_body_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult float_body_tokens = c_preprocess(scalar_arena,
                                                       S8("double blend_values("
                                                          "float left, int count)\n"
                                                          "{\n"
                                                          "    double base = 1.5;\n"
                                                          "    float scale = 2.0f;\n"
                                                          "    return base +"
                                                          " scale * left + count;\n"
                                                          "}\n"),
                                                       (CPreprocessOptions){0});
    CParseResult float_body_parse = c_parse(scalar_arena, float_body_tokens);
    CIRLowerResult float_body_ir = c_lower_to_ir(scalar_arena, S8("float-body.c"), float_body_tokens, float_body_parse, lp64_target);
    BUSTER_TEST(arguments, float_body_ir.diagnostic_count == 0);
    if (float_body_ir.program)
    {
        IrFunction* function = float_body_ir.program->modules[0].functions;
        u32 constant_count = 0;
        u32 multiply_count = 0;
        u32 extend_count = 0;
        u32 integer_to_float_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = &function->instructions[instruction_index];
            constant_count += instruction->opcode == IR_OPCODE_CONSTANT_FLOAT;
            multiply_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_FLOAT_MULTIPLY;
            extend_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND;
            integer_to_float_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT;
        }
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, constant_count == 2);
        BUSTER_TEST(arguments, multiply_count == 1);
        BUSTER_TEST(arguments, extend_count == 1);
        BUSTER_TEST(arguments, integer_to_float_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(float_body_ir.program, &float_body_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult pointer_body_tokens = c_preprocess(scalar_arena,
                                                         S8("int *identity_pointer("
                                                            "int *value)\n"
                                                            "{ return value; }\n"
                                                            "int *address_value(int value)\n"
                                                            "{ return &value; }\n"
                                                            "int load_value(int *value)\n"
                                                            "{ return *value; }\n"),
                                                         (CPreprocessOptions){0});
    CParseResult pointer_body_parse = c_parse(scalar_arena, pointer_body_tokens);
    CIRLowerResult pointer_body_ir = c_lower_to_ir(scalar_arena, S8("pointer-body.c"), pointer_body_tokens, pointer_body_parse, lp64_target);
    BUSTER_TEST(arguments, pointer_body_ir.diagnostic_count == 0);
    if (pointer_body_ir.program)
    {
        IrModule* module = &pointer_body_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 3);
        BUSTER_TEST(arguments, module->functions[0].state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, module->functions[1].state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, module->functions[2].state == IR_FUNCTION_LOWERED);
        u32 address_count = 0;
        u32 dereference_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                address_count += function->instructions[instruction_index].opcode == IR_OPCODE_ADDRESS_OF;
                dereference_count += function->instructions[instruction_index].opcode == IR_OPCODE_DEREFERENCE;
            }
        }
        BUSTER_TEST(arguments, address_count == 1);
        BUSTER_TEST(arguments, dereference_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(pointer_body_ir.program, module).error == IR_VALIDATION_NONE);
    }
    // `*p` on a pointer to an array designates the array object, so the
    // dereference is a place and nothing reads it: a `LOAD` of the array type
    // would copy the whole object into a temporary, and the subscript that
    // follows would then index the copy -- which is how `(*p)[1] = v` stored
    // into a frame slot and dropped the assignment (#719).  Both bodies below
    // dereference and index once; only the reading one loads, and neither
    // loads anything of array type.
    CPreprocessResult pointer_to_array_tokens = c_preprocess(scalar_arena,
                                                             S8("void store_element(int (*p)[3], int v)\n"
                                                                "{ (*p)[1] = v; }\n"
                                                                "int read_element(int (*p)[3])\n"
                                                                "{ return (*p)[1]; }\n"),
                                                             (CPreprocessOptions){0});
    CParseResult pointer_to_array_parse = c_parse(scalar_arena, pointer_to_array_tokens);
    CIRLowerResult pointer_to_array_ir =
        c_lower_to_ir(scalar_arena, S8("pointer-to-array.c"), pointer_to_array_tokens, pointer_to_array_parse, lp64_target);
    BUSTER_TEST(arguments, pointer_to_array_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, pointer_to_array_ir.diagnostic_count == 0);
    if (pointer_to_array_ir.program)
    {
        IrModule* module = &pointer_to_array_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 2);
        if (module->function_count == 2)
        {
            u32 aggregate_load_count = 0;
            u32 store_dereference_count = 0;
            u32 store_index_count = 0;
            u32 store_load_count = 0;
            u32 read_load_count = 0;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = &module->functions[function_index];
                BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    IrType* type = ir_type_from_id(&pointer_to_array_ir.program->types, instruction->canonical_type);
                    if (instruction->opcode == IR_OPCODE_LOAD && type && type->kind == IR_TYPE_ARRAY)
                    {
                        aggregate_load_count += 1;
                    }
                    if (!function_index)
                    {
                        store_dereference_count += instruction->opcode == IR_OPCODE_DEREFERENCE;
                        store_index_count += instruction->opcode == IR_OPCODE_INDEX;
                        store_load_count += instruction->opcode == IR_OPCODE_LOAD;
                    }
                    else
                    {
                        read_load_count += instruction->opcode == IR_OPCODE_LOAD;
                    }
                }
            }
            BUSTER_TEST(arguments, aggregate_load_count == 0);
            BUSTER_TEST(arguments, store_dereference_count == 1);
            BUSTER_TEST(arguments, store_index_count == 1);
            // The pointer and the stored value are the only things read.
            BUSTER_TEST(arguments, store_load_count == 2);
            // The pointer and the element it names.
            BUSTER_TEST(arguments, read_load_count == 2);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(pointer_to_array_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult function_pointer_tokens =
        c_preprocess(scalar_arena,
                     S8("extern int launch(void *(*)(void *), void *);\n"
                        "static void *worker(void *argument) { return argument; }\n"
                        "int run(void *argument) { return launch(worker, argument); }\n"
                        "void *invoke(void *argument) { return ((void *(*)(void *))worker)(argument); }\n"),
                     (CPreprocessOptions){0});
    CParseResult function_pointer_parse = c_parse(scalar_arena, function_pointer_tokens);
    CIRLowerResult function_pointer_ir =
        c_lower_to_ir(scalar_arena, S8("function-pointer.c"), function_pointer_tokens, function_pointer_parse, lp64_target);
    BUSTER_TEST(arguments, function_pointer_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, function_pointer_ir.diagnostic_count == 0);
    if (function_pointer_ir.program)
    {
        IrModule* module = &function_pointer_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 4);
        if (module->function_count == 4)
        {
            IrType* launch_type = ir_type_from_id(&function_pointer_ir.program->types, module->functions[0].canonical_type);
            IrType* entry_pointer = launch_type ? ir_type_from_id(&function_pointer_ir.program->types, launch_type->parameter_types[0]) : 0;
            IrType* entry_function =
                entry_pointer && entry_pointer->kind == IR_TYPE_POINTER ? ir_type_from_id(&function_pointer_ir.program->types, entry_pointer->element_type) : 0;
            BUSTER_TEST(arguments, entry_pointer && entry_pointer->kind == IR_TYPE_POINTER);
            BUSTER_TEST(arguments, entry_function && entry_function->kind == IR_TYPE_FUNCTION);
            BUSTER_TEST(arguments, module->functions[1].state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, module->functions[2].state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, module->functions[3].state == IR_FUNCTION_LOWERED);
        }
    }
    // __builtin_prefetch is a hint: the address operand keeps its side effects,
    // the call itself disappears.  base.h falls back to <intrin.h> when the
    // builtin is missing, which drags all of <immintrin.h> into MSVC-targeted
    // builds.
    CPreprocessResult prefetch_tokens = c_preprocess(scalar_arena,
                                                     S8("extern int sink;\n"
                                                        "static int bump(int *values) { sink += 1; return values[0]; }\n"
                                                        "int warm(int *values)\n"
                                                        "{\n"
                                                        "    __builtin_prefetch(values);\n"
                                                        "    __builtin_prefetch(values, 0, 3);\n"
                                                        "    __builtin_prefetch(&values[bump(values)], 1, 1);\n"
                                                        "    return values[0];\n"
                                                        "}\n"),
                                                     (CPreprocessOptions){0});
    CParseResult prefetch_parse = c_parse(scalar_arena, prefetch_tokens);
    CIRLowerResult prefetch_ir = c_lower_to_ir(scalar_arena, S8("prefetch.c"), prefetch_tokens, prefetch_parse, lp64_target);
    BUSTER_TEST(arguments, prefetch_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, prefetch_ir.diagnostic_count == 0);
    if (prefetch_ir.program)
    {
        IrModule* module = &prefetch_ir.program->modules[0];
        bool called_prefetch = false;
        u32 bump_calls = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction instruction = function->instructions[instruction_index];
                if (instruction.opcode != IR_OPCODE_CALL)
                {
                    continue;
                }
                IrSymbol* symbol = ir_symbol_from_id(&prefetch_ir.program->symbols, instruction.symbol);
                called_prefetch |= symbol && string_equal(symbol->link_name, S8("__builtin_prefetch"));
                bump_calls += symbol && string_equal(symbol->link_name, S8("bump"));
            }
        }
        BUSTER_TEST(arguments, !called_prefetch);
        BUSTER_TEST(arguments, bump_calls == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(prefetch_ir.program, module).error == IR_VALIDATION_NONE);
    }
    // C99 6.7.4p7: an inline definition provides no external definition, so an
    // unused one must not be emitted -- and must not import what its body
    // touches.  MSVC's <immintrin.h> reaches __isa_inverted this way.
    CPreprocessResult inline_definition_tokens = c_preprocess(scalar_arena,
                                                              S8("extern int inline_only_symbol;\n"
                                                                 "extern int emitted_symbol;\n"
                                                                 "inline int inline_only(void) { return inline_only_symbol; }\n"
                                                                 "__inline int inline_keyword_only(void) { return inline_only_symbol; }\n"
                                                                 "extern inline int extern_inline(void) { return emitted_symbol; }\n"
                                                                 "int redeclared(void);\n"
                                                                 "inline int redeclared(void) { return emitted_symbol; }\n"
                                                                 "inline int inline_used(void) { return emitted_symbol; }\n"
                                                                 "int caller(void) { return inline_used(); }\n"),
                                                              (CPreprocessOptions){0});
    CParseResult inline_definition_parse = c_parse(scalar_arena, inline_definition_tokens);
    CIRLowerResult inline_definition_ir =
        c_lower_to_ir(scalar_arena, S8("inline-definition.c"), inline_definition_tokens, inline_definition_parse, lp64_target);
    BUSTER_TEST(arguments, inline_definition_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, inline_definition_ir.diagnostic_count == 0);
    if (inline_definition_ir.program)
    {
        IrModule* module = &inline_definition_ir.program->modules[0];
        bool found_inline_only = false;
        bool found_inline_keyword_only = false;
        bool found_extern_inline = false;
        bool found_redeclared = false;
        bool found_inline_used = false;
        bool found_caller = false;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            String8 name = module->functions[function_index].name;
            found_inline_only |= string_equal(name, S8("inline_only"));
            found_inline_keyword_only |= string_equal(name, S8("inline_keyword_only"));
            found_extern_inline |= string_equal(name, S8("extern_inline"));
            found_redeclared |= string_equal(name, S8("redeclared"));
            found_inline_used |= string_equal(name, S8("inline_used"));
            found_caller |= string_equal(name, S8("caller"));
        }
        BUSTER_TEST(arguments, !found_inline_only);
        BUSTER_TEST(arguments, !found_inline_keyword_only);
        BUSTER_TEST(arguments, found_extern_inline);
        BUSTER_TEST(arguments, found_redeclared);
        BUSTER_TEST(arguments, found_inline_used);
        BUSTER_TEST(arguments, found_caller);
        // No emitted body may reach the extern only the dropped inline
        // definitions touched, which is what turns into an unresolvable import.
        bool referenced_inline_only_symbol = false;
        bool referenced_emitted_symbol = false;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrSymbol* symbol = ir_symbol_from_id(&inline_definition_ir.program->symbols, function->instructions[instruction_index].symbol);
                referenced_inline_only_symbol |= symbol && string_equal(symbol->link_name, S8("inline_only_symbol"));
                referenced_emitted_symbol |= symbol && string_equal(symbol->link_name, S8("emitted_symbol"));
            }
        }
        BUSTER_TEST(arguments, !referenced_inline_only_symbol);
        BUSTER_TEST(arguments, referenced_emitted_symbol);
    }
    CPreprocessResult typedef_shadow_tokens = c_preprocess(scalar_arena,
                                                           S8("typedef void *id;\n"
                                                              "typedef int TokenId;\n"
                                                              "void tokenize(void) { TokenId id = 0; id = 1; }\n"),
                                                           (CPreprocessOptions){0});
    CParseResult typedef_shadow_parse = c_parse(scalar_arena, typedef_shadow_tokens);
    CIRLowerResult typedef_shadow_ir = c_lower_to_ir(scalar_arena, S8("typedef-shadow.c"), typedef_shadow_tokens, typedef_shadow_parse, lp64_target);
    BUSTER_TEST(arguments, typedef_shadow_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, typedef_shadow_ir.diagnostic_count == 0);
    if (typedef_shadow_ir.program)
    {
        IrModule* module = &typedef_shadow_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 1);
        BUSTER_TEST(arguments, module->function_count == 1 && module->functions[0].state == IR_FUNCTION_LOWERED);
    }
    CPreprocessResult array_type_tokens = c_preprocess(scalar_arena, S8("int sum_four(int values[4]);\n"), (CPreprocessOptions){0});
    CParseResult array_type_parse = c_parse(scalar_arena, array_type_tokens);
    CIRLowerResult array_type_ir = c_lower_to_ir(scalar_arena, S8("array-type.c"), array_type_tokens, array_type_parse, lp64_target);
    BUSTER_TEST(arguments, array_type_ir.diagnostic_count == 0);
    if (array_type_ir.program)
    {
        IrFunction* function = array_type_ir.program->modules[0].functions;
        IrType* signature = ir_type_from_id(&array_type_ir.program->types, function->canonical_type);
        IrType* parameter = ir_type_from_id(&array_type_ir.program->types, signature->parameter_types[0]);
        BUSTER_TEST(arguments, parameter->kind == IR_TYPE_POINTER);
        IrType* element = ir_type_from_id(&array_type_ir.program->types, parameter->element_type);
        BUSTER_TEST(arguments, element->kind == IR_TYPE_INTEGER);
        BUSTER_TEST(arguments, element->bit_width == 32);
        bool found_array = false;
        for (u32 type_index = 0; type_index < array_type_ir.program->types.count; type_index += 1)
        {
            IrType* type = &array_type_ir.program->types.types[type_index];
            if (type->kind == IR_TYPE_ARRAY)
            {
                found_array = type->element_count == 4 && type->layout.size == 16 && type->layout.alignment == 4;
            }
        }
        BUSTER_TEST(arguments, found_array);
    }
    scratch_end(scalar_temporary);
    TemporalArena aggregate_temporary = scratch_begin(0, 0);
    CPreprocessResult aggregate_tokens = c_preprocess(aggregate_temporary.arena,
                                                      S8("struct Pair {\n"
                                                         "    int left;\n"
                                                         "    long right;\n"
                                                         "};\n"
                                                         "union Bits {\n"
                                                         "    unsigned int word;\n"
                                                         "    float real;\n"
                                                         "};\n"
                                                         "struct Node {\n"
                                                         "    int value;\n"
                                                         "    struct Node *next;\n"
                                                         "};\n"
                                                         "typedef struct Pair Pair;\n"
                                                         "typedef struct {\n"
                                                         "    int x;\n"
                                                         "    int y;\n"
                                                         "} AnonymousPair;\n"
                                                         "enum Direction {\n"
                                                         "    DIRECTION_NEGATIVE = -1,\n"
                                                         "    DIRECTION_ZERO,\n"
                                                         "    DIRECTION_POSITIVE = 4\n"
                                                         "};\n"
                                                         "static Pair origin ="
                                                         " {.right = 2,"
                                                         " .left = DIRECTION_POSITIVE};\n"
                                                         "static int sequence[3] ="
                                                         " {[2] = 6, [0] = 4, [1] = 5};\n"
                                                         "static union Bits bits = {.word = 7};\n"
                                                         "static int direction ="
                                                         " DIRECTION_POSITIVE + 1;\n"
                                                         "int sum_pair(int left, int right) {\n"
                                                         "    Pair pair;\n"
                                                         "    pair.left = left;\n"
                                                         "    pair.right = right;\n"
                                                         "    pair.left += 1;\n"
                                                         "    return pair.left + pair.right;\n"
                                                         "}\n"
                                                         "int node_value(struct Node *node) {\n"
                                                         "    return node->value;\n"
                                                         "}\n"
                                                         "int next_node_value(struct Node *node) {\n"
                                                         "    return node->next->value;\n"
                                                         "}\n"
                                                         "Pair echo_pair(Pair pair) {\n"
                                                         "    return pair;\n"
                                                         "}\n"
                                                         "int read_pair(Pair pair) {\n"
                                                         "    return pair.left;\n"
                                                         "}\n"
                                                         "int read_echo_pair(Pair pair) {\n"
                                                         "    return echo_pair(pair).left;\n"
                                                         "}\n"
                                                         "int call_read(int left) {\n"
                                                         "    Pair pair;\n"
                                                         "    pair.left = left;\n"
                                                         "    pair.right = 0;\n"
                                                         "    return read_pair(echo_pair(pair));\n"
                                                         "}\n"
                                                         "int anonymous_sum(int x) {\n"
                                                         "    AnonymousPair pair;\n"
                                                         "    pair.x = x;\n"
                                                         "    pair.y = 1;\n"
                                                         "    return pair.x + pair.y;\n"
                                                         "}\n"
                                                         "int enum_value(void) {\n"
                                                         "    return DIRECTION_NEGATIVE"
                                                         " + DIRECTION_ZERO"
                                                         " + DIRECTION_POSITIVE;\n"
                                                         "}\n"
                                                         "int array_sum(int index) {\n"
                                                         "    int values[3];\n"
                                                         "    values[0] = 1;\n"
                                                         "    values[1] = 2;\n"
                                                         "    values[2] = 3;\n"
                                                         "    values[index] += 1;\n"
                                                         "    return values[0] + values[index];\n"
                                                         "}\n"
                                                         "int first(int values[3]) {\n"
                                                         "    return values[0];\n"
                                                         "}\n"
                                                         "int call_first(void) {\n"
                                                         "    int values[3];\n"
                                                         "    values[0] = 7;\n"
                                                         "    return first(values);\n"
                                                         "}\n"
                                                         "int *offset(int *values) {\n"
                                                         "    return values + 1;\n"
                                                         "}\n"
                                                         "int same(int *left, int *right) {\n"
                                                         "    return left == right;\n"
                                                         "}\n"),
                                                      (CPreprocessOptions){0});
    CParseResult aggregate_parse = c_parse(aggregate_temporary.arena, aggregate_tokens);
    BUSTER_TEST(arguments, aggregate_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_parse.declaration_count == 24);
    BUSTER_TEST(arguments, aggregate_parse.member_count == 8);
    BUSTER_TEST(arguments, aggregate_parse.enum_member_count == 3);
    u32 complete_aggregate_count = 0;
    bool found_self_pointer = false;
    for (u32 type_index = 0; type_index < aggregate_parse.type_count; type_index += 1)
    {
        CType* type = &aggregate_parse.types[type_index];
        if ((type->kind == C_TYPE_STRUCT || type->kind == C_TYPE_UNION) && type->is_complete)
        {
            complete_aggregate_count += 1;
            if (string_equal(type->tag, S8("Node")) && type->member_count == 2)
            {
                CMember* next = &aggregate_parse.members[type->member_start + 1];
                CType* pointer = &aggregate_parse.types[next->type.value];
                found_self_pointer = pointer->kind == C_TYPE_POINTER && pointer->element_type.value == type_index;
            }
        }
    }
    BUSTER_TEST(arguments, complete_aggregate_count == 4);
    BUSTER_TEST(arguments, found_self_pointer);
    CIRLowerResult aggregate_ir = c_lower_to_ir(aggregate_temporary.arena, S8("aggregates.c"), aggregate_tokens, aggregate_parse, target_native);
    BUSTER_TEST(arguments, aggregate_ir.diagnostic_count == 0);
    if (aggregate_ir.program)
    {
        bool pair_layout = false;
        bool union_layout = false;
        bool node_layout = false;
        for (u32 type_index = 0; type_index < aggregate_ir.program->types.count; type_index += 1)
        {
            IrType* type = &aggregate_ir.program->types.types[type_index];
            if (type->kind == IR_TYPE_STRUCT && string_equal(type->name, S8("Pair")))
            {
                u64 expected_size = target_native.os == OPERATING_SYSTEM_WINDOWS ? 8 : 16;
                u64 expected_offset = target_native.os == OPERATING_SYSTEM_WINDOWS ? 4 : 8;
                pair_layout =
                    type->layout.resolved && type->layout.size == expected_size && type->field_count == 2 && type->fields[1].offset == expected_offset;
            }
            else if (type->kind == IR_TYPE_UNION && string_equal(type->name, S8("Bits")))
            {
                union_layout =
                    type->layout.resolved && type->layout.size == 4 && type->field_count == 2 && type->fields[0].offset == 0 && type->fields[1].offset == 0;
            }
            else if (type->kind == IR_TYPE_STRUCT && string_equal(type->name, S8("Node")))
            {
                node_layout = type->layout.resolved && type->layout.size == 16 && type->field_count == 2 && type->fields[1].offset == 8;
            }
        }
        BUSTER_TEST(arguments, pair_layout);
        BUSTER_TEST(arguments, union_layout);
        BUSTER_TEST(arguments, node_layout);
        IrModule* module = &aggregate_ir.program->modules[0];
        BUSTER_TEST(arguments, module->global_count == 4);
        if (module->global_count == 4)
        {
            BUSTER_TEST(arguments, module->globals[0].initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
            BUSTER_TEST(arguments, module->globals[0].bytes.length != 0);
            IrType* origin_type = ir_type_from_id(&aggregate_ir.program->types, module->globals[0].type);
            BUSTER_TEST(arguments,
                        origin_type && module->globals[0].bytes.pointer[0] == 4 && module->globals[0].bytes.pointer[origin_type->fields[1].offset] == 2);
            IrType* sequence_type = ir_type_from_id(&aggregate_ir.program->types, module->globals[1].type);
            IrType* sequence_element = sequence_type ? ir_type_from_id(&aggregate_ir.program->types, sequence_type->element_type) : 0;
            BUSTER_TEST(arguments, sequence_type && sequence_element && module->globals[1].bytes.length == sequence_type->layout.size &&
                                       module->globals[1].bytes.pointer[0] == 4 && module->globals[1].bytes.pointer[sequence_element->layout.size] == 5 &&
                                       module->globals[1].bytes.pointer[sequence_element->layout.size * 2] == 6);
            BUSTER_TEST(arguments, module->globals[2].bytes.pointer[0] == 7);
            BUSTER_TEST(arguments, module->globals[3].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER && module->globals[3].initializer_bits == 5);
        }
        BUSTER_TEST(arguments, module->function_count == 14);
        BUSTER_TEST(arguments, module->lowered_function_count == 14);
        u32 field_count = 0;
        u32 index_count = 0;
        u32 pointer_comparison_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                field_count += function->instructions[instruction_index].opcode == IR_OPCODE_FIELD;
                index_count += function->instructions[instruction_index].opcode == IR_OPCODE_INDEX;
                pointer_comparison_count += function->instructions[instruction_index].binary_operation == IR_BINARY_POINTER_EQUAL;
            }
        }
        BUSTER_TEST(arguments, field_count == 16);
        BUSTER_TEST(arguments, index_count == 10);
        BUSTER_TEST(arguments, pointer_comparison_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(aggregate_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(aggregate_temporary);
    TemporalArena extension_aggregate_temporary = scratch_begin(0, 0);
    CPreprocessResult extension_aggregate_tokens = c_preprocess(extension_aggregate_temporary.arena,
                                                                S8("typedef long SystemLong;\n"
                                                                   "struct ResourceUsage {\n"
                                                                   "    int prefix;\n"
                                                                   "    __extension__ union {\n"
                                                                   "        long value;\n"
                                                                   "        SystemLong system_value;\n"
                                                                   "    };\n"
                                                                   "    int suffix;\n"
                                                                   "};\n"
                                                                   "long resource_value(long input) {\n"
                                                                   "    struct ResourceUsage usage;\n"
                                                                   "    usage.value = input;\n"
                                                                   "    return usage.system_value;\n"
                                                                   "}\n"),
                                                                (CPreprocessOptions){0});
    CParseResult extension_aggregate_parse = c_parse(extension_aggregate_temporary.arena, extension_aggregate_tokens);
    BUSTER_TEST(arguments, extension_aggregate_parse.diagnostic_count == 0);
    CIRLowerResult extension_aggregate_ir =
        c_lower_to_ir(extension_aggregate_temporary.arena, S8("extension-aggregate.c"), extension_aggregate_tokens, extension_aggregate_parse, lp64_target);
    BUSTER_TEST(arguments, extension_aggregate_ir.diagnostic_count == 0);
    bool found_extension_aggregate_layout = false;
    if (extension_aggregate_ir.program)
    {
        for (u32 type_index = 0; type_index < extension_aggregate_ir.program->types.count; type_index += 1)
        {
            IrType* type = &extension_aggregate_ir.program->types.types[type_index];
            if (type->kind == IR_TYPE_STRUCT && string_equal(type->name, S8("ResourceUsage")))
            {
                found_extension_aggregate_layout = type->layout.resolved && type->layout.size == 24 && type->layout.alignment == 8 && type->field_count == 3;
            }
        }
    }
    BUSTER_TEST(arguments, found_extension_aggregate_layout);
    scratch_end(extension_aggregate_temporary);
    CPreprocessResult local_tokens = c_preprocess(arguments->arena,
                                                  S8("int main(void) {\n"
                                                     "    int value = 2;\n"
                                                     "    value = value * 3;\n"
                                                     "    return value;\n"
                                                     "}\n"),
                                                  (CPreprocessOptions){0});
    CParseResult local_parse = c_parse(arguments->arena, local_tokens);
    BUSTER_TEST(arguments, local_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, local_parse.declaration_count == 1);
    BUSTER_TEST(arguments, local_parse.entity_count == 2);
    BUSTER_TEST(arguments, local_parse.scope_count == 2);
    BUSTER_TEST(arguments, local_parse.identifier_use_count == 3);
    if (local_parse.entity_count == 2)
    {
        BUSTER_TEST(arguments, local_parse.entities[1].kind == C_ENTITY_LOCAL);
        BUSTER_STRING_TEST(arguments, local_parse.entities[1].name, S8("value"));
        for (u32 use_index = 0; use_index < local_parse.identifier_use_count; use_index += 1)
        {
            BUSTER_TEST(arguments, local_parse.identifier_uses[use_index].entity.value == 1);
        }
    }
    CIRLowerResult local_ir = c_lower_to_ir(arguments->arena, S8("locals.c"), local_tokens, local_parse, target_native);
    BUSTER_TEST(arguments, local_ir.diagnostic_count == 0);
    if (local_ir.program)
    {
        IrFunction* function = local_ir.program->modules[0].functions;
        u32 local_count = 0;
        u32 load_count = 0;
        u32 store_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            local_count += opcode == IR_OPCODE_LOCAL;
            load_count += opcode == IR_OPCODE_LOAD;
            store_count += opcode == IR_OPCODE_STORE;
        }
        BUSTER_TEST(arguments, function->local_count == 1);
        BUSTER_TEST(arguments, local_count == 1);
        BUSTER_TEST(arguments, load_count == 2);
        BUSTER_TEST(arguments, store_count == 2);
        BUSTER_TEST(arguments, ir_validate_canonical_module(local_ir.program, &local_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    {
        TemporalArena volatile_temporary = scratch_begin(0, 0);
        CPreprocessResult volatile_tokens = {0};
        CParseResult volatile_parse = {0};
        CIRLowerResult volatile_ir = c_test_lower_source(
            volatile_temporary.arena,
            S8("volatile int global_value;"
               " struct Pair { int member; };"
               " int read_volatile(volatile int *pointer, volatile struct Pair *pair) {"
               "   volatile int local = *pointer;"
               "   global_value = local;"
               "   pair->member = global_value;"
               "   return *pointer + pair->member;"
               " }"
               " struct Locale { int *locinfo; };"
               " typedef struct Locale *locale_t;"
               " int *get_locale_data_prefix(void const volatile *const locale_pointers) {"
               "   locale_t const typed_locale_pointers = (locale_t)locale_pointers;"
               "   return typed_locale_pointers->locinfo;"
               " }"),
            S8("volatile-accesses.c"), target_native, &volatile_tokens, &volatile_parse);
        BUSTER_TEST(arguments, volatile_tokens.diagnostic_count == 0 && volatile_parse.diagnostic_count == 0 &&
                                   volatile_ir.diagnostic_count == 0 && volatile_ir.program);
        if (volatile_ir.program)
        {
            IrFunction* function = volatile_ir.program->modules[0].functions;
            u32 memory_access_count = 0;
            u32 volatile_access_count = 0;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                bool memory_access = instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_STORE;
                memory_access_count += memory_access;
                volatile_access_count += memory_access && instruction->volatile_access;
            }
            BUSTER_TEST(arguments, memory_access_count > volatile_access_count && volatile_access_count == 8);
            BUSTER_TEST(arguments, ir_validate_canonical_module(volatile_ir.program, &volatile_ir.program->modules[0]).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, volatile_ir.program->modules[0].function_count == 2);
            if (volatile_ir.program->modules[0].function_count == 2)
            {
                IrFunction* locale_function = volatile_ir.program->modules[0].functions + 1;
                IrType* signature = ir_type_from_id(&volatile_ir.program->types, locale_function->canonical_type);
                IrType* parameter = signature && signature->parameter_count == 1
                                        ? ir_type_from_id(&volatile_ir.program->types, signature->parameter_types[0])
                                        : 0;
                IrType* element = parameter && parameter->kind == IR_TYPE_POINTER
                                      ? ir_type_from_id(&volatile_ir.program->types, parameter->element_type)
                                      : 0;
                BUSTER_TEST(arguments, element && element->kind == IR_TYPE_VOID && element->is_volatile);
            }
        }
        scratch_end(volatile_temporary);
    }
    CPreprocessResult shadow_tokens = c_preprocess(arguments->arena,
                                                   S8("int main(int value) {\n"
                                                      "    int result = value;\n"
                                                      "    { int value = 2;"
                                                      " result = value; }\n"
                                                      "    return result;\n"
                                                      "}\n"),
                                                   (CPreprocessOptions){0});
    CParseResult shadow_parse = c_parse(arguments->arena, shadow_tokens);
    BUSTER_TEST(arguments, shadow_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, shadow_parse.scope_count == 3);
    BUSTER_TEST(arguments, shadow_parse.entity_count == 4);
    BUSTER_TEST(arguments, shadow_parse.identifier_use_count == 4);
    if (shadow_parse.identifier_use_count == 4)
    {
        CEntityId parameter = shadow_parse.parameters[0].entity;
        CEntityId outer_result = shadow_parse.identifier_uses[1].entity;
        CEntityId inner_value = shadow_parse.identifier_uses[2].entity;
        BUSTER_TEST(arguments, shadow_parse.identifier_uses[0].entity.value == parameter.value);
        BUSTER_TEST(arguments, inner_value.value != parameter.value);
        BUSTER_TEST(arguments, shadow_parse.identifier_uses[3].entity.value == outer_result.value);
    }
    CIRLowerResult shadow_ir = c_lower_to_ir(arguments->arena, S8("shadow.c"), shadow_tokens, shadow_parse, target_native);
    BUSTER_TEST(arguments, shadow_ir.diagnostic_count == 0);
    if (shadow_ir.program)
    {
        IrFunction* function = shadow_ir.program->modules[0].functions;
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, function->local_count == 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(shadow_ir.program, &shadow_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }

    CPreprocessResult auto_type_tokens = c_preprocess(arguments->arena,
                                                      S8("int auto_type_decay_function(void) { return 17; }\n"
                                                         "struct AutoTypeIrPair { int left; int right; };\n"
                                                         "int auto_type_decay_test(void) {\n"
                                                         "    __auto_type array_result = (int[2]){ 4, 9 };\n"
                                                         "    __auto_type function_result = auto_type_decay_function;\n"
                                                         "    const __auto_type qualified_result = 3;\n"
                                                         "    __auto_type float_result = 1.5f;\n"
                                                         "    const int qualified_source = 5;\n"
                                                         "    __auto_type unqualified_lvalue_result = qualified_source;\n"
                                                         "    return array_result[1] + function_result() + qualified_result + unqualified_lvalue_result;\n"
                                                         "}\n"
                                                         "int auto_type_ir_shape_test(void) {\n"
                                                         "    __auto_type ir_scalar = 1;\n"
                                                         "    __auto_type ir_pointer = (int *)0;\n"
                                                         "    __auto_type ir_float = 1.25f;\n"
                                                         "    __auto_type ir_aggregate = (struct AutoTypeIrPair){ 2, 3 };\n"
                                                         "    return ir_scalar + (int)ir_float + (ir_pointer == 0) + ir_aggregate.left;\n"
                                                         "}\n"
                                                         "int auto_type_scope_test(void) {\n"
                                                         "    int value = 7;\n"
                                                         "    { __auto_type value = value; return value; }\n"
                                                         "}\n"),
                                                      (CPreprocessOptions){
                                                          .target = target_native,
                                                          .data_layout = target_data_layout(target_native),
                                                          .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                      });
    CParseResult auto_type_parse = c_parse(arguments->arena, auto_type_tokens);
    BUSTER_TEST(arguments, auto_type_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, auto_type_parse.diagnostic_count == 0);
    CEntityId array_entity = c_test_find_local_entity(&auto_type_parse, S8("array_result"), C_SCOPE_ID_INVALID);
    CEntityId function_entity = c_test_find_local_entity(&auto_type_parse, S8("function_result"), C_SCOPE_ID_INVALID);
    CEntityId qualified_entity = c_test_find_local_entity(&auto_type_parse, S8("qualified_result"), C_SCOPE_ID_INVALID);
    CEntityId float_entity = c_test_find_local_entity(&auto_type_parse, S8("float_result"), C_SCOPE_ID_INVALID);
    CEntityId unqualified_lvalue_entity = c_test_find_local_entity(&auto_type_parse, S8("unqualified_lvalue_result"), C_SCOPE_ID_INVALID);
    BUSTER_TEST(arguments, array_entity.value < auto_type_parse.entity_count);
    BUSTER_TEST(arguments, function_entity.value < auto_type_parse.entity_count);
    BUSTER_TEST(arguments, qualified_entity.value < auto_type_parse.entity_count);
    BUSTER_TEST(arguments, float_entity.value < auto_type_parse.entity_count);
    BUSTER_TEST(arguments, unqualified_lvalue_entity.value < auto_type_parse.entity_count);
    CType* auto_array_type = array_entity.value < auto_type_parse.entity_count
                                 ? c_type_from_id(&auto_type_parse, auto_type_parse.entities[array_entity.value].type)
                                 : 0;
    CType* auto_array_element = auto_array_type ? c_type_from_id(&auto_type_parse, auto_array_type->element_type) : 0;
    CType* auto_function_type = function_entity.value < auto_type_parse.entity_count
                                    ? c_type_from_id(&auto_type_parse, auto_type_parse.entities[function_entity.value].type)
                                    : 0;
    CType* auto_function_value = auto_function_type ? c_type_from_id(&auto_type_parse, auto_function_type->element_type) : 0;
    CType* auto_function_return = auto_function_value ? c_type_from_id(&auto_type_parse, auto_function_value->return_type) : 0;
    CType* auto_qualified_type = qualified_entity.value < auto_type_parse.entity_count
                                     ? c_type_from_id(&auto_type_parse, auto_type_parse.entities[qualified_entity.value].type)
                                     : 0;
    CType* auto_float_type = float_entity.value < auto_type_parse.entity_count
                                 ? c_type_from_id(&auto_type_parse, auto_type_parse.entities[float_entity.value].type)
                                 : 0;
    CType* auto_unqualified_lvalue_type = unqualified_lvalue_entity.value < auto_type_parse.entity_count
                                             ? c_type_from_id(&auto_type_parse, auto_type_parse.entities[unqualified_lvalue_entity.value].type)
                                             : 0;
    BUSTER_TEST(arguments, auto_array_type && auto_array_type->kind == C_TYPE_POINTER);
    BUSTER_TEST(arguments, auto_array_element && auto_array_element->kind == C_TYPE_INT);
    BUSTER_TEST(arguments, auto_array_element && auto_array_element->kind != C_TYPE_ARRAY);
    BUSTER_TEST(arguments, auto_function_type && auto_function_type->kind == C_TYPE_POINTER);
    BUSTER_TEST(arguments, auto_function_value && auto_function_value->kind == C_TYPE_FUNCTION);
    BUSTER_TEST(arguments, auto_function_return && auto_function_return->kind == C_TYPE_INT);
    BUSTER_TEST(arguments, auto_qualified_type && auto_qualified_type->kind == C_TYPE_INT && auto_qualified_type->is_const);
    BUSTER_TEST(arguments, auto_float_type && auto_float_type->kind == C_TYPE_FLOAT);
    BUSTER_TEST(arguments, auto_unqualified_lvalue_type && auto_unqualified_lvalue_type->kind == C_TYPE_INT && !auto_unqualified_lvalue_type->is_const);

    CEntityId outer_value = C_ENTITY_ID_INVALID;
    CEntityId inner_value = C_ENTITY_ID_INVALID;
    for (u32 entity_index = 0; entity_index < auto_type_parse.entity_count; entity_index += 1)
    {
        CEntity* entity = &auto_type_parse.entities[entity_index];
        if (entity->kind != C_ENTITY_LOCAL || !string_equal(entity->name, S8("value")))
        {
            continue;
        }
        if (outer_value.value == C_ID_UNDERLYING_INVALID)
        {
            outer_value = (CEntityId){.value = entity_index};
        }
        else if (entity->scope.value != auto_type_parse.entities[outer_value.value].scope.value)
        {
            inner_value = (CEntityId){.value = entity_index};
        }
    }
    u32 auto_initializer_token = UINT32_MAX;
    for (u32 token_index = 0; token_index + 3 < auto_type_tokens.token_count; token_index += 1)
    {
        if (string_equal(c_token_spelling(auto_type_tokens.spelling_base, auto_type_tokens.tokens[token_index]), S8("__auto_type")) &&
            string_equal(c_token_spelling(auto_type_tokens.spelling_base, auto_type_tokens.tokens[token_index + 1]), S8("value")) &&
            auto_type_tokens.tokens[token_index + 2].kind == C_TOKEN_PUNCTUATOR &&
            string_equal(c_token_spelling(auto_type_tokens.spelling_base, auto_type_tokens.tokens[token_index + 2]), S8("=")) &&
            string_equal(c_token_spelling(auto_type_tokens.spelling_base, auto_type_tokens.tokens[token_index + 3]), S8("value")))
        {
            auto_initializer_token = token_index + 3;
            break;
        }
    }
    CEntityId initializer_entity = C_ENTITY_ID_INVALID;
    for (u32 use_index = 0; use_index < auto_type_parse.identifier_use_count; use_index += 1)
    {
        CIdentifierUse use = auto_type_parse.identifier_uses[use_index];
        if (use.token_index == auto_initializer_token)
        {
            initializer_entity = use.entity;
            break;
        }
    }
    BUSTER_TEST(arguments, outer_value.value < auto_type_parse.entity_count);
    BUSTER_TEST(arguments, inner_value.value < auto_type_parse.entity_count);
    BUSTER_TEST(arguments, auto_initializer_token < auto_type_tokens.token_count);
    BUSTER_TEST(arguments, initializer_entity.value == outer_value.value);
    BUSTER_TEST(arguments, initializer_entity.value != inner_value.value);
    CIRLowerResult auto_type_ir = c_lower_to_ir(arguments->arena, S8("auto-type-frontend.c"), auto_type_tokens, auto_type_parse, target_native);
    BUSTER_TEST(arguments, auto_type_ir.diagnostic_count == 0);
    if (auto_type_ir.program)
    {
        BUSTER_TEST(arguments, auto_type_ir.program->module_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(auto_type_ir.program, &auto_type_ir.program->modules[0]).error == IR_VALIDATION_NONE);
        IrFunction* auto_ir_shape_function = 0;
        IrModule* auto_ir_module = &auto_type_ir.program->modules[0];
        for (u32 function_index = 0; function_index < auto_ir_module->function_count; function_index += 1)
        {
            if (string_equal(auto_ir_module->functions[function_index].name, S8("auto_type_ir_shape_test")))
            {
                auto_ir_shape_function = &auto_ir_module->functions[function_index];
                break;
            }
        }
        bool found_auto_ir_integer = false;
        bool found_auto_ir_pointer = false;
        bool found_auto_ir_float = false;
        bool found_auto_ir_struct = false;
        if (auto_ir_shape_function)
        {
            for (u32 instruction_index = 0; instruction_index < auto_ir_shape_function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = &auto_ir_shape_function->instructions[instruction_index];
                if (instruction->opcode != IR_OPCODE_LOCAL)
                {
                    continue;
                }
                IrType* instruction_type = ir_type_from_id(&auto_type_ir.program->types, instruction->canonical_type);
                if (!instruction_type)
                {
                    continue;
                }
                found_auto_ir_integer |= instruction_type->kind == IR_TYPE_INTEGER && instruction_type->bit_width == 32 && instruction_type->is_signed;
                if (instruction_type->kind == IR_TYPE_POINTER && instruction_type->element_type.value < auto_type_ir.program->types.count)
                {
                    IrType* pointer_element = ir_type_from_id(&auto_type_ir.program->types, instruction_type->element_type);
                    found_auto_ir_pointer |= pointer_element && pointer_element->kind == IR_TYPE_INTEGER && pointer_element->bit_width == 32;
                }
                found_auto_ir_float |= instruction_type->kind == IR_TYPE_FLOAT && instruction_type->bit_width == 32;
                found_auto_ir_struct |= instruction_type->kind == IR_TYPE_STRUCT && instruction_type->field_count == 2;
            }
        }
        BUSTER_TEST(arguments, auto_ir_shape_function != 0);
        BUSTER_TEST(arguments, found_auto_ir_integer);
        BUSTER_TEST(arguments, found_auto_ir_pointer);
        BUSTER_TEST(arguments, found_auto_ir_float);
        BUSTER_TEST(arguments, found_auto_ir_struct);
    }

    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __auto_type value; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires an initialized data declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __auto_type left = 1, right = 2; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type may only be used with a single declarator"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __auto_type *value = (int *)0; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires a plain identifier as declarator"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __auto_type value[2] = { 1, 2 }; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires a plain identifier as declarator"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __auto_type value(void) = 0; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires a plain identifier as declarator"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { static __auto_type value = 1; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires an automatic object declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { extern __auto_type value = 1; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires an automatic object declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __thread __auto_type value = 1; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires an automatic object declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { typedef __auto_type value = 1; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type requires an automatic object declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { int __auto_type value = 1; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type cannot be combined with another type specifier"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(__auto_type parameter) { return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                S8("GNU __auto_type is supported only for one initialized automatic object declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("struct AutoTypeInvalidMember { __auto_type member; };\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                S8("GNU __auto_type is supported only for one initialized automatic object declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __auto_type value = value; return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNDECLARED_IDENTIFIER, S8("use of undeclared identifier 'value'"));
    c_test_auto_type_diagnostic(arguments, &result, S8("__auto_type value = 1;\nint f(void) { return 0; }\n"), C_PREPROCESS_DIALECT_GNU23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                S8("GNU __auto_type is supported only for one initialized automatic object declaration"));
    c_test_auto_type_diagnostic(arguments, &result, S8("int f(void) { __auto_type value = 1; return 0; }\n"), C_PREPROCESS_DIALECT_C23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("GNU __auto_type is only available in GNU dialects"));

    return result;
}

// Regression coverage for the 2026-08-08 stage-1 stray-global-write incident:
// a file-scope array bound of the form sizeof(table)/sizeof(table[0]) + 1,
// where table's element is a struct, folded through the type-prediction
// query's int guess to 2 before table's type mapped, so the next global was
// laid out inside the array. The bound must either resolve to the real count
// or defer to a later type-mapping pass, never fold a guessed size.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_global_array_sizeof_bound(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("typedef struct { const char *name; unsigned char builtin; } SizeofEntry;\n"
                                               "static const SizeofEntry sizeof_table[] = {\n"
                                               " { \"a\", 1 }, { \"b\", 2 }, { \"c\", 3 }, { \"d\", 4 },\n"
                                               " { \"e\", 5 }, { \"f\", 6 }, { \"g\", 7 },\n"
                                               "};\n"
                                               "static unsigned char sizeof_kinds[sizeof(sizeof_table) / sizeof(sizeof_table[0]) + 1];\n"
                                               "static unsigned char sizeof_whole[sizeof(sizeof_table)];\n"
                                               "unsigned int sizeof_probe(void)\n"
                                               "{ return (unsigned int)(sizeof(sizeof_kinds) + sizeof_kinds[2] + sizeof_whole[3]"
                                               " + sizeof_table[0].builtin); }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("global-array-sizeof-bound.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule* module = &ir.program->modules[0];
        IrType* table_type = 0;
        IrType* kinds_type = 0;
        IrType* whole_type = 0;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&ir.program->symbols, global->symbol);
            if (!symbol)
            {
                continue;
            }
            if (string_equal(symbol->link_name, S8("sizeof_table")))
            {
                table_type = ir_type_from_id(&ir.program->types, global->type);
            }
            else if (string_equal(symbol->link_name, S8("sizeof_kinds")))
            {
                kinds_type = ir_type_from_id(&ir.program->types, global->type);
            }
            else if (string_equal(symbol->link_name, S8("sizeof_whole")))
            {
                whole_type = ir_type_from_id(&ir.program->types, global->type);
            }
        }
        BUSTER_TEST(arguments, table_type && table_type->kind == IR_TYPE_ARRAY && table_type->element_count == 7);
        BUSTER_TEST(arguments, table_type && table_type->layout.resolved && table_type->layout.size == 112);
        BUSTER_TEST(arguments, kinds_type && kinds_type->kind == IR_TYPE_ARRAY && kinds_type->element_count == 8);
        BUSTER_TEST(arguments, kinds_type && kinds_type->layout.resolved && kinds_type->layout.size == 8);
        BUSTER_TEST(arguments, whole_type && whole_type->kind == IR_TYPE_ARRAY && whole_type->element_count == 112);
        BUSTER_TEST(arguments, whole_type && whole_type->layout.resolved && whole_type->layout.size == 112);
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// Regression coverage for the 2026-08-16 enum-constant miscompile: enumerator
// initializers were folded by the preprocessor's #if evaluator, which reads
// `sizeof` as an ordinary identifier and substitutes zero for it, so
// 1 << (sizeof(u16) * 8u - 2u) folded to 0 while the same expression folded
// correctly as an array bound, a bit-field width, and a case label. Every
// constant context that accepts sizeof/_Alignof must agree on the value.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_sizeof_constant_expression(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("typedef unsigned short u16;\n"
                                               "struct SizeofPoint { int x; int y; };\n"
                                               "enum SizeofConstants {\n"
                                               " SIZEOF_SHIFT = 1 << (sizeof(u16) * 8u - 2u),\n"
                                               " SIZEOF_WORD = sizeof(u16),\n"
                                               " SIZEOF_POINT = sizeof(struct SizeofPoint),\n"
                                               " SIZEOF_POINTER = sizeof(char *),\n"
                                               " SIZEOF_ARRAY = sizeof(u16[3]),\n"
                                               " SIZEOF_ALIGN = _Alignof(struct SizeofPoint),\n"
                                               " SIZEOF_DERIVED = SIZEOF_WORD + SIZEOF_POINT,\n"
                                               " SIZEOF_CAST = (int)(sizeof(struct SizeofPoint) / sizeof(int)),\n"
                                               "};\n"
                                               "struct SizeofBits { unsigned wide : sizeof(u16) * 8u - 2u; unsigned narrow : 2; };\n"
                                               "static unsigned char sizeof_bound[sizeof(u16) * 8u - 2u];\n"
                                               "static struct SizeofBits sizeof_bits;\n"
                                               "unsigned char sizeof_bound_probe(void) { return sizeof_bound[0] + (unsigned char)sizeof_bits.narrow; }\n"
                                               "int sizeof_dispatch(int value)\n"
                                               "{\n"
                                               " switch (value) {\n"
                                               " case (int)(sizeof(u16) * 8u - 2u): return 7;\n"
                                               " default: return 0;\n"
                                               " }\n"
                                               "}\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    bool found_shift = false;
    bool found_word = false;
    bool found_point = false;
    bool found_pointer = false;
    bool found_array = false;
    bool found_align = false;
    bool found_derived = false;
    bool found_cast = false;
    for (u32 entity_index = 0; entity_index < parse.entity_count; entity_index += 1)
    {
        CEntity* entity = &parse.entities[entity_index];
        if (entity->kind != C_ENTITY_ENUMERATOR || entity->constant_is_negative)
        {
            continue;
        }
        found_shift |= string_equal(entity->name, S8("SIZEOF_SHIFT")) && entity->constant_value == 16384;
        found_word |= string_equal(entity->name, S8("SIZEOF_WORD")) && entity->constant_value == 2;
        found_point |= string_equal(entity->name, S8("SIZEOF_POINT")) && entity->constant_value == 8;
        found_pointer |= string_equal(entity->name, S8("SIZEOF_POINTER")) && entity->constant_value == c_preprocess_detail(tokens)->data_layout.pointer.size;
        found_array |= string_equal(entity->name, S8("SIZEOF_ARRAY")) && entity->constant_value == 6;
        found_align |= string_equal(entity->name, S8("SIZEOF_ALIGN")) && entity->constant_value == 4;
        found_derived |= string_equal(entity->name, S8("SIZEOF_DERIVED")) && entity->constant_value == 10;
        found_cast |= string_equal(entity->name, S8("SIZEOF_CAST")) && entity->constant_value == 2;
    }
    BUSTER_TEST(arguments, found_shift);
    BUSTER_TEST(arguments, found_word);
    BUSTER_TEST(arguments, found_point);
    BUSTER_TEST(arguments, found_pointer);
    BUSTER_TEST(arguments, found_array);
    BUSTER_TEST(arguments, found_align);
    BUSTER_TEST(arguments, found_derived);
    BUSTER_TEST(arguments, found_cast);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("sizeof-constant-expression.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule* module = &ir.program->modules[0];
        IrType* bound_type = 0;
        IrType* bits_type = 0;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&ir.program->symbols, global->symbol);
            if (symbol && string_equal(symbol->link_name, S8("sizeof_bound")))
            {
                bound_type = ir_type_from_id(&ir.program->types, global->type);
            }
            else if (symbol && string_equal(symbol->link_name, S8("sizeof_bits")))
            {
                bits_type = ir_type_from_id(&ir.program->types, global->type);
            }
        }
        BUSTER_TEST(arguments, bound_type && bound_type->kind == IR_TYPE_ARRAY && bound_type->element_count == 14);
        // The bit-field width is folded during lowering, not at parse time, so
        // the resolved field layout is where a misfolded width would show.
        BUSTER_TEST(arguments, bits_type && bits_type->kind == IR_TYPE_STRUCT && bits_type->field_count == 2);
        if (bits_type && bits_type->field_count == 2)
        {
            BUSTER_TEST(arguments, bits_type->fields[0].is_bit_field && bits_type->fields[0].bit_width == 14 && bits_type->fields[0].bit_offset == 0);
            BUSTER_TEST(arguments, bits_type->fields[1].is_bit_field && bits_type->fields[1].bit_width == 2 && bits_type->fields[1].bit_offset == 14);
            BUSTER_TEST(arguments, bits_type->layout.resolved && bits_type->layout.size == 4);
        }
        IrInstruction* switch_instruction = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                if (function->instructions[instruction_index].opcode == IR_OPCODE_SWITCH)
                {
                    switch_instruction = &function->instructions[instruction_index];
                }
            }
        }
        BUSTER_TEST(arguments, switch_instruction != 0);
        if (switch_instruction)
        {
            BUSTER_TEST(arguments, switch_instruction->immediate_count == 1);
            BUSTER_TEST(arguments, switch_instruction->immediates[0] == 14);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// A type name may spell a function -- `int(void)` -- not just a pointer to one
// -- `int (*)(void)`. The type-name resolution accepted only the pointer
// declarator, so a bare function declarator left the whole type name unresolved
// and sizeof over it fell back to the prediction's int guess, folding 4 where
// GNU folds 1 (tests/basic_c_sizeof_function_designator.c is the runtime
// fixture). The array bounds below are where a misfolded size shows in the IR.
// The variadic spelling `int(int, ...)` is one of them: the ellipsis is a
// parameter of the type name, not of the declarator the bound belongs to, so
// spelled_variadic below must measure like every other function type.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_sizeof_function_type_name(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("typedef int NamedFunction(void);\n"
                                               "static unsigned char spelled_function[sizeof(int(void))];\n"
                                               "static unsigned char spelled_unprototyped[sizeof(int()) + 1];\n"
                                               "static unsigned char spelled_variadic[sizeof(int(int, ...)) + 2];\n"
                                               "static unsigned char spelled_typedef[sizeof(NamedFunction) + 3];\n"
                                               "static unsigned char spelled_pointer[sizeof(int (*)(void))];\n"
                                               "static unsigned char spelled_parameter[sizeof(int (*)(int(void)))];\n"
                                               "static unsigned char spelled_function_parameter[sizeof(int(int (*)(void))) + 4];\n"
                                               "unsigned int spelled_probe(void)\n"
                                               "{ return (unsigned int)(sizeof(int(void)) + spelled_function[0] + spelled_pointer[0]); }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("sizeof-function-type-name.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule* module = &ir.program->modules[0];
        // Every bound is `sizeof(...) + <its index>`, so one table walk names
        // both the global and the count its sizeof must have folded to.
        struct
        {
            String8 name;
            u64 element_count;
        } expected[] = {
            {S8("spelled_function"), 1},
            {S8("spelled_unprototyped"), 2},
            {S8("spelled_variadic"), 3},
            {S8("spelled_typedef"), 4},
            {S8("spelled_pointer"), ir.program->data_layout.pointer.size},
            {S8("spelled_parameter"), ir.program->data_layout.pointer.size},
            {S8("spelled_function_parameter"), 5},
        };
        for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(expected); expected_index += 1)
        {
            IrType* global_type = 0;
            for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
            {
                IrGlobal* global = module->globals + global_index;
                IrSymbol* symbol = ir_symbol_from_id(&ir.program->symbols, global->symbol);
                if (symbol && string_equal(symbol->link_name, expected[expected_index].name))
                {
                    global_type = ir_type_from_id(&ir.program->types, global->type);
                }
            }
            BUSTER_TEST(arguments, global_type && global_type->kind == IR_TYPE_ARRAY);
            BUSTER_TEST(arguments, global_type && global_type->element_count == expected[expected_index].element_count);
            BUSTER_TEST(arguments, global_type && global_type->layout.resolved && global_type->layout.size == expected[expected_index].element_count);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// An ellipsis makes a declarator variadic only where it is a parameter of that
// declarator's own parameter list -- the one top-level parenthesis group. A
// deeper one belongs to a nested type name: a parameter's own parameter list,
// or a function type named inside an array bound. Both scans that answer this
// from tokens alone -- the whole-declaration one in c_parse_ast and
// c_parser_scan_declarator, which rescans each declarator of a comma-separated
// list -- used to take any ellipsis in range, so fixed_arity and list_fixed
// below came out variadic and their calls skipped the arity check.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_declarator_ellipsis_depth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("int fixed_arity(int (*callback)(int, ...));\n"
                                               "int truly_variadic(int first, ...);\n"
                                               "int (*pointer_variadic)(int, ...);\n"
                                               "typedef int FixedTypedef(int (*)(int, ...));\n"
                                               "static unsigned char bound_over_variadic[sizeof(int(int, ...)) + 2];\n"
                                               "int list_fixed(int (*)(int, ...)), list_variadic(int, ...);\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    struct
    {
        String8 name;
        CDeclarationKind kind;
        CTypeKind type_kind;
        bool is_variadic;
    } expected[] = {
        {S8("fixed_arity"), C_DECLARATION_FUNCTION, C_TYPE_FUNCTION, false},
        {S8("truly_variadic"), C_DECLARATION_FUNCTION, C_TYPE_FUNCTION, true},
        // The pointed-at function's parameter list is this declarator's own, so
        // the flag is true here and the object type is a pointer either way.
        {S8("pointer_variadic"), C_DECLARATION_OBJECT, C_TYPE_POINTER, true},
        {S8("FixedTypedef"), C_DECLARATION_TYPEDEF, C_TYPE_FUNCTION, false},
        {S8("bound_over_variadic"), C_DECLARATION_OBJECT, C_TYPE_ARRAY, false},
        {S8("list_fixed"), C_DECLARATION_FUNCTION, C_TYPE_FUNCTION, false},
        {S8("list_variadic"), C_DECLARATION_FUNCTION, C_TYPE_FUNCTION, true},
    };
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(expected); expected_index += 1)
    {
        CDeclaration* declaration = 0;
        for (u32 declaration_index = 0; declaration_index < parse.declaration_count; declaration_index += 1)
        {
            if (string_equal(parse.declarations[declaration_index].name, expected[expected_index].name))
            {
                declaration = parse.declarations + declaration_index;
            }
        }
        BUSTER_TEST(arguments, declaration != 0);
        if (!declaration)
        {
            continue;
        }
        BUSTER_TEST(arguments, declaration->kind == expected[expected_index].kind);
        BUSTER_TEST(arguments, declaration->is_variadic == expected[expected_index].is_variadic);
        CType* type = declaration->type.value < parse.type_count ? parse.types + declaration->type.value : 0;
        BUSTER_TEST(arguments, type != 0 && type->kind == expected[expected_index].type_kind);
        // The declaration flag is what seeds the function type, so the type is
        // where a stray ellipsis actually reaches the ABI and the arity check.
        BUSTER_TEST(arguments, !type || type->kind != C_TYPE_FUNCTION || type->is_variadic == expected[expected_index].is_variadic);
        // A pointer to a variadic function still points at one: the pointee is
        // built by the type machine's own parameter walk, not by these scans.
        if (type && type->kind == C_TYPE_POINTER)
        {
            CType* pointee = type->element_type.value < parse.type_count ? parse.types + type->element_type.value : 0;
            BUSTER_TEST(arguments, pointee != 0 && pointee->kind == C_TYPE_FUNCTION && pointee->is_variadic);
        }
    }
    scratch_end(temporary);
    return result;
}

// The function type the first call in `name` takes its callee from: the type
// of the IR_OPCODE_FUNCTION reference for a direct call, and the pointee of
// the callee pointer for an indirect one.
BUSTER_GLOBAL_LOCAL IrType* c_test_first_call_callee_type(IrProgram* program, IrModule* module, String8 name)
{
    IrType* result = 0;
    for (u32 function_index = 0; function_index < module->function_count && !result; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (!string_equal(function->name, name))
        {
            continue;
        }
        for (u32 instruction_index = 0; instruction_index < function->instruction_count && !result; instruction_index += 1)
        {
            IrInstruction* instruction = function->instructions + instruction_index;
            if (instruction->opcode != IR_OPCODE_CALL || !instruction->operand_count ||
                instruction->operands[0].value >= function->value_count)
            {
                continue;
            }
            IrType* callee = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
            result = callee && callee->kind == IR_TYPE_POINTER ? ir_type_from_id(&program->types, callee->element_type) : callee;
        }
    }

    return result;
}

// Whether a call-site function type names exactly these parameter types.
BUSTER_GLOBAL_LOCAL bool c_test_call_parameters_are(IrProgram* program, IrType* call_type, IrTypeKind* kinds, u32* bit_widths, u32 parameter_count)
{
    bool result = call_type && call_type->kind == IR_TYPE_FUNCTION && call_type->parameter_count == parameter_count;
    for (u32 parameter_index = 0; parameter_index < parameter_count && result; parameter_index += 1)
    {
        IrType* parameter = ir_type_from_id(&program->types, call_type->parameter_types[parameter_index]);
        result = parameter && parameter->kind == kinds[parameter_index] && parameter->bit_width == bit_widths[parameter_index];
    }

    return result;
}

/* Before C23 a function declared `()` has no parameter list, so a call to it
   supplies one: the arguments take the default argument promotions and the
   call site -- not the declaration -- says where they go. The IR shows that
   directly, because the reference the call takes to the symbol carries the
   call's own signature (see IrType.is_unprototyped) with the `...` that makes
   a System V caller set the AL vector count for a callee that turns out to be
   variadic. These calls used to be refused outright as "could not prepare C
   calls" whenever no later prototype in the same unit happened to replace the
   declaration; that refusal now survives only where the callee really does
   have a parameter list to overrun, and says so. */
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_unprototyped_call_arguments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    String8 source = S8("void die();\n"
                        "int side();\n"
                        "double mix();\n"
                        "int (*through_pointer)();\n"
                        "int call_one(int x) { die(x); return 0; }\n"
                        "int call_two(int x) { return side(x, x + 1); }\n"
                        "int call_promotes(float f, char c, short s) { return (int)mix(f, c, s); }\n"
                        "int call_none(void) { die(); return 0; }\n"
                        "int call_pointer(int x) { return through_pointer(x, x); }\n");
    CPreprocessResult tokens = {0};
    CParseResult parse = {0};
    CIRLowerResult ir = c_test_lower_source(temporary.arena, source, S8("unprototyped-call.c"), target_native, &tokens, &parse);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule* module = &ir.program->modules[0];
        IrType* one = c_test_first_call_callee_type(ir.program, module, S8("call_one"));
        IrType* two = c_test_first_call_callee_type(ir.program, module, S8("call_two"));
        IrType* promotes = c_test_first_call_callee_type(ir.program, module, S8("call_promotes"));
        IrType* none = c_test_first_call_callee_type(ir.program, module, S8("call_none"));
        IrType* pointer = c_test_first_call_callee_type(ir.program, module, S8("call_pointer"));
        IrTypeKind one_kinds[] = {IR_TYPE_INTEGER};
        u32 one_widths[] = {32};
        BUSTER_TEST(arguments, c_test_call_parameters_are(ir.program, one, one_kinds, one_widths, BUSTER_ARRAY_LENGTH(one_kinds)));
        BUSTER_TEST(arguments, one && one->is_variadic && !one->is_unprototyped);
        IrTypeKind two_kinds[] = {IR_TYPE_INTEGER, IR_TYPE_INTEGER};
        u32 two_widths[] = {32, 32};
        BUSTER_TEST(arguments, c_test_call_parameters_are(ir.program, two, two_kinds, two_widths, BUSTER_ARRAY_LENGTH(two_kinds)));
        // The default argument promotions, read off the call site: float
        // widens to double, char and short to int.
        IrTypeKind promotes_kinds[] = {IR_TYPE_FLOAT, IR_TYPE_INTEGER, IR_TYPE_INTEGER};
        u32 promotes_widths[] = {64, 32, 32};
        BUSTER_TEST(arguments, c_test_call_parameters_are(ir.program, promotes, promotes_kinds, promotes_widths, BUSTER_ARRAY_LENGTH(promotes_kinds)));
        // An empty argument list has nothing to name, so the call keeps the
        // declaration's own type -- and that type is the marked one.
        BUSTER_TEST(arguments, none && none->kind == IR_TYPE_FUNCTION && !none->parameter_count && !none->is_variadic && none->is_unprototyped);
        // Through a pointer the callee is the same call-site signature, one
        // indirection down.
        BUSTER_TEST(arguments, c_test_call_parameters_are(ir.program, pointer, two_kinds, two_widths, BUSTER_ARRAY_LENGTH(two_kinds)));
        BUSTER_TEST(arguments, pointer && pointer->is_variadic);
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

/* C23 made `()` mean `(void)`, so the same calls are a constraint violation
   there, and every dialect refuses a call that overruns a real parameter list.
   Both used to report only that the call could not be prepared, which named
   neither the callee nor the count. */
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_call_arity_diagnostics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    struct
    {
        String8 source;
        CPreprocessDialect dialect;
        String8 message;
    } expected[] = {
        {S8("void die();\nint call_one(int x) { die(x); return 0; }\n"), C_PREPROCESS_DIALECT_C23,
         S8("in function 'call_one': too many arguments in the call to 'die': it declares no parameters")},
        {S8("void none(void);\nint call_none(int x) { none(x); return 0; }\n"), C_PREPROCESS_DIALECT_GNU17,
         S8("in function 'call_none': too many arguments in the call to 'none': it declares no parameters")},
        {S8("void two(int a, int b);\nint call_three(int x) { two(x, x, x); return 0; }\n"), C_PREPROCESS_DIALECT_GNU17,
         S8("in function 'call_three': too many arguments in the call to 'two': it declares 2 parameters")},
        {S8("void two(int a, int b);\nint call_partial(int x) { two(x); return 0; }\n"), C_PREPROCESS_DIALECT_GNU17,
         S8("in function 'call_partial': too few arguments in the call to 'two': it declares 2 parameters")},
        {S8("void tail(int a, ...);\nint call_empty(void) { tail(); return 0; }\n"), C_PREPROCESS_DIALECT_GNU17,
         S8("in function 'call_empty': too few arguments in the call to 'tail': it declares at least 1 parameter")},
        // An indirect callee has no name to resolve, so its shortfall used to
        // travel to IR validation and come back as a code generation failure
        // naming an opcode.
        {S8("void (*through)(int a, int b);\nint call_short(int x) { through(x); return 0; }\n"), C_PREPROCESS_DIALECT_GNU17,
         S8("in function 'call_short': too few arguments in the call to '<function pointer>': it declares 2 parameters")},
        {S8("void (*through)(int a, int b);\nint call_long(int x) { through(x, x, x); return 0; }\n"), C_PREPROCESS_DIALECT_GNU17,
         S8("in function 'call_long': too many arguments in the call to '<function pointer>': it declares 2 parameters")},
    };
    for (u32 expected_index = 0; expected_index < BUSTER_ARRAY_LENGTH(expected); expected_index += 1)
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult tokens = c_preprocess(temporary.arena, expected[expected_index].source,
                                                (CPreprocessOptions){
                                                    .target = target_native,
                                                    .data_layout = target_data_layout(target_native),
                                                    .dialect = expected[expected_index].dialect,
                                                });
        CParseResult parse = c_parse(temporary.arena, tokens);
        CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("call-arity.c"), tokens, parse, target_native);
        BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, ir.diagnostic_count == 1);
        if (ir.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, ir.diagnostics[0].message, expected[expected_index].message);
        }
        scratch_end(temporary);
    }

    return result;
}

/* Function-body sizeof over an expression operand must fold through the resolved
   operand types with the usual arithmetic conversions, never through the
   type-prediction guess: narrow operands promote to int, shifts keep the promoted
   left operand, assignments keep the left operand unpromoted, and a nested sizeof
   contributes size_t. Each probe function's IR must contain the correctly folded
   constant and must not contain the value the prediction guess used to fold. */
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_function_body_sizeof_expression(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    // Lower for the native target and an LLP64 one: the size_type/ptrdiff selection
    // reads the program data layout, so a Windows-target lowering must fold the
    // nested-sizeof probe through a 64-bit size_t even though its unsigned long is 32-bit.
    Target targets[2] = {target_native, target_native};
    targets[1].os = OPERATING_SYSTEM_WINDOWS;
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        Target probe_target = targets[target_index];
        CPreprocessResult tokens =
            c_preprocess(temporary.arena,
                         S8("char sizeof_take_char(void) { return 1; }\n"
                            "static short sizeof_global_short;\n"
                            "struct SizeofPromote { char first; short second; };\n"
                            "unsigned int sizeof_fold_short_add(void) { short value; return (unsigned int)sizeof(value + value); }\n"
                            "unsigned int sizeof_fold_char_negate(void) { char value; return (unsigned int)sizeof(-value); }\n"
                            "unsigned int sizeof_fold_bool_not(void) { _Bool value; return (unsigned int)sizeof(~value); }\n"
                            "unsigned int sizeof_fold_shift_left_operand(void) { int left; long long right;"
                            " return (unsigned int)sizeof(left << right); }\n"
                            "unsigned int sizeof_fold_assign_left(void) { short target; long long source;"
                            " return (unsigned int)sizeof(target = source); }\n"
                            "unsigned int sizeof_fold_compound_assign_left(void) { char target; long long source;"
                            " return (unsigned int)sizeof(target += source); }\n"
                            "unsigned int sizeof_fold_conditional_promotes(void) { _Bool which; char value;"
                            " return (unsigned int)sizeof(which ? value : value); }\n"
                            "unsigned int sizeof_fold_call_add(void)"
                            " { return (unsigned int)sizeof(sizeof_take_char() + sizeof_take_char()); }\n"
                            "unsigned int sizeof_fold_nested_sizeof(void) { int value; return (unsigned int)sizeof(sizeof(value) + value); }\n"
                            "unsigned int sizeof_fold_member_add(void) { struct SizeofPromote pair;"
                            " return (unsigned int)sizeof(pair.first + pair.second); }\n"
                            "unsigned int sizeof_fold_element_add(void) { short table[4];"
                            " return (unsigned int)sizeof(table[0] + table[1]); }\n"
                            "unsigned int sizeof_fold_cast_add(void) { long long wide; return (unsigned int)sizeof((char)wide + 1); }\n"
                            "unsigned int sizeof_fold_global_add(void)"
                            " { return (unsigned int)sizeof(sizeof_global_short + sizeof_global_short); }\n"),
                         (CPreprocessOptions){
                             .target = probe_target,
                             .data_layout = target_data_layout(probe_target),
                         });
        CParseResult parse = c_parse(temporary.arena, tokens);
        CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("function-body-sizeof-expression.c"), tokens, parse, probe_target);
        BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, ir.diagnostic_count == 0);
        BUSTER_TEST(arguments, ir.program != 0);
        if (ir.program)
        {
            struct
            {
                String8 name;
                u64 folded;
                u64 guessed;
            } probes[] = {
                {S8_INITIALIZER("sizeof_fold_short_add"), 4, 2},
                {S8_INITIALIZER("sizeof_fold_char_negate"), 4, 1},
                {S8_INITIALIZER("sizeof_fold_bool_not"), 4, 1},
                {S8_INITIALIZER("sizeof_fold_shift_left_operand"), 4, 8},
                {S8_INITIALIZER("sizeof_fold_assign_left"), 2, 8},
                {S8_INITIALIZER("sizeof_fold_compound_assign_left"), 1, 8},
                {S8_INITIALIZER("sizeof_fold_conditional_promotes"), 4, 1},
                {S8_INITIALIZER("sizeof_fold_call_add"), 4, 1},
                {S8_INITIALIZER("sizeof_fold_nested_sizeof"), 8, 4},
                {S8_INITIALIZER("sizeof_fold_member_add"), 4, 2},
                {S8_INITIALIZER("sizeof_fold_element_add"), 4, 2},
                {S8_INITIALIZER("sizeof_fold_cast_add"), 4, 1},
                {S8_INITIALIZER("sizeof_fold_global_add"), 4, 2},
            };
            IrModule* module = &ir.program->modules[0];
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(probes); probe_index += 1)
            {
                bool found_folded = false;
                bool found_guessed = false;
                bool found_function = false;
                for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
                {
                    IrFunction* function = &module->functions[function_index];
                    if (!string_equal(function->name, probes[probe_index].name))
                    {
                        continue;
                    }
                    found_function = true;
                    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                    {
                        IrInstruction instruction = function->instructions[instruction_index];
                        if (instruction.opcode != IR_OPCODE_CONSTANT_INTEGER)
                        {
                            continue;
                        }
                        found_folded |= instruction.immediates[0] == probes[probe_index].folded;
                        found_guessed |= instruction.immediates[0] == probes[probe_index].guessed;
                    }
                }
                BUSTER_TEST(arguments, found_function);
                BUSTER_TEST(arguments, found_folded);
                BUSTER_TEST(arguments, !found_guessed);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
        }
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_control_flow(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena control_flow_temporary = scratch_begin(0, 0);
    CPreprocessResult if_tokens = c_preprocess(control_flow_temporary.arena,
                                               S8("int choose(int value) {\n"
                                                  "    int result = 1;\n"
                                                  "    if (value) {\n"
                                                  "        result = 2;\n"
                                                  "    } else {\n"
                                                  "        result = 3;\n"
                                                  "    }\n"
                                                  "    return result;\n"
                                                  "}\n"
                                                  "int select_return(int value) {\n"
                                                  "    switch (value) {\n"
                                                  "    case 1: return 10;\n"
                                                  "    default: return 20;\n"
                                                  "    }\n"
                                                  "}\n"),
                                               (CPreprocessOptions){0});
    CParseResult if_parse = c_parse(control_flow_temporary.arena, if_tokens);
    BUSTER_TEST(arguments, if_parse.diagnostic_count == 0);
    CIRLowerResult if_ir = c_lower_to_ir(control_flow_temporary.arena, S8("if.c"), if_tokens, if_parse, target_native);
    BUSTER_TEST(arguments, if_ir.diagnostic_count == 0);
    if (if_ir.program)
    {
        IrModule* module = &if_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_count = 0;
        u32 branch_if_count = 0;
        u32 return_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            branch_count += opcode == IR_OPCODE_BRANCH;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
            return_count += opcode == IR_OPCODE_RETURN;
        }
        BUSTER_TEST(arguments, function->block_count == 4);
        BUSTER_TEST(arguments, branch_if_count == 1);
        BUSTER_TEST(arguments, branch_count == 2);
        BUSTER_TEST(arguments, return_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(if_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult else_if_tokens = c_preprocess(control_flow_temporary.arena,
                                                    S8("int classify(int value) {\n"
                                                       "    int result = 0;\n"
                                                       "    if (value < 0) {\n"
                                                       "        result = -1;\n"
                                                       "    } else if (value > 0) {\n"
                                                       "        result = 1;\n"
                                                       "    } else {\n"
                                                       "        result = 2;\n"
                                                       "    }\n"
                                                       "    return result;\n"
                                                       "}\n"),
                                                    (CPreprocessOptions){0});
    CParseResult else_if_parse = c_parse(control_flow_temporary.arena, else_if_tokens);
    BUSTER_TEST(arguments, else_if_parse.diagnostic_count == 0);
    CIRLowerResult else_if_ir = c_lower_to_ir(control_flow_temporary.arena, S8("else_if.c"), else_if_tokens, else_if_parse, target_native);
    BUSTER_TEST(arguments, else_if_ir.diagnostic_count == 0);
    if (else_if_ir.program)
    {
        IrModule* module = &else_if_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            branch_if_count += function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, function->block_count == 7);
        BUSTER_TEST(arguments, branch_if_count == 2);
        BUSTER_TEST(arguments, ir_validate_canonical_module(else_if_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult while_tokens = c_preprocess(control_flow_temporary.arena,
                                                  S8("int count_down(int value) {\n"
                                                     "    while (value) {\n"
                                                     "        if (value == 2) {\n"
                                                     "            break;\n"
                                                     "        }\n"
                                                     "        value = value - 1;\n"
                                                     "        continue;\n"
                                                     "    }\n"
                                                     "    return value;\n"
                                                     "}\n"),
                                                  (CPreprocessOptions){0});
    CParseResult while_parse = c_parse(control_flow_temporary.arena, while_tokens);
    BUSTER_TEST(arguments, while_parse.diagnostic_count == 0);
    CIRLowerResult while_ir = c_lower_to_ir(control_flow_temporary.arena, S8("while.c"), while_tokens, while_parse, target_native);
    BUSTER_TEST(arguments, while_ir.diagnostic_count == 0);
    if (while_ir.program)
    {
        IrModule* module = &while_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_count = 0;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            branch_count += opcode == IR_OPCODE_BRANCH;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, function->block_count == 6);
        BUSTER_TEST(arguments, branch_if_count == 2);
        BUSTER_TEST(arguments, branch_count == 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(while_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult for_tokens = c_preprocess(control_flow_temporary.arena,
                                                S8("int sum_to(int count) {\n"
                                                   "    int sum = 0;\n"
                                                   "    for (int index = 0; index < count;"
                                                   " index++) {\n"
                                                   "        sum += index;\n"
                                                   "    }\n"
                                                   "    return sum;\n"
                                                   "}\n"),
                                                (CPreprocessOptions){0});
    CParseResult for_parse = c_parse(control_flow_temporary.arena, for_tokens);
    BUSTER_TEST(arguments, for_parse.diagnostic_count == 0);
    CIRLowerResult for_ir = c_lower_to_ir(control_flow_temporary.arena, S8("for.c"), for_tokens, for_parse, target_native);
    BUSTER_TEST(arguments, for_ir.diagnostic_count == 0);
    if (for_ir.program)
    {
        IrModule* module = &for_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_count = 0;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            branch_count += opcode == IR_OPCODE_BRANCH;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, function->block_count == 5);
        BUSTER_TEST(arguments, branch_if_count == 1);
        BUSTER_TEST(arguments, branch_count == 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(for_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult for_preheader_tokens = c_preprocess(control_flow_temporary.arena,
                                                          S8("int sum_after_selection(int count) {\n"
                                                             "    int sum = 0;\n"
                                                             "    sum = count ? 1 : 2;\n"
                                                             "    for (int index = 0; index < count;"
                                                             " index += 1) {\n"
                                                             "        sum += index;\n"
                                                             "    }\n"
                                                             "    return sum;\n"
                                                             "}\n"),
                                                          (CPreprocessOptions){0});
    CParseResult for_preheader_parse = c_parse(control_flow_temporary.arena, for_preheader_tokens);
    BUSTER_TEST(arguments, for_preheader_parse.diagnostic_count == 0);
    CIRLowerResult for_preheader_ir =
        c_lower_to_ir(control_flow_temporary.arena, S8("for-preheader.c"), for_preheader_tokens, for_preheader_parse, target_native);
    BUSTER_TEST(arguments, for_preheader_ir.diagnostic_count == 0);
    if (for_preheader_ir.program)
    {
        IrModule* module = &for_preheader_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 1);
        BUSTER_TEST(arguments, module->functions[0].block_count >= 7);
        BUSTER_TEST(arguments, ir_validate_canonical_module(for_preheader_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult single_body_tokens = c_preprocess(control_flow_temporary.arena,
                                                        S8("int single_bodies(int value) {\n"
                                                           "    int index = 0;\n"
                                                           "    if (value) value = 3;"
                                                           " else value = 2;\n"
                                                           "    while (value > 1)"
                                                           " value = value - 1;\n"
                                                           "    do value = value + 1;"
                                                           " while (value < 2);\n"
                                                           "    for (index = 0; index < 2;"
                                                           " index = index + 1)"
                                                           " value = value + index;\n"
                                                           "    return value;\n"
                                                           "}\n"),
                                                        (CPreprocessOptions){0});
    CParseResult single_body_parse = c_parse(control_flow_temporary.arena, single_body_tokens);
    BUSTER_TEST(arguments, single_body_parse.diagnostic_count == 0);
    CIRLowerResult single_body_ir = c_lower_to_ir(control_flow_temporary.arena, S8("single_bodies.c"), single_body_tokens, single_body_parse, target_native);
    BUSTER_TEST(arguments, single_body_ir.diagnostic_count == 0);
    if (single_body_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(single_body_ir.program, &single_body_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult switch_tokens = c_preprocess(control_flow_temporary.arena,
                                                   S8("int select_value(int value) {\n"
                                                      "    int result = 0;\n"
                                                      "    switch (value) {\n"
                                                      "    case 1:\n"
                                                      "        result = 10;\n"
                                                      "        break;\n"
                                                      "    case 2:\n"
                                                      "        result = 20;\n"
                                                      "    default:\n"
                                                      "        result += 1;\n"
                                                      "        break;\n"
                                                      "    }\n"
                                                      "    return result;\n"
                                                      "}\n"),
                                                   (CPreprocessOptions){0});
    CParseResult switch_parse = c_parse(control_flow_temporary.arena, switch_tokens);
    BUSTER_TEST(arguments, switch_parse.diagnostic_count == 0);
    CIRLowerResult switch_ir = c_lower_to_ir(control_flow_temporary.arena, S8("switch.c"), switch_tokens, switch_parse, target_native);
    BUSTER_TEST(arguments, switch_ir.diagnostic_count == 0);
    if (switch_ir.program)
    {
        IrModule* module = &switch_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 switch_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            switch_count += function->instructions[instruction_index].opcode == IR_OPCODE_SWITCH;
        }
        BUSTER_TEST(arguments, function->block_count == 5);
        BUSTER_TEST(arguments, switch_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(switch_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult ternary_case_tokens = c_preprocess(control_flow_temporary.arena,
                                                         S8("int ternary_case(int value) {\n"
                                                            "    int result = 0;\n"
                                                            "    switch (value) {\n"
                                                            "    case 1 ? 2 : 3:\n"
                                                            "        result = 7;\n"
                                                            "        break;\n"
                                                            "    default:\n"
                                                            "        result = 1;\n"
                                                            "        break;\n"
                                                            "    }\n"
                                                            "    return result;\n"
                                                            "}\n"),
                                                         (CPreprocessOptions){0});
    CParseResult ternary_case_parse = c_parse(control_flow_temporary.arena, ternary_case_tokens);
    BUSTER_TEST(arguments, ternary_case_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, ternary_case_parse.diagnostic_count == 0);
    CIRLowerResult ternary_case_ir =
        c_lower_to_ir(control_flow_temporary.arena, S8("ternary-case.c"), ternary_case_tokens, ternary_case_parse, target_native);
    BUSTER_TEST(arguments, ternary_case_ir.diagnostic_count == 0);
    if (ternary_case_ir.program)
    {
        IrModule* module = &ternary_case_ir.program->modules[0];
        IrFunction* function = module->functions;
        IrInstruction* switch_instruction = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            if (function->instructions[instruction_index].opcode == IR_OPCODE_SWITCH)
            {
                switch_instruction = &function->instructions[instruction_index];
            }
        }
        BUSTER_TEST(arguments, switch_instruction != 0);
        if (switch_instruction)
        {
            // The label constant is the ternary's selected arm, not its
            // condition or the colon-truncated prefix.
            BUSTER_TEST(arguments, switch_instruction->immediate_count == 1);
            BUSTER_TEST(arguments, switch_instruction->immediates[0] == 2);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(ternary_case_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult case_range_tokens = c_preprocess(control_flow_temporary.arena,
                                                       S8("int range_dispatch(int value) {\n"
                                                          "    int result = 0;\n"
                                                          "    switch (value) {\n"
                                                          "    case -3 ... -1:\n"
                                                          "        result = 10;\n"
                                                          "        break;\n"
                                                          "    case 0 ... 2:\n"
                                                          "        result = 20;\n"
                                                          "    case 3:\n"
                                                          "        result += 30;\n"
                                                          "        break;\n"
                                                          "    default:\n"
                                                          "        result = -1;\n"
                                                          "        break;\n"
                                                          "    }\n"
                                                          "    return result;\n"
                                                          "}\n"
                                                          "int range_huge(unsigned int value) {\n"
                                                          "    switch (value) {\n"
                                                          "    case 0u ... 0xffffffffu: return 1;\n"
                                                          "    default: return 2;\n"
                                                          "    }\n"
                                                          "}\n"
                                                          "int read_range_value(int *calls, int value) {\n"
                                                          "    *calls += 1;\n"
                                                          "    return value;\n"
                                                          "}\n"
                                                          "int nested_range(int *calls, int value) {\n"
                                                          "    switch (read_range_value(calls, value)) {\n"
                                                          "    case 1 ... 2:\n"
                                                          "        switch (value - 1) {\n"
                                                          "        case 0 ... 0: return 1;\n"
                                                          "        default: return 2;\n"
                                                          "        }\n"
                                                          "    default: return 3;\n"
                                                          "    }\n"
                                                          "}\n"),
                                                       (CPreprocessOptions){
                                                           .target = target_native,
                                                           .data_layout = target_data_layout(target_native),
                                                           .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                       });
    CParseResult case_range_parse = c_parse(control_flow_temporary.arena, case_range_tokens);
    CIRLowerResult case_range_ir =
        c_lower_to_ir(control_flow_temporary.arena, S8("case-range.c"), case_range_tokens, case_range_parse, target_native);
    BUSTER_TEST(arguments, case_range_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, case_range_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, case_range_ir.diagnostic_count == 0);
    if (case_range_ir.program)
    {
        IrModule* module = &case_range_ir.program->modules[0];
        IrFunction* dispatch = 0;
        IrFunction* huge = 0;
        IrFunction* nested = 0;
        u32 switch_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (string_equal(function->name, S8("range_dispatch")))
            {
                dispatch = function;
            }
            else if (string_equal(function->name, S8("range_huge")))
            {
                huge = function;
            }
            else if (string_equal(function->name, S8("nested_range")))
            {
                nested = function;
            }
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                switch_count += function->instructions[instruction_index].opcode == IR_OPCODE_SWITCH;
            }
        }
        BUSTER_TEST(arguments, dispatch != 0);
        BUSTER_TEST(arguments, huge != 0);
        BUSTER_TEST(arguments, nested != 0);
        if (dispatch)
        {
            u32 branch_if_count = 0;
            u32 binary_count = 0;
            for (u32 instruction_index = 0; instruction_index < dispatch->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = dispatch->instructions[instruction_index].opcode;
                branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
                binary_count += opcode == IR_OPCODE_BINARY;
            }
            BUSTER_TEST(arguments, branch_if_count >= 5);
            BUSTER_TEST(arguments, binary_count >= 5);
            BUSTER_TEST(arguments, dispatch->instruction_count < 100);
        }
        if (huge)
        {
            BUSTER_TEST(arguments, huge->instruction_count < 100);
        }
        if (nested)
        {
            u32 call_count = 0;
            for (u32 instruction_index = 0; instruction_index < nested->instruction_count; instruction_index += 1)
            {
                call_count += nested->instructions[instruction_index].opcode == IR_OPCODE_CALL;
            }
            BUSTER_TEST(arguments, call_count == 1);
        }
        BUSTER_TEST(arguments, switch_count == 0);
        BUSTER_TEST(arguments, ir_validate_canonical_module(case_range_ir.program, module).error == IR_VALIDATION_NONE);
    }
    c_test_case_range_lower_diagnostic(arguments, &result,
                                       S8("int malformed(int value) { switch (value) { case 1 ... 2 ... 3: return 0; } return 1; }\n"),
                                       S8("in function 'malformed': malformed GNU case range"));
    c_test_case_range_lower_diagnostic(arguments, &result,
                                       S8("int nonconstant_low(int value) { switch (value) { case value ... 2: return 0; } return 1; }\n"),
                                       S8("in function 'nonconstant_low': case range lower bound is not an integer constant expression"));
    c_test_case_range_lower_diagnostic(arguments, &result,
                                       S8("int nonconstant_high(int value) { switch (value) { case 1 ... value: return 0; } return 1; }\n"),
                                       S8("in function 'nonconstant_high': case range upper bound is not an integer constant expression"));
    c_test_case_range_lower_diagnostic(arguments, &result,
                                       S8("int reversed(int value) { switch (value) { case 3 ... 1: return 0; } return 1; }\n"),
                                       S8("in function 'reversed': case range is not ordered after conversion to the switch type"));
    c_test_case_range_lower_diagnostic(arguments, &result,
                                       S8("int singleton_overlap(int value) { switch (value) { case 1 ... 3: return 0; case 3: return 1; } return 2; }\n"),
                                       S8("in function 'singleton_overlap': case label overlaps another case label"));
    c_test_case_range_lower_diagnostic(arguments, &result,
                                       S8("int range_overlap(int value) { switch (value) { case 1 ... 3: return 0; case 3 ... 5: return 1; } return 2; }\n"),
                                       S8("in function 'range_overlap': case label overlaps another case label"));
    // A plain case label is folded in the type it is spelled in, so the
    // lowering has to convert it to the promoted type of the controlling
    // expression before it becomes a dispatch immediate.  `case -1` on a
    // `long long` switch used to arrive as the `int` bits 0xffffffff: it
    // matched nothing and was indistinguishable from `case 4294967295`.
    // Promotion, not truncation to the switched value's own type, is what the
    // narrow function pins -- an `unsigned char` switch promotes to `int`, so
    // -1 stays -1 there and never collapses onto 255.
    CPreprocessResult case_label_tokens = c_preprocess(control_flow_temporary.arena,
                                                       S8("long long wide_labels(long long value) {\n"
                                                          "    switch (value) {\n"
                                                          "    case -1: return 11;\n"
                                                          "    case 4294967295LL: return 22;\n"
                                                          "    default: return 33;\n"
                                                          "    }\n"
                                                          "}\n"
                                                          "int narrow_labels(unsigned char value) {\n"
                                                          "    switch (value) {\n"
                                                          "    case -1: return 11;\n"
                                                          "    case 255: return 22;\n"
                                                          "    default: return 33;\n"
                                                          "    }\n"
                                                          "}\n"),
                                                       (CPreprocessOptions){
                                                           .target = target_native,
                                                           .data_layout = target_data_layout(target_native),
                                                           .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                       });
    CParseResult case_label_parse = c_parse(control_flow_temporary.arena, case_label_tokens);
    CIRLowerResult case_label_ir = c_lower_to_ir(control_flow_temporary.arena, S8("case-label.c"), case_label_tokens, case_label_parse, target_native);
    BUSTER_TEST(arguments, case_label_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, case_label_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, case_label_ir.diagnostic_count == 0);
    if (case_label_ir.program)
    {
        IrModule* case_label_module = &case_label_ir.program->modules[0];
        u64 wide_immediates[2] = {0};
        u64 narrow_immediates[2] = {0};
        u32 wide_immediate_count = 0;
        u32 narrow_immediate_count = 0;
        for (u32 function_index = 0; function_index < case_label_module->function_count; function_index += 1)
        {
            IrFunction* function = case_label_module->functions + function_index;
            bool is_wide = string_equal(function->name, S8("wide_labels"));
            bool is_narrow = string_equal(function->name, S8("narrow_labels"));
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode != IR_OPCODE_SWITCH || instruction->immediate_count != 2)
                {
                    continue;
                }
                for (u32 immediate_index = 0; immediate_index < 2; immediate_index += 1)
                {
                    if (is_wide)
                    {
                        wide_immediates[immediate_index] = instruction->immediates[immediate_index];
                    }
                    else if (is_narrow)
                    {
                        narrow_immediates[immediate_index] = instruction->immediates[immediate_index];
                    }
                }
                wide_immediate_count += is_wide ? 2 : 0;
                narrow_immediate_count += is_narrow ? 2 : 0;
            }
        }
        BUSTER_TEST(arguments, wide_immediate_count == 2);
        BUSTER_TEST(arguments, narrow_immediate_count == 2);
        BUSTER_TEST(arguments, wide_immediates[0] == UINT64_MAX);
        BUSTER_TEST(arguments, wide_immediates[1] == 0xffffffffull);
        BUSTER_TEST(arguments, narrow_immediates[0] == 0xffffffffull);
        BUSTER_TEST(arguments, narrow_immediates[1] == 0xffull);
        BUSTER_TEST(arguments, ir_validate_canonical_module(case_label_ir.program, case_label_module).error == IR_VALIDATION_NONE);
    }
    // Two plain labels that collide are the same fault as two overlapping
    // ranges, and are now reported as one rather than failing the whole body
    // as an unsupported statement.
    c_test_case_range_lower_diagnostic(arguments, &result,
                                       S8("int duplicate_plain(int value) { switch (value) { case 3: return 0; case 3: return 1; } return 2; }\n"),
                                       S8("in function 'duplicate_plain': case label overlaps another case label"));
    c_test_auto_type_diagnostic(arguments, &result,
                                S8("int strict_range(int value) { switch (value) { case 1 ... 2: return 0; } return 1; }\n"), C_PREPROCESS_DIALECT_C23,
                                C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                S8("in function 'strict_range': GNU case ranges are only available in GNU dialects"));
    {
        // Control falling off the end of a non-void function is undefined only
        // if the caller uses the value (C 6.9.1p12), so the body lowers and
        // its final block ends in unreachable, exactly as Clang and GCC emit.
        // sbase's dc ends a function this way, after a call to its own
        // non-noreturn error().
        CPreprocessResult falls_off_tokens = c_preprocess(control_flow_temporary.arena,
                                                          S8("int falls_off(int value) {\n"
                                                             "    if (value) {\n"
                                                             "        return 1;\n"
                                                             "    }\n"
                                                             "    value = value + 1;\n"
                                                             "}\n"),
                                                          (CPreprocessOptions){0});
        CParseResult falls_off_parse = c_parse(control_flow_temporary.arena, falls_off_tokens);
        BUSTER_TEST(arguments, falls_off_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, falls_off_parse.diagnostic_count == 0);
        CIRLowerResult falls_off_ir =
            c_lower_to_ir(control_flow_temporary.arena, S8("falls-off.c"), falls_off_tokens, falls_off_parse, target_native);
        BUSTER_TEST(arguments, falls_off_ir.diagnostic_count == 0);
        BUSTER_TEST(arguments, falls_off_ir.program != 0);
        if (falls_off_ir.program)
        {
            IrModule* falls_off_module = &falls_off_ir.program->modules[0];
            bool falls_off_unreachable = false;
            for (u32 function_index = 0; function_index < falls_off_module->function_count; function_index += 1)
            {
                IrFunction* falls_off_function = falls_off_module->functions + function_index;
                if (!string_equal(falls_off_function->name, S8("falls_off")))
                {
                    continue;
                }
                for (u32 instruction_index = 0; instruction_index < falls_off_function->instruction_count; instruction_index += 1)
                {
                    falls_off_unreachable |= falls_off_function->instructions[instruction_index].opcode == IR_OPCODE_UNREACHABLE;
                }
            }
            BUSTER_TEST(arguments, falls_off_unreachable);
            BUSTER_TEST(arguments, ir_validate_canonical_module(falls_off_ir.program, falls_off_module).error == IR_VALIDATION_NONE);
        }
    }
    CPreprocessResult goto_tokens = c_preprocess(control_flow_temporary.arena,
                                                 S8("int jump(int value) {\n"
                                                    "    goto target;\n"
                                                    "    value = 99;\n"
                                                    "target:\n"
                                                    "    value += 1;\n"
                                                    "    if (value > 2) {\n"
                                                    "        goto done;\n"
                                                    "    }\n"
                                                    "    value += 2;\n"
                                                    "done:\n"
                                                    "    return value;\n"
                                                    "}\n"),
                                                 (CPreprocessOptions){0});
    CParseResult goto_parse = c_parse(control_flow_temporary.arena, goto_tokens);
    BUSTER_TEST(arguments, goto_parse.diagnostic_count == 0);
    CIRLowerResult goto_ir = c_lower_to_ir(control_flow_temporary.arena, S8("goto.c"), goto_tokens, goto_parse, target_native);
    BUSTER_TEST(arguments, goto_ir.diagnostic_count == 0);
    if (goto_ir.program)
    {
        IrModule* module = &goto_ir.program->modules[0];
        BUSTER_TEST(arguments, module->lowered_function_count == 1);
        BUSTER_TEST(arguments, module->functions[0].block_count >= 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(goto_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult short_circuit_tokens = c_preprocess(control_flow_temporary.arena,
                                                          S8("int observe(int value) {\n"
                                                             "    return value;\n"
                                                             "}\n"
                                                             "int short_circuit(int left, int right) {\n"
                                                             "    if (left && observe(right)"
                                                             " || observe(left)) {\n"
                                                             "        return 1;\n"
                                                             "    }\n"
                                                             "    return 0;\n"
                                                             "}\n"
                                                             "int logical_value(int left, int right) {\n"
                                                             "    int result = left && observe(right);\n"
                                                             "    return result || observe(left);\n"
                                                             "}\n"
                                                             "int conditional_value(int condition,"
                                                             " int left, int right) {\n"
                                                             "    int selected = condition ? left : right;\n"
                                                             "    return condition && left"
                                                             " ? observe(selected) : selected + 1;\n"
                                                             "}\n"
                                                             "int nested_conditional(int first,"
                                                             " int second, int third) {\n"
                                                             "    return first ?"
                                                             " (second ? 1 : 2) :"
                                                             " (third ? 3 : 4);\n"
                                                             "}\n"
                                                             "int embedded_logical(int left,"
                                                             " int right) {\n"
                                                             "    return"
                                                             " (left && observe(right)) == 1;\n"
                                                             "}\n"
                                                             "int embedded_conditional(int condition,"
                                                             " int value) {\n"
                                                             "    return"
                                                             " (condition ? observe(value) : 0) + 2;\n"
                                                             "}\n"),
                                                          (CPreprocessOptions){0});
    CParseResult short_circuit_parse = c_parse(control_flow_temporary.arena, short_circuit_tokens);
    BUSTER_TEST(arguments, short_circuit_parse.diagnostic_count == 0);
    CIRLowerResult short_circuit_ir =
        c_lower_to_ir(control_flow_temporary.arena, S8("short_circuit.c"), short_circuit_tokens, short_circuit_parse, target_native);
    BUSTER_TEST(arguments, short_circuit_ir.diagnostic_count == 0);
    if (short_circuit_ir.program)
    {
        IrModule* module = &short_circuit_ir.program->modules[0];
        IrFunction* function = &module->functions[1];
        u32 call_count = 0;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            call_count += opcode == IR_OPCODE_CALL;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, call_count == 2);
        BUSTER_TEST(arguments, branch_if_count == 3);
        IrFunction* value_function = &module->functions[2];
        u32 value_branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < value_function->instruction_count; instruction_index += 1)
        {
            value_branch_if_count += value_function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, value_branch_if_count == 4);
        IrFunction* conditional_function = &module->functions[3];
        u32 conditional_branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < conditional_function->instruction_count; instruction_index += 1)
        {
            conditional_branch_if_count += conditional_function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, conditional_branch_if_count == 3);
        BUSTER_TEST(arguments, conditional_function->local_count == 6);
        IrFunction* nested_function = &module->functions[4];
        u32 nested_branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < nested_function->instruction_count; instruction_index += 1)
        {
            nested_branch_if_count += nested_function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, nested_branch_if_count == 3);
        for (u32 function_index = 5; function_index <= 6; function_index += 1)
        {
            IrFunction* embedded = &module->functions[function_index];
            u32 embedded_call_count = 0;
            u32 embedded_branch_if_count = 0;
            for (u32 instruction_index = 0; instruction_index < embedded->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = embedded->instructions[instruction_index].opcode;
                embedded_call_count += opcode == IR_OPCODE_CALL;
                embedded_branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
            }
            BUSTER_TEST(arguments, embedded_call_count == 1);
            BUSTER_TEST(arguments, embedded_branch_if_count >= 1);
        }
        IrValidationResult short_validation = ir_validate_canonical_module(short_circuit_ir.program, module);
        BUSTER_TEST(arguments, short_validation.error == IR_VALIDATION_NONE);
    }
    scratch_end(control_flow_temporary);
    CPreprocessResult undeclared_tokens = c_preprocess(arguments->arena,
                                                       S8("int main(void)"
                                                          " { return missing; }\n"),
                                                       (CPreprocessOptions){0});
    CParseResult undeclared_parse = c_parse(arguments->arena, undeclared_tokens);
    BUSTER_TEST(arguments, undeclared_parse.diagnostic_count == 1);
    BUSTER_TEST(arguments, undeclared_parse.diagnostic_count == 1 && undeclared_parse.diagnostics[0].kind == C_DIAGNOSTIC_UNDECLARED_IDENTIFIER);
    CPreprocessResult argument_tokens = c_preprocess(arguments->arena,
                                                     S8("static int identity(int value)\n"
                                                        "{\n"
                                                        "    return value;\n"
                                                        "}\n"
                                                        "int main(void)\n"
                                                        "{\n"
                                                        "    return "
                                                        "(identity(1 + 2) == 3) - 1;\n"
                                                        "}\n"),
                                                     (CPreprocessOptions){0});
    CParseResult argument_parse = c_parse(arguments->arena, argument_tokens);
    CIRLowerResult argument_ir = c_lower_to_ir(arguments->arena, S8("arguments.c"), argument_tokens, argument_parse, target_native);
    BUSTER_TEST(arguments, argument_ir.diagnostic_count == 0);
    if (argument_ir.program)
    {
        IrModule* module = &argument_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 2);
        IrFunction* identity = module->functions;
        IrType* identity_type = ir_type_from_id(&argument_ir.program->types, identity->canonical_type);
        BUSTER_TEST(arguments, identity_type->parameter_count == 1);
        BUSTER_TEST(arguments, identity->instructions[1].opcode == IR_OPCODE_ARGUMENT);
        IrFunction* main_function = module->functions + 1;
        u32 call_count = 0;
        u32 comparison_count = 0;
        u32 cast_count = 0;
        for (u32 instruction_index = 0; instruction_index < main_function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = main_function->instructions + instruction_index;
            if (instruction->opcode == IR_OPCODE_CALL)
            {
                call_count += 1;
                BUSTER_TEST(arguments, instruction->operand_count == 2);
                IrInstruction* argument = main_function->instructions + main_function->values[instruction->operands[1].value].definition.value;
                BUSTER_TEST(arguments, argument->opcode == IR_OPCODE_BINARY);
                BUSTER_TEST(arguments, argument->binary_operation == IR_BINARY_INTEGER_ADD);
            }
            comparison_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_INTEGER_EQUAL;
            cast_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND;
        }
        BUSTER_TEST(arguments, call_count == 1);
        BUSTER_TEST(arguments, comparison_count == 1);
        BUSTER_TEST(arguments, cast_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(argument_ir.program, module).error == IR_VALIDATION_NONE);
    }
    return result;
}

// A conditional nested in the then arm produces `identifier : ... :` token
// runs (`b ? b ? c : s : l`) whose middle identifier must not be mistaken
// for a label; the misdetection left an unreachable, unterminated label
// block behind and failed canonical validation.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_for_declaration_scopes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena for_scope_temporary = scratch_begin(0, 0);
    // A for statement's init declaration scopes over the whole controlled statement, and the
    // controlled statement is not required to be a compound one. Every body below is unbraced, so
    // the scope has to be built from the statement grammar rather than from a closing brace: the
    // selection case ends past the `else` arm, and the nested and guarded cases put the `for`
    // itself where no `;`, `{` or `}` precedes it.
    CPreprocessResult for_scope_tokens = c_preprocess(for_scope_temporary.arena,
                                                      S8("int single(int count)\n"
                                                         "{\n"
                                                         "    int total = 0;\n"
                                                         "    for (int index = 0; index < count; index += 1)"
                                                         " total = total + index;\n"
                                                         "    return total;\n"
                                                         "}\n"
                                                         "int selection(int count)\n"
                                                         "{\n"
                                                         "    int total = 0;\n"
                                                         "    for (int index = 0; index < count; index += 1)"
                                                         " if (index & 1) total = total + index;"
                                                         " else total = total - index;\n"
                                                         "    return total;\n"
                                                         "}\n"
                                                         "int nested(int count)\n"
                                                         "{\n"
                                                         "    int total = 0;\n"
                                                         "    for (int outer = 0; outer < count; outer += 1)"
                                                         " for (int inner = 0; inner < outer; inner += 1)"
                                                         " total = total + outer + inner;\n"
                                                         "    return total;\n"
                                                         "}\n"
                                                         "int guarded(int count)\n"
                                                         "{\n"
                                                         "    int total = 0;\n"
                                                         "    if (count > 0)"
                                                         " for (int index = 0; index < count; index += 1)"
                                                         " total = total + index;\n"
                                                         "    return total;\n"
                                                         "}\n"
                                                         "int repeated(int count)\n"
                                                         "{\n"
                                                         "    int total = 0;\n"
                                                         "    for (int index = 0; index < count; index += 1)"
                                                         " do { total = total + index; } while (0);\n"
                                                         "    return total;\n"
                                                         "}\n"
                                                         "int empty(int count)\n"
                                                         "{\n"
                                                         "    int total = 0;\n"
                                                         "    for (int index = 0; index < count; index += 1) ;\n"
                                                         "    return total;\n"
                                                         "}\n"),
                                                      (CPreprocessOptions){0});
    CParseResult for_scope_parse = c_parse(for_scope_temporary.arena, for_scope_tokens);
    BUSTER_TEST(arguments, for_scope_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, for_scope_parse.diagnostic_count == 0);
    CIRLowerResult for_scope_ir =
        c_lower_to_ir(for_scope_temporary.arena, S8("for-declaration-scopes.c"), for_scope_tokens, for_scope_parse, target_native);
    BUSTER_TEST(arguments, for_scope_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, for_scope_ir.program != 0);
    if (for_scope_ir.program)
    {
        IrModule* module = &for_scope_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 6);
        BUSTER_TEST(arguments, ir_validate_canonical_module(for_scope_ir.program, module).error == IR_VALIDATION_NONE);
    }
    // The scope ends with the controlled statement and no later: a use after the loop is undeclared.
    CPreprocessResult after_single_tokens = c_preprocess(for_scope_temporary.arena,
                                                         S8("int after_single(int count)\n"
                                                            "{\n"
                                                            "    int total = 0;\n"
                                                            "    for (int index = 0; index < count; index += 1)"
                                                            " total = total + index;\n"
                                                            "    return total + index;\n"
                                                            "}\n"),
                                                         (CPreprocessOptions){0});
    CParseResult after_single_parse = c_parse(for_scope_temporary.arena, after_single_tokens);
    BUSTER_TEST(arguments, after_single_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, after_single_parse.diagnostic_count != 0);
    CPreprocessResult after_selection_tokens = c_preprocess(for_scope_temporary.arena,
                                                            S8("int after_selection(int count)\n"
                                                               "{\n"
                                                               "    int total = 0;\n"
                                                               "    for (int index = 0; index < count; index += 1)"
                                                               " if (index & 1) total = total + index;"
                                                               " else total = total - index;\n"
                                                               "    return total + index;\n"
                                                               "}\n"),
                                                            (CPreprocessOptions){0});
    CParseResult after_selection_parse = c_parse(for_scope_temporary.arena, after_selection_tokens);
    BUSTER_TEST(arguments, after_selection_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, after_selection_parse.diagnostic_count != 0);
    scratch_end(for_scope_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_then_nested_conditionals(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena nested_conditional_temporary = scratch_begin(0, 0);
    CPreprocessResult nested_conditional_tokens = c_preprocess(nested_conditional_temporary.arena,
                                                               S8("int probe(void)\n"
                                                                  "{\n"
                                                                  "    char c = 1; short s = 2; long l = 3; _Bool b = 1;\n"
                                                                  "    return (int)sizeof(b ? b ? c : s : l);\n"
                                                                  "}\n"
                                                                  "long select_then(int which)\n"
                                                                  "{\n"
                                                                  "    char c = 1; short s = 2; long l = 3;\n"
                                                                  "    return which ? which > 1 ? c : s : l;\n"
                                                                  "}\n"),
                                                               (CPreprocessOptions){0});
    CParseResult nested_conditional_parse = c_parse(nested_conditional_temporary.arena, nested_conditional_tokens);
    BUSTER_TEST(arguments, nested_conditional_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, nested_conditional_parse.diagnostic_count == 0);
    CIRLowerResult nested_conditional_ir = c_lower_to_ir(nested_conditional_temporary.arena, S8("nested-conditional.c"), nested_conditional_tokens,
                                                         nested_conditional_parse, target_native);
    BUSTER_TEST(arguments, nested_conditional_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, nested_conditional_ir.program != 0);
    if (nested_conditional_ir.program)
    {
        IrModule* module = &nested_conditional_ir.program->modules[0];
        BUSTER_TEST(arguments, ir_validate_canonical_module(nested_conditional_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(nested_conditional_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_conditional_type_prediction(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("const char* choose_feature(unsigned target_register, unsigned source_register)\n"
                                               "{\n"
                                               "    char apx_features[1] = {0};\n"
                                               "    return (target_register >= 16 || source_register >= 16) ? apx_features : 0;\n"
                                               "}\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("conditional-type-prediction.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, &ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }

    // This is the shape used by Lua's lua_absindex: a casted pointer
    // difference plus an integer in the false arm of a conditional.  The
    // predictor must retain the cast's integer type rather than the last
    // member expression's pointer type.
    CPreprocessResult lua_shape_tokens = c_preprocess(temporary.arena,
                                                      S8("struct ConditionalState { int *top; int *func; };\n"
                                                         "int conditional_lua_shape(struct ConditionalState *state, int index)\n"
                                                         "{ return (index > 0 || index <= -2000) ? index : ((int)(state->top - state->func)) + index; }\n"),
                                                      (CPreprocessOptions){0});
    CParseResult lua_shape_parse = c_parse(temporary.arena, lua_shape_tokens);
    CIRLowerResult lua_shape_ir = c_lower_to_ir(temporary.arena, S8("conditional-lua-shape.c"), lua_shape_tokens, lua_shape_parse, target_native);
    BUSTER_TEST(arguments, lua_shape_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, lua_shape_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, lua_shape_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, lua_shape_ir.program != 0);
    if (lua_shape_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(lua_shape_ir.program, &lua_shape_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }

    // Type prediction is speculative and must yield to the ordinary lowering
    // path before a deeply nested conditional/arithmetic expression exhausts
    // the compiler's host stack.  Fuzz-instrumented sanitizer frames are large
    // enough to expose this well before an unsanitized build does.
    u64 deep_capacity = 4096;
    char8* deep_source_pointer = arena_allocate(temporary.arena, char8, deep_capacity);
    u64 deep_source_length = 0;
    c_test_append_source(deep_source_pointer, deep_capacity, &deep_source_length, S8("int deep_prediction(int value) { return "));
    for (u32 depth = 0; depth < 96; depth += 1)
    {
        c_test_append_source(deep_source_pointer, deep_capacity, &deep_source_length, S8("value ? 1 + ("));
    }
    c_test_append_source(deep_source_pointer, deep_capacity, &deep_source_length, S8("value"));
    for (u32 depth = 0; depth < 96; depth += 1)
    {
        c_test_append_source(deep_source_pointer, deep_capacity, &deep_source_length, S8(") : 0"));
    }
    c_test_append_source(deep_source_pointer, deep_capacity, &deep_source_length, S8("; }\n"));
    String8 deep_source = {deep_source_pointer, deep_source_length};
    CPreprocessResult deep_tokens = c_preprocess(temporary.arena, deep_source, (CPreprocessOptions){0});
    CParseResult deep_parse = c_parse(temporary.arena, deep_tokens);
    CIRLowerResult deep_ir = c_lower_to_ir(temporary.arena, S8("deep-conditional-type-prediction.c"), deep_tokens, deep_parse, target_native);
    BUSTER_TEST(arguments, deep_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, deep_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, deep_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, deep_ir.program != 0);
    if (deep_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(deep_ir.program, &deep_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// A conditional with void branches is common in Lua's GC-barrier macros:
// `iscollectable(v) ? luaC_objbarrier(...) : ((void)(0))`.  It has side
// effects but no result place; lowering must not cast/store a synthetic value
// into a void temporary.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_conditional_void_expression(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("void barrier_call(void);\n"
                                               "void conditional_void_shape(int condition)\n"
                                               "{ condition ? barrier_call() : ((void)(0)); }\n"
                                               "void conditional_void_nested(int condition)\n"
                                               "{ condition ? (condition ? barrier_call() : ((void)(0))) : ((void)(0)); }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("conditional-void-expression.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, &ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// Lua's luaV_fastget macro assigns a lookup result in one arm of a
// conditional comma expression.  The assignment is not the arm's root
// operator, but it is still a required side effect before the following
// `isempty(slot)` test and before either conditional result is used.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_conditional_comma_assignment(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("typedef struct FastValue { unsigned char tt; } FastValue;\n"
                                               "typedef struct FastState { FastValue *table; unsigned char tt; unsigned int limit; FastValue *array; } FastState;\n"
                                               "extern const FastValue *fast_lookup(FastValue *table, const char *key);\n"
                                               "extern const FastValue *fast_lookup_index(FastValue *table, unsigned long key);\n"
                                               "#define fast_ttistable(o) ((o)->tt == 5)\n"
                                               "#define fast_hvalue(o) ((o)->table)\n"
                                               "#define fast_isempty(o) ((o)->tt == 0)\n"
                                               "#define fast_get(L,t,k,slot,f) (!fast_ttistable(t) ? (slot = (void *)0, 0) : (slot = f(fast_hvalue(t), k), !fast_isempty(slot)))\n"
                                               "#define fast_geti(t,k,slot) (!fast_ttistable(t) ? (slot = (void *)0, 0) : (slot = ((unsigned long)(k) - 1u < (t)->limit) ? &(t)->array[(k) - 1] : fast_lookup_index(fast_hvalue(t), (k)), !fast_isempty(slot)))\n"
                                               "typedef struct LexState { int current; } LexState;\n"
                                               "extern int lex_save(LexState *state, int current);\n"
                                               "extern int lex_getc(LexState *state);\n"
                                               "#define lex_next(ls) ((ls)->current = lex_getc((ls)))\n"
                                               "#define lex_save_and_next(ls) (lex_save((ls), (ls)->current), lex_next((ls)))\n"
                                               "#define lex_cast(t,e) ((t)(e))\n"
                                               "#define lex_cast_void(value) lex_cast(void, (value))\n"
                                               "int fast_macro_shape(FastState *state, const char *key)\n"
                                               "{ const FastValue *slot; return fast_get((void *)0, state, key, slot, fast_lookup) ? slot->tt : 0; }\n"
                                               "int fast_macro_integer_shape(FastState *state, unsigned long key)\n"
                                               "{ const FastValue *slot; return fast_geti(state, key, slot) ? slot->tt : 0; }\n"
                                               "int lex_control_shape(LexState *state)\n"
                                               "{ while (lex_cast_void(lex_save_and_next(state)), state->current >= 0) {} return state->current; }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("conditional-comma-assignment.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule* module = &ir.program->modules[0];
        BUSTER_TEST(arguments, module->lowered_function_count >= 3);
        IrFunction* function = c_test_find_ir_function(module, S8("fast_macro_shape"));
        BUSTER_TEST(arguments, function != 0);
        if (function)
        {
            u32 call_count = 0;
            u32 store_count = 0;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = function->instructions[instruction_index].opcode;
                call_count += opcode == IR_OPCODE_CALL;
                store_count += opcode == IR_OPCODE_STORE;
            }
            BUSTER_TEST(arguments, call_count == 1);
            BUSTER_TEST(arguments, store_count >= 1);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
        IrFunction* integer_function = c_test_find_ir_function(module, S8("fast_macro_integer_shape"));
        BUSTER_TEST(arguments, integer_function != 0);
        if (integer_function)
        {
            u32 call_count = 0;
            u32 store_count = 0;
            for (u32 instruction_index = 0; instruction_index < integer_function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = integer_function->instructions[instruction_index].opcode;
                call_count += opcode == IR_OPCODE_CALL;
                store_count += opcode == IR_OPCODE_STORE;
            }
            BUSTER_TEST(arguments, call_count == 1);
            BUSTER_TEST(arguments, store_count >= 2);
        }
        IrFunction *control_function = c_test_find_ir_function(module, S8("lex_control_shape"));
        BUSTER_TEST(arguments, control_function != 0);
        if (control_function)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
        }
    }
    scratch_end(temporary);
    return result;
}

// `size_t` and `ptrdiff_t` must be pointer-width whatever target the lowering
// is handed, because an integer converted to a pointer is widened to them on
// the way.  The shape that broke this is the one every test above writes
// without meaning to: `c_preprocess` with default options carries one target's
// data layout, and `c_lower_to_ir` builds its scalar types from the target
// argument.  When the two disagree about `long` -- LP64 against LLP64 -- a
// choice made from the layout's own `unsigned long` entry picks a 32-bit
// `ptrdiff_t` on a 64-bit-pointer target, and `(void *)0` reaches
// INTEGER_TO_POINTER still 32 bits wide.  Both directions are lowered here so
// the test is the same test on an LP64 host and on an LLP64 one.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_pointer_width_integer_conversion(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 triples[] = {
        S8("x86_64-pc-windows-msvc"),
        S8("x86_64-unknown-linux-gnu"),
    };
    for (u32 triple_index = 0; triple_index < BUSTER_ARRAY_LENGTH(triples); triple_index += 1)
    {
        TemporalArena temporary = scratch_begin(0, 0);
        // Default preprocess options on purpose: this is the mismatch, and it
        // is what the frontend tests around this one all do.
        CPreprocessResult tokens = c_preprocess(temporary.arena,
                                                S8("typedef struct Slot { unsigned char tag; } Slot;\n"
                                                   "extern Slot *slot_lookup(Slot *table, unsigned long key);\n"
                                                   "int slot_shape(Slot *table, unsigned long key)\n"
                                                   "{ Slot *slot = (void *)0; slot = key ? slot_lookup(table, key) : (void *)0;\n"
                                                   "  return slot != (void *)0 ? slot->tag : 0; }\n"),
                                                (CPreprocessOptions){0});
        TargetParseResult parsed_target = target_parse_triple(triples[triple_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        CParseResult parse = c_parse(temporary.arena, tokens);
        CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("pointer-width-integer-conversion.c"), tokens, parse, parsed_target.target);
        BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, ir.diagnostic_count == 0);
        BUSTER_TEST(arguments, ir.program != 0);
        if (ir.program && ir.program->module_count)
        {
            IrModule* module = &ir.program->modules[0];
            BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
            IrFunction* function = c_test_find_ir_function(module, S8("slot_shape"));
            BUSTER_TEST(arguments, function != 0);
            if (function)
            {
                // Every integer-to-pointer conversion in the function reaches
                // it at the pointer's own width, which is the invariant
                // ir_canonical_conversion_valid enforces and the reason the
                // widening exists at all.
                u32 conversion_count = 0;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    if (instruction->opcode != IR_OPCODE_CAST || instruction->conversion_operation != IR_CONVERSION_INTEGER_TO_POINTER ||
                        !instruction->operand_count)
                    {
                        continue;
                    }
                    IrType* destination = ir_type_from_id(&ir.program->types, instruction->canonical_type);
                    IrType* source = ir_type_from_id(&ir.program->types, function->values[instruction->operands[0].value].canonical_type);
                    BUSTER_TEST(arguments, destination != 0 && source != 0);
                    if (destination && source)
                    {
                        BUSTER_TEST(arguments, destination->layout.resolved);
                        BUSTER_TEST(arguments, source->bit_width == destination->layout.size * 8);
                    }
                    conversion_count += 1;
                }
                BUSTER_TEST(arguments, conversion_count >= 1);
            }
        }
        scratch_end(temporary);
    }

    return result;
}

// A GNU statement expression can contain control flow and calls.  Its calls
// must be emitted in the selected arm's block, rather than hoisted into the
// enclosing expression's entry block by call preparation.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_statement_expression_control_call(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("extern void abort(void);\n"
                                               "typedef struct StatementValue { unsigned char tag; } StatementValue;\n"
                                               "int statement_expression_control_call(StatementValue *value)\n"
                                               "{ (void)({ if (value->tag & 64) ; else abort(); }); return 7; }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("statement-expression-control-call.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule* module = &ir.program->modules[0];
        IrFunction* function = c_test_find_ir_function(module, S8("statement_expression_control_call"));
        BUSTER_TEST(arguments, function != 0);
        if (function)
        {
            u32 entry_branch_if_count = 0;
            u32 abort_call_count = 0;
            bool abort_call_in_entry = false;
            for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
            {
                IrBlock* block = &function->blocks[block_index];
                if (block->first_instruction.value == IR_ID_UNDERLYING_INVALID || block->last_instruction.value >= function->instruction_count)
                {
                    continue;
                }
                for (u32 instruction_index = block->first_instruction.value; instruction_index <= block->last_instruction.value; instruction_index += 1)
                {
                    IrInstruction* instruction = &function->instructions[instruction_index];
                    if (instruction->opcode == IR_OPCODE_BRANCH_IF && block_index == function->entry.value)
                    {
                        entry_branch_if_count += 1;
                    }
                    if (instruction->opcode == IR_OPCODE_CALL)
                    {
                        IrSymbol* symbol = ir_symbol_from_id(&ir.program->symbols, instruction->symbol);
                        if (symbol && string_equal(symbol->name, S8("abort")))
                        {
                            abort_call_count += 1;
                            abort_call_in_entry |= block_index == function->entry.value;
                        }
                    }
                }
            }
            BUSTER_TEST(arguments, entry_branch_if_count == 1);
            BUSTER_TEST(arguments, abort_call_count == 1);
            BUSTER_TEST(arguments, !abort_call_in_entry);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// A control-bearing statement expression can also occur inside a function
// call argument (the shape used by glibc's assert/check_exp macros).  The
// deferred call must still be lowered in the statement-expression body rather
// than being lost while the outer call's arguments are prepared.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_statement_expression_nested_call(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("extern void abort(void);\n"
                                               "typedef struct NestedObject { unsigned char tag; } NestedObject;\n"
                                               "typedef struct NestedString { char contents[1]; } NestedString;\n"
                                               "union NestedUnion { NestedObject object; NestedString string; };\n"
                                               "#define nested_check(c,e) ((void)sizeof((c) ? 1 : 0), __extension__ ({ if (c) ; else abort(); }), (e))\n"
                                               "static NestedString *nested_get(NestedString *value) { return value; }\n"
                                               "NestedString *statement_expression_nested_call(NestedObject *value)\n"
                                               "{ return nested_get(nested_check((value->tag & 64) != 0, (&(((union NestedUnion *)value)->string)))); }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("statement-expression-nested-call.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule *module = &ir.program->modules[0];
        IrFunction *function = c_test_find_ir_function(module, S8("statement_expression_nested_call"));
        BUSTER_TEST(arguments, function != 0);
        if (function)
        {
            u32 abort_call_count = 0;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction *instruction = &function->instructions[instruction_index];
                if (instruction->opcode == IR_OPCODE_CALL)
                {
                    IrSymbol *symbol = ir_symbol_from_id(&ir.program->symbols, instruction->symbol);
                    abort_call_count += symbol && string_equal(symbol->name, S8("abort"));
                }
            }
            BUSTER_TEST(arguments, abort_call_count == 1);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// A statement expression whose body contains control flow evaluates to its
// final expression statement.  The two tests above only wrap the value in
// `(void)`, so the value being dropped was invisible: here the tail is
// `x + 6`, and the function's returned value must be that addition rather
// than the zero the lowering substituted when it found no trailing
// expression.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_statement_expression_control_value(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("int statement_expression_control_value(int x)\n"
                                               "{ return ({ if (x > 100) { x = 1; } x + 6; }); }\n"
                                               "int statement_expression_loop_value(int x)\n"
                                               "{ return ({ while (x > 100) { x = 1; } x + 6; }); }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("statement-expression-control-value.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        IrModule* module = &ir.program->modules[0];
        String8 value_function_names[] = {
            S8("statement_expression_control_value"),
            S8("statement_expression_loop_value"),
        };
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(value_function_names); name_index += 1)
        {
            IrFunction* function = c_test_find_ir_function(module, value_function_names[name_index]);
            BUSTER_TEST(arguments, function != 0);
            if (!function)
            {
                continue;
            }
            u32 return_count = 0;
            u32 returned_addition_count = 0;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = &function->instructions[instruction_index];
                if (instruction->opcode != IR_OPCODE_RETURN || instruction->operand_count != 1)
                {
                    continue;
                }
                return_count += 1;
                IrValueId returned = instruction->operands[0];
                if (returned.value >= function->value_count)
                {
                    continue;
                }
                IrInstructionId definition = function->values[returned.value].definition;
                if (definition.value >= function->instruction_count)
                {
                    continue;
                }
                IrInstruction* returned_definition = &function->instructions[definition.value];
                returned_addition_count +=
                    returned_definition->opcode == IR_OPCODE_BINARY && returned_definition->binary_operation == IR_BINARY_INTEGER_ADD;
            }
            BUSTER_TEST(arguments, return_count == 1);
            BUSTER_TEST(arguments, returned_addition_count == 1);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// A declaration inside a value-producing GNU statement expression is visible
// to the statements that follow it in the same body -- the shape the construct
// exists for, as in `({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })`.
// Where the statement expression stands in a statement, the function-body walk
// opens the body's scope at its `{` like any other block. Where it stands in a
// declaration's initializer it does not: that walk hands the whole declaration
// statement to c_parse_local_declarations and resumes past its semicolon, so
// the body's scope is opened from the declaration walk or nowhere, and its
// names resolve in the enclosing block instead.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_statement_expression_declaration_scope(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena statement_scope_temporary = scratch_begin(0, 0);
    CPreprocessResult statement_scope_tokens =
        c_preprocess(statement_scope_temporary.arena,
                     S8("int initializer(int x) { int v = ({ int t = x + 1; t; }); return v; }\n"
                        "int conditional_arm(int x) { int v = x > 100 ? 1 : ({ int t = x + 1; t; }); return v; }\n"
                        "int chained(int x) { int v = ({ int a = x + 1; int b = a * 2; b + a; }); return v; }\n"
                        "int nested(int x) { int v = ({ int t = ({ int s = x; s + 1; }); t * 2; }); return v; }\n"
                        "int adjacent(int x) { int v = ({ int t = x; t; }) + ({ int u = x; u * 2; }); return v; }\n"
                        "int declarator_list(int x) { int a = ({ int t = x; t; }), b = ({ int u = x; u; }); return a + b; }\n"
                        "int loop_initializer(int x) { int s = 0; for (int i = ({ int t = x; t; }); i < 8; i += 1) { s += i; } return s; }\n"
                        "int braced(int x) { int a[2] = { ({ int t = x; t; }), 9 }; return a[0] + a[1]; }\n"
                        "int shadowing(int x) { int t = 5; int v = ({ int t = x * 7; t; }); return t + v; }\n"
                        "int through_typedef(int x) { int v = ({ typedef int Local; Local t = x + 4; t; }); return v; }\n"
                        "#define STATEMENT_MAX(a, b) ({ __auto_type _a = (a); __auto_type _b = (b); _a > _b ? _a : _b; })\n"
                        "int macro_shape(int x, int y) { int m = STATEMENT_MAX(x, y); return m; }\n"),
                     (CPreprocessOptions){0});
    CParseResult statement_scope_parse = c_parse(statement_scope_temporary.arena, statement_scope_tokens);
    BUSTER_TEST(arguments, statement_scope_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, statement_scope_parse.diagnostic_count == 0);
    CIRLowerResult statement_scope_ir = c_lower_to_ir(statement_scope_temporary.arena, S8("statement-expression-declaration-scope.c"),
                                                      statement_scope_tokens, statement_scope_parse, target_native);
    BUSTER_TEST(arguments, statement_scope_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, statement_scope_ir.program != 0);
    if (statement_scope_ir.program)
    {
        IrModule* module = &statement_scope_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 11);
        BUSTER_TEST(arguments, ir_validate_canonical_module(statement_scope_ir.program, module).error == IR_VALIDATION_NONE);
    }
    // The body declares its own `t`: two entities of that spelling exist, in
    // different scopes, and neither is the other's redefinition.
    u32 statement_scope_t_count = 0;
    CScopeId statement_scope_first_t = C_SCOPE_ID_INVALID;
    bool statement_scope_distinct_scopes = false;
    for (u32 entity_index = 0; entity_index < statement_scope_parse.entity_count; entity_index += 1)
    {
        CEntity* entity = &statement_scope_parse.entities[entity_index];
        if (entity->kind != C_ENTITY_LOCAL || !string_equal(entity->name, S8("t")))
        {
            continue;
        }
        statement_scope_t_count += 1;
        if (statement_scope_first_t.value == C_ID_UNDERLYING_INVALID)
        {
            statement_scope_first_t = entity->scope;
        }
        else
        {
            statement_scope_distinct_scopes |= entity->scope.value != statement_scope_first_t.value;
        }
    }
    BUSTER_TEST(arguments, statement_scope_t_count >= 2);
    BUSTER_TEST(arguments, statement_scope_distinct_scopes);
    scratch_end(statement_scope_temporary);

    // A storage class or `typedef` written inside the body is a specifier of
    // the body's own declaration. Read as one of the declaration being
    // initialized, `v` below becomes a static object and `w` a type name.
    TemporalArena statement_specifier_temporary = scratch_begin(0, 0);
    CPreprocessResult statement_specifier_tokens =
        c_preprocess(statement_specifier_temporary.arena,
                     S8("int specifiers(int x) { int v = ({ static int s = 3; s + x; }); int w = ({ typedef int Local; Local t = x; t; }); return v + w; }\n"),
                     (CPreprocessOptions){0});
    CParseResult statement_specifier_parse = c_parse(statement_specifier_temporary.arena, statement_specifier_tokens);
    BUSTER_TEST(arguments, statement_specifier_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, statement_specifier_parse.diagnostic_count == 0);
    bool statement_specifier_v_automatic = false;
    bool statement_specifier_w_object = false;
    for (u32 entity_index = 0; entity_index < statement_specifier_parse.entity_count; entity_index += 1)
    {
        CEntity* entity = &statement_specifier_parse.entities[entity_index];
        statement_specifier_v_automatic |= entity->kind == C_ENTITY_LOCAL && string_equal(entity->name, S8("v")) && !entity->is_static_storage;
        statement_specifier_w_object |= entity->kind == C_ENTITY_LOCAL && string_equal(entity->name, S8("w"));
    }
    BUSTER_TEST(arguments, statement_specifier_v_automatic);
    BUSTER_TEST(arguments, statement_specifier_w_object);
    CIRLowerResult statement_specifier_ir = c_lower_to_ir(statement_specifier_temporary.arena, S8("statement-expression-specifiers.c"),
                                                          statement_specifier_tokens, statement_specifier_parse, target_native);
    BUSTER_TEST(arguments, statement_specifier_ir.diagnostic_count == 0);
    scratch_end(statement_specifier_temporary);

    // The body's scope ends with the body: a use after it is undeclared, the
    // same way a block's is.
    TemporalArena statement_after_temporary = scratch_begin(0, 0);
    CPreprocessResult statement_after_tokens = c_preprocess(statement_after_temporary.arena,
                                                            S8("int after(int x) { int v = ({ int t = x + 1; t; }); return v + t; }\n"),
                                                            (CPreprocessOptions){0});
    CParseResult statement_after_parse = c_parse(statement_after_temporary.arena, statement_after_tokens);
    BUSTER_TEST(arguments, statement_after_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, statement_after_parse.diagnostic_count != 0);
    scratch_end(statement_after_temporary);
    return result;
}

// Direct identifier updates in a body normally take a local fast path.  A
// file-scope object has no CIntegerIrLocal entry, but the expression core can
// still resolve it to a global place; keep prefix and postfix forms covered
// so that statement lowering does not reject `++global`/`global++`.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_global_identifier_updates(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("static int global_counter;\n"
                                               "int global_identifier_updates(int value)\n"
                                               "{ global_counter = value; ++global_counter; global_counter++; return global_counter; }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("global-identifier-updates.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, &ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// Assignment expressions can hide a dereference behind a parenthesized
// address expression.  The result of `*(&local) = value` must remain usable
// in a comma/return expression after its addressable place is recovered.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_parenthesized_address_assignment_expression(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("int parenthesized_address_assignment(int value)\n"
                                               "{ int local = 0; return (*(&local) = value, local); }\n"
                                               "int parenthesized_address_condition(int condition)\n"
                                               "{ int local = 0; if ((condition ? (*(&local) = 1, 1) : 0) && 1) return local; return local; }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("parenthesized-address-assignment-expression.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, &ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// Lua's userdata accessor combines nested pointer-cast macros, a
// builtin-offsetof ternary, and a switch whose other arm returns void *.  The
// strict expression-type walk must preserve the pointer result instead of
// falling back to the final integer member (`nuvalue`).
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_nested_offsetof_pointer_prediction(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("typedef unsigned long size_t;\n"
                                               "typedef unsigned char Byte;\n"
                                               "typedef struct GCObject { Byte tt; } GCObject;\n"
                                               "typedef struct UValue { unsigned long value; } UValue;\n"
                                               "typedef struct Udata0 { GCObject *next; Byte tt; Byte marked; unsigned short nuvalue; size_t len; void *meta; GCObject *list; union { long double align; } bindata; } Udata0;\n"
                                               "typedef struct Udata { GCObject *next; Byte tt; Byte marked; unsigned short nuvalue; size_t len; void *meta; GCObject *list; UValue uv[1]; } Udata;\n"
                                               "typedef union GCUnion { GCObject gc; Udata u; } GCUnion;\n"
                                               "typedef union Value { void *p; GCObject *gc; } Value;\n"
                                               "typedef struct TValue { Value value; Byte tag; } TValue;\n"
                                               "#define check_exp(c,e) (e)\n"
                                               "#define cast(t,e) ((t)(e))\n"
                                               "#define cast_charp(e) cast(char *, (e))\n"
                                               "#define cast_u(e) cast(union GCUnion *, (e))\n"
                                               "#define gco2u(e) check_exp((e)->tt == 7, &((cast_u(e))->u))\n"
                                               "#define val_(e) ((e)->value)\n"
                                               "#define uvalue(e) check_exp(1, gco2u(val_(e).gc))\n"
                                               "#define ttype(e) ((e)->tag & 0x0F)\n"
                                               "#define udatamemoffset(n) ((n) == 0 ? __builtin_offsetof(Udata0, bindata) : __builtin_offsetof(Udata, uv) + (sizeof(UValue) * (n)))\n"
                                               "#define getudatamem(e) (cast_charp(e) + udatamemoffset((e)->nuvalue))\n"
                                               "void *nested_offsetof_pointer(TValue *object)\n"
                                               "{ switch (ttype(object)) { case 7: return getudatamem(uvalue(object)); case 2: return val_(object).p; default: return ((void *)0); } }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("nested-offsetof-pointer-prediction.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, &ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// A casted pointer dereference is still a modifiable place.  Lua's luac
// reader uses the postfix form `(*(int *)ud)--`; keep it on the same update
// machinery as an uncast dereference.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_casted_dereference_update(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                               S8("int casted_dereference_update(void *opaque)\n"
                                               "{ return (*(int *)opaque)--; }\n"
                                               "int casted_dereference_parenthesized(void *opaque)\n"
                                               "{ return (*((int *)opaque))--; }\n"
                                               "int casted_dereference_index(void *opaque)\n"
                                               "{ return ((int *)opaque)[0]--; }\n"
                                               "int casted_dereference_prefix(void *opaque)\n"
                                               "{ return --*(int *)opaque; }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("casted-dereference-update.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, &ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// Lua and several hosted C headers parenthesize exported function names.  The
// spelling is distinct from a function-pointer object, which must remain an
// object declaration even though both forms contain `(name)(...)`.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_parenthesized_function_declarations(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("extern int (plain)(int value);\n"
                                               "extern int (*pointer)(int value);\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.declaration_count == 2);
    if (parse.declaration_count == 2)
    {
        CDeclaration* plain = &parse.declarations[0];
        CDeclaration* pointer = &parse.declarations[1];
        BUSTER_STRING_TEST(arguments, plain->name, S8("plain"));
        BUSTER_STRING_TEST(arguments, pointer->name, S8("pointer"));
        BUSTER_TEST(arguments, plain->kind == C_DECLARATION_FUNCTION);
        BUSTER_TEST(arguments, pointer->kind == C_DECLARATION_OBJECT);
        CType* plain_type = c_type_from_id(&parse, plain->type);
        CType* pointer_type = c_type_from_id(&parse, pointer->type);
        CType* pointed_function = pointer_type ? c_type_from_id(&parse, pointer_type->element_type) : 0;
        BUSTER_TEST(arguments, plain_type && plain_type->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, plain_type && plain_type->parameter_count == 1);
        BUSTER_TEST(arguments, pointer_type && pointer_type->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, pointed_function && pointed_function->kind == C_TYPE_FUNCTION);
    }
    scratch_end(temporary);
    return result;
}

// Lua's setnilvalue macro wraps a member assignment in an extra pair of
// parentheses and reaches the member through an address-of expression with a
// post-incremented pointer: `((( &(p++->val))->tt_) = value)`.  The expression
// lowering path must recover the field place from the materialized load before
// emitting the store.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_parenthesized_address_place_assignment(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
                                            S8("typedef struct LuaValue { int tt_; } LuaValue;\n"
                                               "typedef struct LuaStackValue { LuaValue val; } LuaStackValue;\n"
                                               "void setnilvalue_shape(LuaStackValue *p)\n"
                                               "{ for (int i = 0; i < 2; i++) (((&(p++->val))->tt_) = ((0) | ((0) << 4))); }\n"
                                               "typedef struct LuaBufferShape { unsigned long n; unsigned long size; char b[8]; } LuaBufferShape;\n"
                                               "void addchar_shape(int value)\n"
                                               "{ LuaBufferShape buffer; ((void)(((&buffer)->n < (&buffer)->size || (void *)0)), ((&buffer)->b[(&buffer)->n++] = value)); }\n"
                                               "typedef struct LuaLockShape { int lock; int *plock; } LuaLockShape;\n"
                                               "void unlock_shape(void *opaque)\n"
                                               "{ --(*((LuaLockShape *)((void *)((char *)opaque - sizeof(LuaLockShape))))->plock); }\n"),
                                            (CPreprocessOptions){0});
    CParseResult parse = c_parse(temporary.arena, tokens);
    CIRLowerResult ir = c_lower_to_ir(temporary.arena, S8("parenthesized-address-place.c"), tokens, parse, target_native);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, ir.program != 0);
    if (ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(ir.program, &ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_vectors(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena vector_temporary = scratch_begin(0, 0);
    CPreprocessResult vector_tokens = c_preprocess(vector_temporary.arena,
                                                   S8("typedef float Float4 "
                                                      "__attribute__((vector_size(16)));"
                                                      " typedef int Int4 "
                                                      "__attribute__((vector_size(16)));"
                                                      " typedef struct VectorPair"
                                                      " { Float4 left; Float4 right; }"
                                                      " VectorPair;"
                                                      " VectorPair identity"
                                                      "(VectorPair value)"
                                                      " { return value; }"
                                                      " Float4 arithmetic"
                                                      "(Float4 left, Float4 right)"
                                                      " { return -(left + right * 2.0f); }"
                                                      " Int4 compare(Int4 left, Int4 right)"
                                                      " { return left > right; }"
                                                      " int main(void)"
                                                      " { VectorPair value = { 0 };"
                                                      " return 0; }\n"),
                                                   (CPreprocessOptions){0});
    CParseResult vector_parse = c_parse(vector_temporary.arena, vector_tokens);
    BUSTER_TEST(arguments, vector_parse.diagnostic_count == 0);
    bool found_c_vector = false;
    for (u32 type_index = 0; type_index < vector_parse.type_count; type_index += 1)
    {
        CType* type = vector_parse.types + type_index;
        found_c_vector |= type->kind == C_TYPE_VECTOR && type->vector_byte_size == 16;
    }
    BUSTER_TEST(arguments, found_c_vector);
    CIRLowerResult vector_ir = c_lower_to_ir(vector_temporary.arena, S8("vector.c"), vector_tokens, vector_parse, target_native);
    BUSTER_TEST(arguments, vector_ir.diagnostic_count == 0);
    if (vector_ir.program)
    {
        bool found_ir_vector = false;
        bool found_vector_pair = false;
        bool found_vector_mask = false;
        for (u32 type_index = 0; type_index < vector_ir.program->types.count; type_index += 1)
        {
            IrType* type = vector_ir.program->types.types + type_index;
            found_ir_vector |= type->kind == IR_TYPE_VECTOR && type->layout.size == 16 && type->element_count == 4;
            found_vector_pair |= type->kind == IR_TYPE_STRUCT && type->field_count == 2 && type->layout.size == 32;
            if (type->kind == IR_TYPE_VECTOR && type->element_count == 4)
            {
                IrType* element = ir_type_from_id(&vector_ir.program->types, type->element_type);
                found_vector_mask |= element && element->kind == IR_TYPE_INTEGER && element->is_signed && element->bit_width == 32;
            }
        }
        BUSTER_TEST(arguments, found_ir_vector);
        BUSTER_TEST(arguments, found_vector_pair);
        BUSTER_TEST(arguments, found_vector_mask);
        IrModule* module = vector_ir.program->modules;
        BUSTER_TEST(arguments, module->function_count == 4);
        u32 vector_binary_count = 0;
        u32 vector_unary_count = 0;
        u32 vector_comparison_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                vector_unary_count += instruction->opcode == IR_OPCODE_UNARY && instruction->unary_operation >= IR_UNARY_VECTOR_INTEGER_NEGATE &&
                                      instruction->unary_operation <= IR_UNARY_VECTOR_INTEGER_BITWISE_NOT;
                vector_binary_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_ADD &&
                                       instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
                vector_comparison_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                                           instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
            }
        }
        BUSTER_TEST(arguments, vector_unary_count == 1);
        BUSTER_TEST(arguments, vector_binary_count == 3);
        BUSTER_TEST(arguments, vector_comparison_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(vector_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(vector_temporary);
    TemporalArena variadic_call_temporary = scratch_begin(0, 0);
    CPreprocessResult variadic_call_tokens = c_preprocess(variadic_call_temporary.arena,
                                                          S8("int sink(int fixed, ...);"
                                                             " int main(void)"
                                                             " { return sink(0,"
                                                             " (char)1, (float)2); }"),
                                                          (CPreprocessOptions){0});
    CParseResult variadic_call_parse = c_parse(variadic_call_temporary.arena, variadic_call_tokens);
    CIRLowerResult variadic_call_ir =
        c_lower_to_ir(variadic_call_temporary.arena, S8("variadic-call.c"), variadic_call_tokens, variadic_call_parse, target_native);
    BUSTER_TEST(arguments, variadic_call_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, variadic_call_ir.diagnostic_count == 0);
    if (variadic_call_ir.program)
    {
        IrModule* variadic_call_module = &variadic_call_ir.program->modules[0];
        IrInstruction* call = 0;
        IrFunction* main_function = 0;
        for (u32 function_index = 0; function_index < variadic_call_module->function_count; function_index += 1)
        {
            IrFunction* function = variadic_call_module->functions + function_index;
            if (string_equal(function->name, S8("main")))
            {
                main_function = function;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    if (function->instructions[instruction_index].opcode == IR_OPCODE_CALL)
                    {
                        call = function->instructions + instruction_index;
                        break;
                    }
                }
                break;
            }
        }
        BUSTER_TEST(arguments, main_function != 0);
        BUSTER_TEST(arguments, call != 0);
        if (main_function && call)
        {
            BUSTER_TEST(arguments, call->operand_count == 4);
            IrType* promoted_character = ir_type_from_id(&variadic_call_ir.program->types, main_function->values[call->operands[2].value].canonical_type);
            IrType* promoted_float = ir_type_from_id(&variadic_call_ir.program->types, main_function->values[call->operands[3].value].canonical_type);
            BUSTER_TEST(arguments, promoted_character && promoted_character->kind == IR_TYPE_INTEGER && promoted_character->bit_width == 32);
            BUSTER_TEST(arguments, promoted_float && promoted_float->kind == IR_TYPE_FLOAT && promoted_float->bit_width == 64);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(variadic_call_ir.program, variadic_call_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(variadic_call_temporary);
    TemporalArena c23_va_start_temporary = scratch_begin(0, 0);
    CPreprocessResult c23_va_start_tokens = c_preprocess(c23_va_start_temporary.arena,
                                                         S8("typedef void *va_list;"
                                                            " int first(int count, ...)"
                                                            " { va_list arguments;"
                                                            " __builtin_c23_va_start("
                                                            "arguments, count);"
                                                            " int value = __builtin_va_arg("
                                                            "arguments, int);"
                                                            " __builtin_va_end(arguments);"
                                                            " return value; }"),
                                                         (CPreprocessOptions){
                                                             .dialect = C_PREPROCESS_DIALECT_C23,
                                                         });
    CParseResult c23_va_start_parse = c_parse(c23_va_start_temporary.arena, c23_va_start_tokens);
    CIRLowerResult c23_va_start_ir = c_lower_to_ir(c23_va_start_temporary.arena, S8("c23-va-start.c"), c23_va_start_tokens, c23_va_start_parse, target_native);
    BUSTER_TEST(arguments, c23_va_start_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, c23_va_start_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, c23_va_start_ir.diagnostic_count == 0);
    if (c23_va_start_ir.program)
    {
        IrModule* module = c23_va_start_ir.program->modules;
        u32 va_start_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                va_start_count += function->instructions[instruction_index].opcode == IR_OPCODE_VA_START;
            }
        }
        BUSTER_TEST(arguments, va_start_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(c23_va_start_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(c23_va_start_temporary);
    TemporalArena debug_trap_temporary = scratch_begin(0, 0);
    CPreprocessResult debug_trap_tokens = c_preprocess(debug_trap_temporary.arena,
                                                       S8("int main(void)"
                                                          " { __builtin_debugtrap();"
                                                          " return 0; }"),
                                                       (CPreprocessOptions){0});
    CParseResult debug_trap_parse = c_parse(debug_trap_temporary.arena, debug_trap_tokens);
    CIRLowerResult debug_trap_ir = c_lower_to_ir(debug_trap_temporary.arena, S8("debug-trap.c"), debug_trap_tokens, debug_trap_parse, target_native);
    BUSTER_TEST(arguments, debug_trap_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, debug_trap_ir.diagnostic_count == 0);
    if (debug_trap_ir.program)
    {
        IrModule* debug_trap_module = &debug_trap_ir.program->modules[0];
        u32 debug_trap_count = 0;
        for (u32 instruction_index = 0; instruction_index < debug_trap_module->functions[0].instruction_count; instruction_index += 1)
        {
            debug_trap_count += debug_trap_module->functions[0].instructions[instruction_index].opcode == IR_OPCODE_DEBUG_TRAP;
        }
        BUSTER_TEST(arguments, debug_trap_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(debug_trap_ir.program, debug_trap_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(debug_trap_temporary);
    TemporalArena compound_temporary = scratch_begin(0, 0);
    CPreprocessResult compound_tokens = c_preprocess(compound_temporary.arena,
                                                     S8("struct Pair"
                                                        " { int left; int right; };"
                                                        " int main(void)"
                                                        " { return ((struct Pair)"
                                                        " { .right = 7, .left = 3 }).right; }"),
                                                     (CPreprocessOptions){0});
    CParseResult compound_parse = c_parse(compound_temporary.arena, compound_tokens);
    CIRLowerResult compound_ir = c_lower_to_ir(compound_temporary.arena, S8("compound-literal.c"), compound_tokens, compound_parse, target_native);
    BUSTER_TEST(arguments, compound_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, compound_ir.diagnostic_count == 0);
    if (compound_ir.program)
    {
        IrModule* compound_module = &compound_ir.program->modules[0];
        u32 aggregate_count = 0;
        u32 field_count = 0;
        for (u32 instruction_index = 0; instruction_index < compound_module->functions[0].instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = compound_module->functions[0].instructions[instruction_index].opcode;
            aggregate_count += opcode == IR_OPCODE_AGGREGATE;
            field_count += opcode == IR_OPCODE_FIELD;
        }
        BUSTER_TEST(arguments, aggregate_count == 1);
        BUSTER_TEST(arguments, field_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(compound_ir.program, compound_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(compound_temporary);
    TemporalArena regression_temporary = scratch_begin(0, 0);
    CPreprocessResult regression_tokens = c_preprocess(regression_temporary.arena,
                                                       S8("typedef unsigned char char8;"
                                                          " typedef struct Id Id;"
                                                          " struct Id { unsigned value; };"
                                                          " struct Item"
                                                          " { Id id; int values[2];"
                                                          " int thread_local; int enabled; };"
                                                          " static struct Item *pick("
                                                          " struct Item *item) { return item; }"
                                                          " int main(void) {"
                                                          " enum { PICK = 1 };"
                                                          " typedef unsigned int LocalIndex;"
                                                          " typedef struct LocalPair"
                                                          " { int value; } LocalPair;"
                                                          " static char8 const text[] = \"x\";"
                                                          " LocalIndex condition = 1;"
                                                          " LocalPair local_pair = { .value = 2 };"
                                                          " struct Item item = (struct Item){"
                                                          " .id = (Id){ .value = 0u },"
                                                          " .thread_local = 5,"
                                                          " .enabled = condition != 0 ||"
                                                          " condition == 0 };"
                                                          " item.values[0] = 3;"
                                                          " item.values[1] = 4;"
                                                          " int alternate[2] = { 9, 10 };"
                                                          " int *selected_values = condition ?"
                                                          " item.values : alternate;"
                                                          " int pointer_order = selected_values <"
                                                          " selected_values + 1;"
                                                          " double converted = condition ?"
                                                          " (_Bool)1 : 2.0;"
                                                          " int nested = condition ?"
                                                          " (condition ? 6 : 7) : 8;"
                                                          " int selected = condition &&"
                                                          " (condition ?"
                                                          " pick(&item)->thread_local :"
                                                          " pick(&item)->values[0]);"
                                                          " int conditional_truth = 0;"
                                                          " if (condition ? selected : nested)"
                                                          " { conditional_truth = 1; }"
                                                          " switch ('a')"
                                                          " { case 'a': conditional_truth += 1;"
                                                          " break; default: break; }"
                                                          " return pick(&item)->values["
                                                          " condition ? 0u : 1u]"
                                                          " + selected_values[0]"
                                                          " + item.thread_local + text[0]"
                                                          " + nested + selected"
                                                          " + conditional_truth"
                                                          " + pointer_order"
                                                          " + local_pair.value"
                                                          " + (int)converted + PICK;"
                                                          " }"),
                                                       (CPreprocessOptions){0});
    CParseResult regression_parse = c_parse(regression_temporary.arena, regression_tokens);
    CIRLowerResult regression_ir = c_lower_to_ir(regression_temporary.arena, S8("c-regressions.c"), regression_tokens, regression_parse, target_native);
    BUSTER_TEST(arguments, regression_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, regression_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, regression_ir.diagnostic_count == 0);
    if (regression_ir.program)
    {
        IrModule* regression_module = &regression_ir.program->modules[0];
        BUSTER_TEST(arguments, regression_module->function_count >= 2);
        BUSTER_TEST(arguments, regression_module->global_count >= 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(regression_ir.program, regression_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(regression_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_scratch_and_hardening(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    Arena* scratch_lifetime_arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_MB(256),
    });
    enum
    {
        C_IR_SCRATCH_STRESS_TYPE_COUNT = 4096,
        C_IR_SCRATCH_STRESS_EXPRESSION_COUNT = 4096,
    };
    u32 scratch_fragment_capacity = C_IR_SCRATCH_STRESS_TYPE_COUNT + C_IR_SCRATCH_STRESS_EXPRESSION_COUNT + 2;
    String8* scratch_fragments = arena_allocate(scratch_lifetime_arena, String8, scratch_fragment_capacity);
    u32 scratch_fragment_count = 0;
    for (u32 index = 0; index < C_IR_SCRATCH_STRESS_TYPE_COUNT; index += 1)
    {
        scratch_fragments[scratch_fragment_count++] = string_format(scratch_lifetime_arena,
                                                                    S8("struct Padding{u32}"
                                                                       " {{ int value; }};"),
                                                                    index);
    }
    scratch_fragments[scratch_fragment_count++] = S8("struct ScratchItem { int value; };"
                                                     " static struct ScratchItem *"
                                                     "scratch_identity("
                                                     "struct ScratchItem *item)"
                                                     " { return item; }"
                                                     " int scratch_stress(void)"
                                                     " { struct ScratchItem item = { 0 };");
    for (u32 index = 0; index < C_IR_SCRATCH_STRESS_EXPRESSION_COUNT; index += 1)
    {
        scratch_fragments[scratch_fragment_count++] = string_format(scratch_lifetime_arena,
                                                                    S8("item.value +="
                                                                       " scratch_identity(&item)"
                                                                       "->value +"
                                                                       " (int)sizeof("
                                                                       "struct Padding{u32});"),
                                                                    index);
    }
    scratch_fragments[scratch_fragment_count++] = S8("return item.value; }");
    BUSTER_TEST(arguments, scratch_fragment_count == scratch_fragment_capacity);
    String8 scratch_lifetime_source = string_join_arena(scratch_lifetime_arena,
                                                        (SliceString8){
                                                            .pointer = scratch_fragments,
                                                            .length = scratch_fragment_count,
                                                        },
                                                        false);
    CPreprocessResult scratch_lifetime_tokens = c_preprocess(scratch_lifetime_arena, scratch_lifetime_source, (CPreprocessOptions){0});
    CParseResult scratch_lifetime_parse = c_parse(scratch_lifetime_arena, scratch_lifetime_tokens);
    CIRLowerResult scratch_lifetime_ir =
        c_lower_to_ir(scratch_lifetime_arena, S8("scratch-lifetime.c"), scratch_lifetime_tokens, scratch_lifetime_parse, target_native);
    BUSTER_TEST(arguments, scratch_lifetime_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, scratch_lifetime_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, scratch_lifetime_ir.diagnostic_count == 0);
    if (scratch_lifetime_ir.program)
    {
        IrModule* scratch_lifetime_module = &scratch_lifetime_ir.program->modules[0];
        BUSTER_TEST(arguments, scratch_lifetime_module->lowered_function_count == 2);
        BUSTER_TEST(arguments, ir_validate_canonical_module(scratch_lifetime_ir.program, scratch_lifetime_module).error == IR_VALIDATION_NONE);
    }
    bool scratch_lifetime_arena_destroyed = arena_destroy(scratch_lifetime_arena, 1);
    BUSTER_TEST(arguments, scratch_lifetime_arena_destroyed);
    TemporalArena hardening_temporary = scratch_begin(0, 0);
    CPreprocessResult hardening_tokens = c_preprocess(hardening_temporary.arena,
                                                      S8("typedef unsigned long Word;"
                                                         " typedef enum Kind"
                                                         " { KIND_VALUE = 1 } Kind;"
                                                         " typedef struct Iterator Iterator;"
                                                         " struct Iterator"
                                                         " { char const *source; Word index; };"
                                                         " typedef struct Duplicate Duplicate;"
                                                         " struct Duplicate { int value; };"
                                                         " typedef Duplicate Duplicate;"
                                                         " typedef union Constant"
                                                         " { struct { Word integer; };"
                                                         " double floating; } Constant;"
                                                         " typedef struct Pair"
                                                         " { int left; int right; } Pair;"
                                                         " typedef struct NamedInline"
                                                         " { int prefix;"
                                                         " union { int integer; double real; }"
                                                         " payload;"
                                                         " int values[2]; unsigned length;"
                                                         " struct NamedInline *pointers[2];"
                                                         " int (*callback)(int); } NamedInline;"
                                                         " typedef NamedInline NamedInline;"
                                                         " typedef struct NamedContainer"
                                                         " { NamedInline stacks; } NamedContainer;"
                                                         "\n#define SAME_LOCATION_LOCAL(name) \\\n"
                                                         " int name(int *value)"
                                                         " { int *result = value;"
                                                         " return result != 0; }\n"
                                                         "SAME_LOCATION_LOCAL(macro_local_first)\n"
                                                         "SAME_LOCATION_LOCAL(macro_local_second)\n"
                                                         " NamedInline named_zero = { 0 };"
                                                         " NamedContainer *named_global;"
                                                         " static char bound_source[7];"
                                                         " static char *bound_target["
                                                         "sizeof(bound_source) /"
                                                         " sizeof(bound_source[0])];"
                                                         " static int consume(Kind *kind)"
                                                         " { return *kind; }"
                                                         " int first(int left);"
                                                         " int second(int right);"
                                                         " int first(int actual)"
                                                         " { return actual; }"
                                                         " int second(int actual)"
                                                         " { return actual + 1; }"
                                                         " int update(Word *index, int *values)"
                                                         " { int prior = values[(*index)++];"
                                                         " return prior + (int)(--*index)"
                                                         " + consume(&(Kind){ KIND_VALUE }); }"
                                                         " int qualified("
                                                         " Iterator * restrict iterator)"
                                                         " { return *(char const *)"
                                                         " iterator->source; }"
                                                         " Word promoted(void)"
                                                         " { Constant value ="
                                                         " (Constant){ .integer = 4 };"
                                                         " return sizeof(value.integer)"
                                                         " + value.integer; }"
                                                         " int aggregate_select(int condition)"
                                                         " { Pair left = { 1, 2 };"
                                                         " Pair right = { 3, 4 };"
                                                         " Pair selected ="
                                                         " condition ? left : right;"
                                                         " return selected.left; }"
                                                         " int named_inline(NamedInline *value)"
                                                         " { return value->payload.integer; }"
                                                         " int named_global_chain(void)"
                                                         " { NamedInline *result ="
                                                         " named_global->stacks.pointers["
                                                         " --named_global->stacks.length];"
                                                         " return result != 0; }"
                                                         " int duplicate_typedef(void)"
                                                         " { Duplicate *result = 0;"
                                                         " return result != 0; }"
                                                         " unsigned named_offset(void)"
                                                         " { return __builtin_offsetof("
                                                         "NamedInline, pointers[1]); }"
                                                         " int condition_assignment(int *next)"
                                                         " { int *event;"
                                                         " while ((event = next))"
                                                         " { return event != 0; }"
                                                         " return 0; }"
                                                         " float grouped_cast_select("
                                                         "NamedInline *value,"
                                                         " unsigned index)"
                                                         " { return"
                                                         " (((float *)&value->payload.real)"
                                                         "[index] > 0.0f)"
                                                         " ? ((float *)&value->payload.real)"
                                                         "[index]"
                                                         " : ((float *)&value->payload.real)"
                                                         "[0]; }"
                                                         " unsigned object_size_bound(void)"
                                                         " { return sizeof(bound_target) /"
                                                         " sizeof(bound_target[0]); }"
                                                         " int local_string_array(void)"
                                                         " { ; const char text[] = \"a\\0b\";"
                                                         " return (int)sizeof(text) + text[2]; }"
                                                         " unsigned inline_assembly(unsigned leaf)"
                                                         " { unsigned a = leaf, b = 0, c = 0, d = 0;"
                                                         " __asm__ volatile (\"cpuid\""
                                                         " : [leaf] \"+a\"(a), \"=b\"(b),"
                                                         " \"=c\"(c), \"=d\"(d)"
                                                         " : \"c\"(0) : \"memory\", \"cc\");"
                                                         " return a ^ b ^ c ^ d; }"
                                                         " void inline_trap(void)"
                                                         " { __asm__ __volatile__(\"ud2\"); }"
                                                         " extern int global_asm_value(void);"
                                                         " __asm__(\".text\\n\""
                                                         "\".globl global_asm_value\\n\""
                                                         "\"global_asm_value:\\n\""
                                                         "\"movl $37, %eax\\n\""
                                                         "\"ret\\n\");"
                                                         " extern int asm_labeled(void)"
                                                         " __asm__(\"external_asm_name\");"
                                                         " int call_asm_labeled(void)"
                                                         " { return asm_labeled(); }"
                                                         " int no_return(void)"
                                                         " { do { __builtin_unreachable();"
                                                         " } while (0); }"),
                                                      (CPreprocessOptions){0});
    CParseResult hardening_parse = c_parse(hardening_temporary.arena, hardening_tokens);
    CIRLowerResult hardening_ir = c_lower_to_ir(hardening_temporary.arena, S8("frontend-hardening.c"), hardening_tokens, hardening_parse, target_native);
    BUSTER_TEST(arguments, hardening_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, hardening_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, hardening_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, hardening_ir.program != 0);
    if (hardening_ir.program)
    {
        IrModule* hardening_module = &hardening_ir.program->modules[0];
        BUSTER_TEST(arguments, hardening_module->function_count == 23);
        BUSTER_TEST(arguments, hardening_module->assembly_count == 1);
        if (hardening_module->assembly_count == 1)
        {
            BUSTER_TEST(arguments, hardening_module->assemblies[0].source.length != 0);
            BUSTER_TEST(arguments, string_equal(hardening_module->assemblies[0].source, S8(".text\n"
                                                                                           ".globl global_asm_value\n"
                                                                                           "global_asm_value:\n"
                                                                                           "movl $37, %eax\n"
                                                                                           "ret\n")));
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(hardening_ir.program, hardening_module).error == IR_VALIDATION_NONE);
        u32 dereference_count = 0;
        u32 field_count = 0;
        u32 unreachable_count = 0;
        u32 inline_assembly_count = 0;
        bool found_asm_link_name = false;
        for (u32 symbol_index = 0; symbol_index < hardening_ir.program->symbols.count; symbol_index += 1)
        {
            found_asm_link_name |= string_equal(hardening_ir.program->symbols.symbols[symbol_index].link_name, S8("external_asm_name"));
        }
        for (u32 function_index = 0; function_index < hardening_module->function_count; function_index += 1)
        {
            IrFunction* function = hardening_module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = function->instructions[instruction_index].opcode;
                dereference_count += opcode == IR_OPCODE_DEREFERENCE;
                field_count += opcode == IR_OPCODE_FIELD;
                unreachable_count += opcode == IR_OPCODE_UNREACHABLE;
                inline_assembly_count += opcode == IR_OPCODE_INLINE_ASSEMBLY;
            }
        }
        BUSTER_TEST(arguments, dereference_count >= 4);
        BUSTER_TEST(arguments, field_count >= 3);
        BUSTER_TEST(arguments, unreachable_count >= 1);
        BUSTER_TEST(arguments, inline_assembly_count == 2);
        BUSTER_TEST(arguments, found_asm_link_name);
    }
    scratch_end(hardening_temporary);
    TemporalArena invalid_assembly_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_assembly_tokens =
        c_preprocess(invalid_assembly_temporary.arena, S8("__asm__(\"nop\", \"not adjacent\");"), (CPreprocessOptions){0});
    CParseResult invalid_assembly_parse = c_parse(invalid_assembly_temporary.arena, invalid_assembly_tokens);
    CIRLowerResult invalid_assembly_ir =
        c_lower_to_ir(invalid_assembly_temporary.arena, S8("invalid-global-assembly.c"), invalid_assembly_tokens, invalid_assembly_parse, target_native);
    BUSTER_TEST(arguments, invalid_assembly_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_assembly_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_assembly_ir.diagnostic_count == 1);
    BUSTER_TEST(arguments, invalid_assembly_ir.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
    BUSTER_TEST(arguments, invalid_assembly_ir.program != 0);
    if (invalid_assembly_ir.program)
    {
        BUSTER_TEST(arguments, invalid_assembly_ir.program->modules[0].assembly_count == 0);
    }
    scratch_end(invalid_assembly_temporary);
    TemporalArena alignas_temporary = scratch_begin(0, 0);
    CPreprocessResult alignas_tokens = c_preprocess(alignas_temporary.arena,
                                                    S8("_Alignas(64) int global_value = 1;"
                                                       " _Alignas(64) extern int redeclared;"
                                                       " _Alignas(64) int redeclared = 4;"
                                                       " struct Aligned {"
                                                       " char prefix;"
                                                       " _Alignas(32) int value;"
                                                       " };"
                                                       " int main(void) {"
                                                       " _Alignas(64) int local = 2;"
                                                       " static _Alignas(64) int saved = 3;"
                                                       " return global_value + local + saved;"
                                                       " }\n"),
                                                    (CPreprocessOptions){0});
    CParseResult alignas_parse = c_parse(alignas_temporary.arena, alignas_tokens);
    CIRLowerResult alignas_ir = c_lower_to_ir(alignas_temporary.arena, S8("alignas.c"), alignas_tokens, alignas_parse, target_native);
    BUSTER_TEST(arguments, alignas_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, alignas_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, alignas_ir.diagnostic_count == 0);
    if (alignas_ir.program)
    {
        IrModule* alignas_module = &alignas_ir.program->modules[0];
        bool found_global_alignment = false;
        bool found_static_alignment = false;
        bool found_local_alignment = false;
        bool found_member_alignment = false;
        IrGlobal* aligned_global = 0;
        IrValue* aligned_local = 0;
        for (u32 global_index = 0; global_index < alignas_module->global_count; global_index += 1)
        {
            IrGlobal* global = alignas_module->globals + global_index;
            found_global_alignment |= global->alignment == 64;
            if (!aligned_global && global->alignment == 64)
            {
                aligned_global = global;
            }
            found_static_alignment |=
                global->alignment == 64 && ir_symbol_from_id(&alignas_ir.program->symbols, global->symbol)->linkage == IR_LINKAGE_INTERNAL;
        }
        for (u32 type_index = 0; type_index < alignas_ir.program->types.count; type_index += 1)
        {
            IrType* type = alignas_ir.program->types.types + type_index;
            found_member_alignment |= type->kind == IR_TYPE_STRUCT && type->layout.alignment == 32 && type->field_count == 2 && type->fields[1].offset == 32;
        }
        for (u32 function_index = 0; function_index < alignas_module->function_count; function_index += 1)
        {
            IrFunction* function = alignas_module->functions + function_index;
            for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
            {
                found_local_alignment |= function->values[value_index].alignment == 64;
                if (!aligned_local && function->values[value_index].alignment == 64)
                {
                    aligned_local = &function->values[value_index];
                }
            }
        }
        BUSTER_TEST(arguments, found_global_alignment);
        BUSTER_TEST(arguments, found_static_alignment);
        BUSTER_TEST(arguments, found_local_alignment);
        BUSTER_TEST(arguments, found_member_alignment);
        BUSTER_TEST(arguments, ir_validate_canonical_module(alignas_ir.program, alignas_module).error == IR_VALIDATION_NONE);
        if (aligned_global)
        {
            u32 alignment = aligned_global->alignment;
            aligned_global->alignment = 3;
            BUSTER_TEST(arguments, ir_validate_canonical_module(alignas_ir.program, alignas_module).error == IR_VALIDATION_ALIGNMENT);
            aligned_global->alignment = alignment;
        }
        if (aligned_local)
        {
            u32 alignment = aligned_local->alignment;
            aligned_local->alignment = 3;
            BUSTER_TEST(arguments, ir_validate_canonical_module(alignas_ir.program, alignas_module).error == IR_VALIDATION_ALIGNMENT);
            aligned_local->alignment = alignment;
        }
    }
    scratch_end(alignas_temporary);
    // An alignment specifier is part of the declaration specifiers, so the
    // specifier scan has to step over it to reach the type whether the type is
    // a builtin keyword, a typedef name, or a struct tag. Only the keyword
    // spelling used to work; the other two lost the declarator entirely and
    // reported the declared name as undeclared.
    TemporalArena alignas_typedef_temporary = scratch_begin(0, 0);
    CPreprocessResult alignas_typedef_tokens = c_preprocess(alignas_typedef_temporary.arena,
                                                            S8("typedef unsigned char u8;"
                                                               " static _Alignas(64) u8 file_scope_array[64];"
                                                               " typedef struct Bytes { _Alignas(64) u8 bytes[64]; } Bytes;"
                                                               " struct Tag { int value; };"
                                                               " _Alignas(64) struct Tag tag_aligned;"
                                                               " u8 _Alignas(64) trailing_specifier;"
                                                               " int main(void) {"
                                                               " _Alignas(64) u8 local_array[64];"
                                                               " Bytes bytes;"
                                                               " local_array[0] = 1;"
                                                               " bytes.bytes[0] = 2;"
                                                               " return file_scope_array[0] + local_array[0] + bytes.bytes[0] + tag_aligned.value + trailing_specifier;"
                                                               " }\n"),
                                                            (CPreprocessOptions){0});
    CParseResult alignas_typedef_parse = c_parse(alignas_typedef_temporary.arena, alignas_typedef_tokens);
    CIRLowerResult alignas_typedef_ir =
        c_lower_to_ir(alignas_typedef_temporary.arena, S8("alignas-typedef.c"), alignas_typedef_tokens, alignas_typedef_parse, target_native);
    BUSTER_TEST(arguments, alignas_typedef_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, alignas_typedef_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, alignas_typedef_ir.diagnostic_count == 0);
    if (alignas_typedef_ir.program)
    {
        IrModule* alignas_typedef_module = &alignas_typedef_ir.program->modules[0];
        u32 aligned_global_count = 0;
        bool found_aligned_local = false;
        bool found_aligned_struct = false;
        for (u32 global_index = 0; global_index < alignas_typedef_module->global_count; global_index += 1)
        {
            aligned_global_count += alignas_typedef_module->globals[global_index].alignment == 64;
        }
        for (u32 type_index = 0; type_index < alignas_typedef_ir.program->types.count; type_index += 1)
        {
            IrType* type = alignas_typedef_ir.program->types.types + type_index;
            found_aligned_struct |= type->kind == IR_TYPE_STRUCT && type->layout.alignment == 64 && type->layout.size == 64 && type->field_count == 1;
        }
        for (u32 function_index = 0; function_index < alignas_typedef_module->function_count; function_index += 1)
        {
            IrFunction* function = alignas_typedef_module->functions + function_index;
            for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
            {
                found_aligned_local |= function->values[value_index].alignment == 64;
            }
        }
        // file_scope_array, tag_aligned, and trailing_specifier.
        BUSTER_TEST(arguments, aligned_global_count == 3);
        BUSTER_TEST(arguments, found_aligned_local);
        BUSTER_TEST(arguments, found_aligned_struct);
        BUSTER_TEST(arguments, ir_validate_canonical_module(alignas_typedef_ir.program, alignas_typedef_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(alignas_typedef_temporary);
    TemporalArena invalid_alignas_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_alignas_tokens = c_preprocess(invalid_alignas_temporary.arena, S8("_Alignas(3) int value;\n"), (CPreprocessOptions){0});
    CParseResult invalid_alignas_parse = c_parse(invalid_alignas_temporary.arena, invalid_alignas_tokens);
    CIRLowerResult invalid_alignas_ir =
        c_lower_to_ir(invalid_alignas_temporary.arena, S8("invalid-alignas.c"), invalid_alignas_tokens, invalid_alignas_parse, target_native);
    BUSTER_TEST(arguments, invalid_alignas_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_alignas_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_alignas_ir.diagnostic_count == 1);
    if (invalid_alignas_ir.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, invalid_alignas_ir.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
    }
    scratch_end(invalid_alignas_temporary);
    {
        String8 invalid_redeclaration_sources[] = {
            S8("_Alignas(64) extern int value; _Alignas(32) int value = 1;\n"),
            S8("_Alignas(64) extern int value; int value = 1;\n"),
            S8("_Alignas(1) int value;\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_redeclaration_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_redeclaration_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            CIRLowerResult invalid_ir = c_lower_to_ir(temporary.arena, S8("invalid-alignas-redeclaration.c"), invalid_tokens, invalid_parse, target_native);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_ir.diagnostic_count == 1);
            if (invalid_ir.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_ir.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
            }
            scratch_end(temporary);
        }
    }
    {
        String8 invalid_alignment_sources[] = {
            S8("_Alignas(16) typedef int Aligned;\n"),
            S8("_Alignas(16) int function(void);\n"),
            S8("int function(_Alignas(16) int value);\n"),
            S8("struct Value { _Alignas(8) unsigned field : 1; };\n"),
            S8("struct Value { unsigned field : 1 __attribute__((aligned(8))); };\n"),
            S8("int function(void) { register _Alignas(16) int value; return 0; }\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_alignment_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_alignment_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 1);
            if (invalid_parse.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
            }
            scratch_end(temporary);
        }
    }
    {
        // A GNU `aligned(N)` only ever raises, so a request the type already
        // satisfies is the no-op GCC and Clang make of it rather than a
        // rejection -- which used to leave the aggregate with no layout at
        // all, and the folded `sizeof` four bytes short of the object it
        // sizes (#689).  The static assertion is the parse-side folding
        // engine's answer and the field offsets are the IR one's; they run
        // over the same records and must agree.
        String8 ignored_alignment_sources[] = {
            S8("struct Aligned { char byte; __attribute__((aligned(2))) int value; };\n"
               "_Static_assert(sizeof(struct Aligned) == 8, \"specifier position\");\n"),
            S8("struct Aligned { char byte; int value __attribute__((aligned(2))); };\n"
               "_Static_assert(sizeof(struct Aligned) == 8, \"declarator position\");\n"),
            S8("struct Aligned { char byte; int value; } __attribute__((aligned(2)));\n"
               "_Static_assert(sizeof(struct Aligned) == 8, \"aggregate position\");\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(ignored_alignment_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult ignored_tokens = c_preprocess(temporary.arena, ignored_alignment_sources[source_index], (CPreprocessOptions){0});
            CParseResult ignored_parse = c_parse(temporary.arena, ignored_tokens);
            CIRLowerResult ignored_ir = c_lower_to_ir(temporary.arena, S8("ignored-alignment.c"), ignored_tokens, ignored_parse, target_native);
            BUSTER_TEST(arguments, ignored_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, ignored_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, ignored_ir.diagnostic_count == 0);
            bool found_aligned_record = false;
            for (u32 type_index = 0; ignored_ir.program && type_index < ignored_ir.program->types.count; type_index += 1)
            {
                IrType* type = ignored_ir.program->types.types + type_index;
                found_aligned_record |= type->kind == IR_TYPE_STRUCT && type->field_count == 2 && type->layout.size == 8 &&
                                        type->layout.alignment == 4 && type->fields[1].offset == 4;
            }
            BUSTER_TEST(arguments, found_aligned_record);
            scratch_end(temporary);
        }
    }
    {
        // An object declarator asking for less than its type already has is
        // accepted for the same reason, and is placed at the type's own
        // alignment.  `_Alignas` mixed in raises the maximum above the natural
        // alignment, which is what Clang measures the request by.
        String8 ignored_object_sources[] = {
            S8("int value __attribute__((aligned(2)));\n"),
            S8("_Alignas(8) _Alignas(2) int value;\n"),
            S8("_Alignas(2) __attribute__((aligned(8))) int value;\n"),
        };
        u32 ignored_object_alignments[] = {4, 8, 8};
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(ignored_object_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult ignored_tokens = c_preprocess(temporary.arena, ignored_object_sources[source_index], (CPreprocessOptions){0});
            CParseResult ignored_parse = c_parse(temporary.arena, ignored_tokens);
            CIRLowerResult ignored_ir = c_lower_to_ir(temporary.arena, S8("ignored-object-alignment.c"), ignored_tokens, ignored_parse, target_native);
            BUSTER_TEST(arguments, ignored_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, ignored_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, ignored_ir.diagnostic_count == 0);
            bool found_object = false;
            if (ignored_ir.program)
            {
                IrModule* module = &ignored_ir.program->modules[0];
                for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
                {
                    found_object |= module->globals[global_index].alignment == ignored_object_alignments[source_index];
                }
            }
            BUSTER_TEST(arguments, found_object);
            scratch_end(temporary);
        }
    }
    {
        // `_Alignas` is the other rule: C requires a declaration's alignment
        // to be at least the natural one, so a smaller request is reported
        // against the specifier rather than left for a later component to
        // blame a missing layout on.  One diagnostic per rejected
        // declaration, at every position one can be written.
        String8 below_natural_alignas_sources[] = {
            S8("_Alignas(2) int value;\n"),
            S8("struct Value { char byte; _Alignas(2) int value; }; struct Value object;\n"),
            S8("int function(void) { _Alignas(2) int value; return value; }\n"),
            S8("struct Value { char byte; __attribute__((aligned(3))) int value; }; struct Value object;\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(below_natural_alignas_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult below_tokens = c_preprocess(temporary.arena, below_natural_alignas_sources[source_index], (CPreprocessOptions){0});
            CParseResult below_parse = c_parse(temporary.arena, below_tokens);
            CIRLowerResult below_ir = c_lower_to_ir(temporary.arena, S8("below-natural-alignment.c"), below_tokens, below_parse, target_native);
            BUSTER_TEST(arguments, below_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, below_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, below_ir.diagnostic_count == 1);
            if (below_ir.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, below_ir.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
            }
            scratch_end(temporary);
        }
    }
    {
        String8 valid_flexible_array_source = S8("struct Packet {"
                                                 " unsigned short tag;"
                                                 " unsigned char bytes[];"
                                                 "};\n");
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult valid_tokens = c_preprocess(temporary.arena, valid_flexible_array_source, (CPreprocessOptions){0});
        CParseResult valid_parse = c_parse(temporary.arena, valid_tokens);
        CIRLowerResult valid_ir = c_lower_to_ir(temporary.arena, S8("valid-flexible-array.c"), valid_tokens, valid_parse, target_native);
        BUSTER_TEST(arguments, valid_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, valid_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, valid_ir.diagnostic_count == 0);
        bool found_flexible_structure = false;
        if (valid_ir.program)
        {
            for (u32 type_index = 0; type_index < valid_ir.program->types.count; type_index += 1)
            {
                IrType* type = valid_ir.program->types.types + type_index;
                found_flexible_structure |= type->kind == IR_TYPE_STRUCT && type->field_count == 2 && type->layout.size == 2 && type->layout.alignment == 2 &&
                                            type->fields[1].offset == 2 &&
                                            ir_type_from_id(&valid_ir.program->types, type->fields[1].type)->kind == IR_TYPE_ARRAY &&
                                            ir_type_from_id(&valid_ir.program->types, type->fields[1].type)->element_count == 0;
            }
        }
        BUSTER_TEST(arguments, found_flexible_structure);
        scratch_end(temporary);
    }
    {
        String8 invalid_flexible_array_sources[] = {
            S8("union Packet { int tag; unsigned char bytes[]; };\n"),
            S8("struct Packet { unsigned char bytes[]; int tag; };\n"),
            S8("struct Packet { unsigned char bytes[]; };\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_flexible_array_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_flexible_array_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 1);
            if (invalid_parse.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_FLEXIBLE_ARRAY_MEMBER);
            }
            scratch_end(temporary);
        }
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        Target clear_cache_target = target_native;
        clear_cache_target.cpu_arch = CPU_ARCH_AARCH64;
        clear_cache_target.os = OPERATING_SYSTEM_LINUX;
        CPreprocessResult clear_cache_tokens = c_preprocess(temporary.arena,
                                                            S8("void clear_cache(char *begin, char *end)"
                                                               " { __builtin___clear_cache(begin, end); }\n"),
                                                            (CPreprocessOptions){
                                                                .target = clear_cache_target,
                                                            });
        CParseResult clear_cache_parse = c_parse(temporary.arena, clear_cache_tokens);
        CIRLowerResult clear_cache_ir = c_lower_to_ir(temporary.arena, S8("clear-cache.c"), clear_cache_tokens, clear_cache_parse, clear_cache_target);
        BUSTER_TEST(arguments, clear_cache_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, clear_cache_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, clear_cache_ir.diagnostic_count == 0);
        u32 clear_cache_count = 0;
        if (clear_cache_ir.program)
        {
            IrModule* module = clear_cache_ir.program->modules;
            BUSTER_TEST(arguments, ir_validate_canonical_module(clear_cache_ir.program, module).error == IR_VALIDATION_NONE);
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    clear_cache_count += function->instructions[instruction_index].opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE;
                }
            }
        }
        BUSTER_TEST(arguments, clear_cache_count == 1);
        scratch_end(temporary);
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult atomic_tokens = c_preprocess(temporary.arena,
                                                       S8("_Atomic(int) counter;"
                                                          " const _Atomic(unsigned long) total;"
                                                          " _Atomic(int *) pointer;"
                                                          " typedef _Atomic(short) AtomicShort;"
                                                          " AtomicShort value;"
                                                          " int atomic_round_trip(void) {"
                                                          " _Atomic(int) local = 3;"
                                                          " local = local + 4;"
                                                          " local += 2;"
                                                          " local++;"
                                                          " ++local;"
                                                          " return local;"
                                                          " }\n"),
                                                       (CPreprocessOptions){0});
        CParseResult atomic_parse = c_parse(temporary.arena, atomic_tokens);
        CIRLowerResult atomic_ir = c_lower_to_ir(temporary.arena, S8("atomic-types.c"), atomic_tokens, atomic_parse, target_native);
        BUSTER_TEST(arguments, atomic_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, atomic_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, atomic_ir.diagnostic_count == 0);
        u32 atomic_object_count = 0;
        bool found_atomic_pointer = false;
        bool found_const_atomic = false;
        for (u32 entity_index = 0; entity_index < atomic_parse.entity_count; entity_index += 1)
        {
            CEntity* entity = atomic_parse.entities + entity_index;
            if (entity->kind != C_ENTITY_OBJECT || entity->scope.value != 0 || entity->type.value >= atomic_parse.type_count)
            {
                continue;
            }
            CType* type = atomic_parse.types + entity->type.value;
            if (!type->is_atomic)
            {
                continue;
            }
            atomic_object_count += 1;
            found_atomic_pointer |= type->kind == C_TYPE_POINTER;
            found_const_atomic |= type->is_const;
        }
        BUSTER_TEST(arguments, atomic_object_count == 4);
        BUSTER_TEST(arguments, found_atomic_pointer);
        BUSTER_TEST(arguments, found_const_atomic);
        bool found_atomic_ir_type = false;
        u32 atomic_load_count = 0;
        u32 atomic_store_count = 0;
        u32 atomic_rmw_count = 0;
        if (atomic_ir.program)
        {
            for (u32 type_index = 0; type_index < atomic_ir.program->types.count; type_index += 1)
            {
                IrType* type = atomic_ir.program->types.types + type_index;
                found_atomic_ir_type |= type->is_atomic && type->unqualified_type.value < atomic_ir.program->types.count && type->layout.resolved;
            }
            IrModule* module = atomic_ir.program->modules;
            BUSTER_TEST(arguments, ir_validate_canonical_module(atomic_ir.program, module).error == IR_VALIDATION_NONE);
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrOpcode opcode = function->instructions[instruction_index].opcode;
                    atomic_load_count += opcode == IR_OPCODE_ATOMIC_LOAD;
                    atomic_store_count += opcode == IR_OPCODE_ATOMIC_STORE;
                    atomic_rmw_count += opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE;
                }
            }
        }
        BUSTER_TEST(arguments, found_atomic_ir_type);
        BUSTER_TEST(arguments, atomic_load_count == 2);
        BUSTER_TEST(arguments, atomic_store_count == 2);
        BUSTER_TEST(arguments, atomic_rmw_count == 3);
        scratch_end(temporary);
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult atomic_builtin_tokens = c_preprocess(temporary.arena,
                                                               S8("int atomic_builtin_ir(void) {"
                                                                  " _Atomic(int) value;"
                                                                  " int expected = 1;"
                                                                  " __c11_atomic_init(&value, 1);"
                                                                  " int previous = __c11_atomic_exchange("
                                                                  " &value, 2, __ATOMIC_ACQ_REL);"
                                                                  " int changed ="
                                                                  " __c11_atomic_compare_exchange_strong("
                                                                  " &value, &expected, 3,"
                                                                  " __ATOMIC_SEQ_CST,"
                                                                  " __ATOMIC_ACQUIRE);"
                                                                  " __c11_atomic_thread_fence("
                                                                  " __ATOMIC_RELEASE);"
                                                                  " __c11_atomic_signal_fence("
                                                                  " __ATOMIC_SEQ_CST);"
                                                                  " return previous + changed;"
                                                                  " }\n"),
                                                               (CPreprocessOptions){0});
        CParseResult parse = c_parse(temporary.arena, atomic_builtin_tokens);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("atomic-builtins.c"), atomic_builtin_tokens, parse, target_native);
        BUSTER_TEST(arguments, atomic_builtin_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        u32 exchange_count = 0;
        u32 compare_exchange_count = 0;
        u32 thread_fence_count = 0;
        u32 signal_fence_count = 0;
        bool compare_orders_valid = false;
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    exchange_count += instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE && instruction->atomic_operation == IR_ATOMIC_EXCHANGE;
                    compare_exchange_count += instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE;
                    if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
                    {
                        compare_orders_valid |=
                            instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL && instruction->failure_memory_order == IR_MEMORY_ORDER_ACQUIRE;
                    }
                    thread_fence_count += instruction->opcode == IR_OPCODE_ATOMIC_FENCE && !instruction->atomic_signal_fence;
                    signal_fence_count += instruction->opcode == IR_OPCODE_ATOMIC_FENCE && instruction->atomic_signal_fence;
                }
            }
        }
        BUSTER_TEST(arguments, exchange_count == 1);
        BUSTER_TEST(arguments, compare_exchange_count == 1);
        BUSTER_TEST(arguments, thread_fence_count == 1);
        BUSTER_TEST(arguments, signal_fence_count == 1);
        BUSTER_TEST(arguments, compare_orders_valid);
        scratch_end(temporary);
    }
    {
        String8 invalid_atomic_sources[] = {
            S8("_Atomic(void) value;\n"),
            S8("_Atomic(int[2]) value;\n"),
            S8("_Atomic(const int) value;\n"),
            S8("_Atomic(_Atomic(int)) value;\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_atomic_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_atomic_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 1);
            if (invalid_parse.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ATOMIC_TYPE);
            }
            scratch_end(temporary);
        }
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult conflict_tokens = c_preprocess(temporary.arena,
                                                         S8("extern _Atomic(int) counter;"
                                                            " extern int counter;\n"),
                                                         (CPreprocessOptions){0});
        CParseResult conflict_parse = c_parse(temporary.arena, conflict_tokens);
        BUSTER_TEST(arguments, conflict_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, conflict_parse.diagnostic_count == 1);
        if (conflict_parse.diagnostic_count == 1)
        {
            BUSTER_TEST(arguments, conflict_parse.diagnostics[0].kind == C_DIAGNOSTIC_CONFLICTING_DECLARATION);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_inline_assembly_volatile_ir(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult preprocess = {0};
    CParseResult parse = {0};
    CIRLowerResult lowered = c_test_lower_source(
        temporary.arena,
        S8("int plain_asm(void) { __asm__(\"nop\"); return 0; }\n"
           "int volatile_asm(void) { __asm__ volatile(\"nop\"); return 0; }\n"
           "int underscored_volatile_asm(void) { __asm__ __volatile__(\"nop\"); return 0; }\n"
           "int plain_asm_goto(int value) { __asm__ goto (\"\" ::: : target); return value; target: return value + 1; }\n"
           "int volatile_asm_goto(int value) { __asm__ volatile goto (\"\" ::: : target); return value; target: return value + 1; }\n"),
        S8("inline-assembly-volatile.c"), target_native, &preprocess, &parse);
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
    if (lowered.program)
    {
        IrModule* module = &lowered.program->modules[0];
        String8 names[] = {
            S8("plain_asm"),
            S8("volatile_asm"),
            S8("underscored_volatile_asm"),
            S8("plain_asm_goto"),
            S8("volatile_asm_goto"),
        };
        bool expected_volatile[] = {false, true, true, false, true};
        for (u32 function_index = 0; function_index < BUSTER_ARRAY_LENGTH(names); function_index += 1)
        {
            IrFunction* function = c_test_find_ir_function(module, names[function_index]);
            IrInstruction* assembly = 0;
            u32 assembly_count = 0;
            for (u32 instruction_index = 0; function && instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                {
                    assembly = instruction;
                    assembly_count += 1;
                }
            }
            BUSTER_TEST(arguments, function != 0);
            BUSTER_TEST(arguments, assembly_count == 1);
            BUSTER_TEST(arguments, assembly && assembly->volatile_access == expected_volatile[function_index]);
            if (function_index >= 3)
            {
                BUSTER_TEST(arguments, assembly && assembly->target_count != 0);
            }
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_frontend_vla_and_ir(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena vla_temporary = scratch_begin(0, 0);
    CPreprocessResult vla_tokens = c_preprocess(vla_temporary.arena,
                                                S8("int unspecified(int count,"
                                                   " int values[*]);"
                                                   " int size(int count) {"
                                                   " int values[count];"
                                                   " values[count - 1] = 7;"
                                                   " return (int)sizeof(values)"
                                                   " + values[count - 1];"
                                                   " }"
                                                   " int loop(int count) {"
                                                   " int total = 0;"
                                                   " for (int index = 0; index < 2;"
                                                   " index += 1) {"
                                                   " int values[count];"
                                                   " values[0] = index;"
                                                   " total += values[0];"
                                                   " }"
                                                   " return total;"
                                                   " }"
                                                   " int nested(int count) {"
                                                   " int result = 0;"
                                                   " { int values[count];"
                                                   " values[0] = 9;"
                                                   " result = values[0]; }"
                                                   " return result;"
                                                   " }"
                                                   " int matrix(int rows, int columns) {"
                                                   " int values[rows][columns];"
                                                   " values[rows - 1][columns - 1] = 11;"
                                                   " return (int)sizeof(values)"
                                                   " + (int)sizeof(values[0])"
                                                   " + values[rows - 1][columns - 1];"
                                                   " }"
                                                   " int matrix_parameter("
                                                   " int rows, int columns,"
                                                   " int values[static rows][columns]) {"
                                                   " values[rows - 1][columns - 1] = 13;"
                                                   " return (int)sizeof(values[0])"
                                                   " + values[rows - 1][columns - 1];"
                                                   " }\n"),
                                                (CPreprocessOptions){0});
    CParseResult vla_parse = c_parse(vla_temporary.arena, vla_tokens);
    CIRLowerResult vla_ir = c_lower_to_ir(vla_temporary.arena, S8("vla.c"), vla_tokens, vla_parse, target_native);
    BUSTER_TEST(arguments, vla_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, vla_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, vla_ir.diagnostic_count == 0);
    if (vla_ir.program)
    {
        IrModule* vla_module = &vla_ir.program->modules[0];
        u32 allocation_count = 0;
        u32 stack_save_count = 0;
        u32 stack_restore_count = 0;
        u32 runtime_multiply_count = 0;
        for (u32 function_index = 0; function_index < vla_module->function_count; function_index += 1)
        {
            IrFunction* function = vla_module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                allocation_count += instruction->opcode == IR_OPCODE_STACK_ALLOCATE;
                stack_save_count += instruction->opcode == IR_OPCODE_STACK_SAVE;
                stack_restore_count += instruction->opcode == IR_OPCODE_STACK_RESTORE;
                runtime_multiply_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_INTEGER_MULTIPLY;
            }
        }
        BUSTER_TEST(arguments, allocation_count == 4);
        BUSTER_TEST(arguments, stack_save_count == 4);
        BUSTER_TEST(arguments, stack_restore_count == 2);
        BUSTER_TEST(arguments, runtime_multiply_count >= 7);
        BUSTER_TEST(arguments, ir_validate_canonical_module(vla_ir.program, vla_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(vla_temporary);
    TemporalArena generic_temporary = scratch_begin(0, 0);
    CPreprocessResult generic_tokens = c_preprocess(generic_temporary.arena,
                                                    S8("static int selected(void)"
                                                       " { return 17; }"
                                                       " static int unselected(void)"
                                                       " { return 99; }"
                                                       " int main(void)"
                                                       " { int control = 0;"
                                                       " double floating = 1.0;"
                                                       " return _Generic(control++,"
                                                       " int: selected(),"
                                                       " default: unselected())"
                                                       " + _Generic(floating,"
                                                       " int: unselected(),"
                                                       " default: 3); }\n"),
                                                    (CPreprocessOptions){0});
    CParseResult generic_parse = c_parse(generic_temporary.arena, generic_tokens);
    CIRLowerResult generic_ir = c_lower_to_ir(generic_temporary.arena, S8("generic.c"), generic_tokens, generic_parse, target_native);
    BUSTER_TEST(arguments, generic_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, generic_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, generic_ir.diagnostic_count == 0);
    if (generic_ir.program)
    {
        IrModule* generic_module = &generic_ir.program->modules[0];
        IrFunction* generic_main = 0;
        for (u32 function_index = 0; function_index < generic_module->function_count; function_index += 1)
        {
            if (string_equal(generic_module->functions[function_index].name, S8("main")))
            {
                generic_main = generic_module->functions + function_index;
                break;
            }
        }
        BUSTER_TEST(arguments, generic_main != 0);
        if (generic_main)
        {
            u32 generic_call_count = 0;
            u32 generic_store_count = 0;
            for (u32 instruction_index = 0; instruction_index < generic_main->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = generic_main->instructions[instruction_index].opcode;
                generic_call_count += opcode == IR_OPCODE_CALL;
                generic_store_count += opcode == IR_OPCODE_STORE;
            }
            BUSTER_TEST(arguments, generic_call_count == 1);
            BUSTER_TEST(arguments, generic_store_count == 2);
            BUSTER_TEST(arguments, ir_validate_canonical_module(generic_ir.program, generic_module).error == IR_VALIDATION_NONE);
        }
    }
    scratch_end(generic_temporary);
    {
        TemporalArena nullptr_temporary = scratch_begin(0, 0);
        CPreprocessResult nullptr_tokens = c_preprocess(nullptr_temporary.arena,
                                                        S8("typedef typeof(nullptr) nullptr_t;"
                                                           " nullptr_t value = nullptr;"
                                                           " int *pointer = nullptr;"
                                                           " int classify(void) {"
                                                           " int *a = 1 ? nullptr : pointer;"
                                                           " nullptr_t b = 1 ? nullptr : (1 - 1);"
                                                           " if (a || b || nullptr != (2 - 2)) return 0;"
                                                           " return _Generic(nullptr,"
                                                           " nullptr_t: 1, default: 0);"
                                                           " }\n"),
                                                        (CPreprocessOptions){
                                                            .dialect = C_PREPROCESS_DIALECT_C23,
                                                        });
        CParseResult nullptr_parse = c_parse(nullptr_temporary.arena, nullptr_tokens);
        CIRLowerResult nullptr_ir = c_lower_to_ir(nullptr_temporary.arena, S8("nullptr.c"), nullptr_tokens, nullptr_parse, target_native);
        BUSTER_TEST(arguments, nullptr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, nullptr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, nullptr_ir.diagnostic_count == 0);
        bool found_nullptr_c_type = false;
        for (u32 type_index = 0; type_index < nullptr_parse.type_count; type_index += 1)
        {
            found_nullptr_c_type |= nullptr_parse.types[type_index].kind == C_TYPE_NULLPTR;
        }
        BUSTER_TEST(arguments, found_nullptr_c_type);
        bool found_nullptr_ir_type = false;
        if (nullptr_ir.program)
        {
            for (u32 type_index = 0; type_index < nullptr_ir.program->types.count; type_index += 1)
            {
                IrType* type = nullptr_ir.program->types.types + type_index;
                found_nullptr_ir_type |= type->kind == IR_TYPE_POINTER && type->is_nullptr && type->layout.size == 8 && type->layout.alignment == 8;
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(nullptr_ir.program, nullptr_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, found_nullptr_ir_type);
        scratch_end(nullptr_temporary);
    }
    {
        String8 invalid_nullptr_sources[] = {
            S8("typedef typeof(nullptr) nullptr_t;"
               " int main(void) {"
               " nullptr_t value = 0;"
               " return value == nullptr;"
               " }\n"),
            S8("int main(void) {"
               " return (int)nullptr;"
               " }\n"),
            S8("int main(void) {"
               " return *nullptr;"
               " }\n"),
            S8("int main(void) {"
               " return nullptr + 1;"
               " }\n"),
            S8("int main(void) {"
               " return nullptr == 1;"
               " }\n"),
            S8("int main(void) {"
               " return 1 ? nullptr : 1;"
               " }\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_nullptr_sources); case_index += 1)
        {
            TemporalArena invalid_temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(invalid_temporary.arena, invalid_nullptr_sources[case_index],
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C23,
                                                            });
            CParseResult invalid_parse = c_parse(invalid_temporary.arena, invalid_tokens);
            CIRLowerResult invalid_ir = c_lower_to_ir(invalid_temporary.arena, S8("invalid-nullptr.c"), invalid_tokens, invalid_parse, target_native);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_ir.diagnostic_count == 1);
            if (invalid_ir.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_ir.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
            }
            scratch_end(invalid_temporary);
        }
    }
    {
        TemporalArena c17_nullptr_temporary = scratch_begin(0, 0);
        CPreprocessResult c17_nullptr_tokens = c_preprocess(c17_nullptr_temporary.arena,
                                                            S8("int nullptr = 3;"
                                                               " int main(void) {"
                                                               " return nullptr - 3;"
                                                               " }\n"),
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C17,
                                                            });
        CParseResult c17_nullptr_parse = c_parse(c17_nullptr_temporary.arena, c17_nullptr_tokens);
        CIRLowerResult c17_nullptr_ir =
            c_lower_to_ir(c17_nullptr_temporary.arena, S8("c17-nullptr-identifier.c"), c17_nullptr_tokens, c17_nullptr_parse, target_native);
        BUSTER_TEST(arguments, c17_nullptr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_nullptr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_nullptr_ir.diagnostic_count == 0);
        if (c17_nullptr_ir.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(c17_nullptr_ir.program, c17_nullptr_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(c17_nullptr_temporary);
    }
    {
        TemporalArena constexpr_temporary = scratch_begin(0, 0);
        CPreprocessResult constexpr_tokens = c_preprocess(constexpr_temporary.arena,
                                                          S8("struct Pair { int x; int y; };"
                                                             " constexpr int count = 4;"
                                                             " constexpr int next = count + 1;"
                                                             " constexpr double exact = 1.5;"
                                                             " constexpr int values[] = { 1, 2, 3, 4 };"
                                                             " constexpr struct Pair pair = { count, next };"
                                                             " constexpr int *nothing = nullptr;"
                                                             " static_assert(next == 5);"
                                                             " int sized[count];"
                                                             " int main(void) {"
                                                             " constexpr int local = next + 1;"
                                                             " int automatic[local];"
                                                             " automatic[0] = local;"
                                                             " return automatic[0] == 6 &&"
                                                             " values[3] == 4 && pair.y == 5 &&"
                                                             " nothing == nullptr ? 0 : 1;"
                                                             " }\n"),
                                                          (CPreprocessOptions){
                                                              .dialect = C_PREPROCESS_DIALECT_C23,
                                                          });
        CParseResult constexpr_parse = c_parse(constexpr_temporary.arena, constexpr_tokens);
        CIRLowerResult constexpr_ir = c_lower_to_ir(constexpr_temporary.arena, S8("constexpr.c"), constexpr_tokens, constexpr_parse, target_native);
        BUSTER_TEST(arguments, constexpr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, constexpr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, constexpr_ir.diagnostic_count == 0);
        u32 constexpr_entity_count = 0;
        bool found_count = false;
        bool found_next = false;
        bool found_local = false;
        for (u32 entity_index = 0; entity_index < constexpr_parse.entity_count; entity_index += 1)
        {
            CEntity* entity = constexpr_parse.entities + entity_index;
            if (!entity->is_constexpr)
            {
                continue;
            }
            constexpr_entity_count += 1;
            BUSTER_TEST(arguments, entity->type.value < constexpr_parse.type_count);
            if (entity->type.value < constexpr_parse.type_count)
            {
                BUSTER_TEST(arguments, constexpr_parse.types[entity->type.value].is_const);
            }
            found_count |=
                string_equal(entity->name, S8("count")) && entity->has_constant_value && !entity->constant_is_negative && entity->constant_value == 4;
            found_next |= string_equal(entity->name, S8("next")) && entity->has_constant_value && entity->constant_value == 5;
            found_local |= string_equal(entity->name, S8("local")) && entity->has_constant_value && entity->constant_value == 6;
        }
        BUSTER_TEST(arguments, constexpr_entity_count == 7);
        BUSTER_TEST(arguments, found_count);
        BUSTER_TEST(arguments, found_next);
        BUSTER_TEST(arguments, found_local);
        if (constexpr_ir.program)
        {
            IrModule* module = constexpr_ir.program->modules;
            u32 read_only_globals = 0;
            u32 internal_globals = 0;
            for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
            {
                IrGlobal* global = module->globals + global_index;
                IrSymbol* symbol = ir_symbol_from_id(&constexpr_ir.program->symbols, global->symbol);
                bool named_constexpr =
                    symbol && (string_equal(symbol->name, S8("count")) || string_equal(symbol->name, S8("next")) || string_equal(symbol->name, S8("exact")) ||
                               string_equal(symbol->name, S8("values")) || string_equal(symbol->name, S8("pair")) || string_equal(symbol->name, S8("nothing")));
                if (!named_constexpr)
                {
                    continue;
                }
                read_only_globals += global->is_read_only;
                internal_globals += symbol->linkage == IR_LINKAGE_INTERNAL;
            }
            BUSTER_TEST(arguments, read_only_globals == 6);
            BUSTER_TEST(arguments, internal_globals == 6);
            BUSTER_TEST(arguments, ir_validate_canonical_module(constexpr_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(constexpr_temporary);
    }
    {
        // A C99 `static` array bound is a bound qualifier on a parameter, not
        // a storage class on the function that declares it, so it must not
        // reach the symbol's linkage. The specifier walk sees the whole
        // parameter list, which is why every one of these spellings used to
        // come out internal. The two genuinely `static` functions are the
        // other half of the answer: a walk that simply stopped finding
        // `static` would make the external count right and this one wrong.
        TemporalArena static_bound_temporary = scratch_begin(0, 0);
        CPreprocessResult static_bound_tokens =
            c_preprocess(static_bound_temporary.arena,
                         S8("void prototyped(char buffer[static 8], unsigned value);\n"
                            "void prototyped(char* buffer, unsigned value) { buffer[0] = (char)value; }\n"
                            "int defined(int values[static 4]) { return values[0]; }\n"
                            "long qualified(long values[const static 2]) { return values[1]; }\n"
                            "__attribute__((visibility(\"hidden\"))) int attributed(int values[static 2]) { return values[0]; }\n"
                            "static int internal_bound(int values[static 2]) { return values[1]; }\n"
                            "static int internal_plain(int value) { return value; }\n"
                            "int use(int* values) { return internal_bound(values) + internal_plain(values[0]); }\n"),
                         (CPreprocessOptions){0});
        CParseResult static_bound_parse = c_parse(static_bound_temporary.arena, static_bound_tokens);
        CIRLowerResult static_bound_ir =
            c_lower_to_ir(static_bound_temporary.arena, S8("static-array-bound.c"), static_bound_tokens, static_bound_parse, target_native);
        BUSTER_TEST(arguments, static_bound_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, static_bound_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, static_bound_ir.diagnostic_count == 0);
        if (static_bound_ir.program)
        {
            IrModule* static_bound_module = &static_bound_ir.program->modules[0];
            u32 external_definitions = 0;
            u32 internal_definitions = 0;
            for (u32 function_index = 0; function_index < static_bound_module->function_count; function_index += 1)
            {
                IrSymbol* symbol = ir_symbol_from_id(&static_bound_ir.program->symbols, static_bound_module->functions[function_index].symbol);
                if (!symbol || !symbol->is_definition)
                {
                    continue;
                }
                external_definitions += symbol->linkage == IR_LINKAGE_EXTERNAL;
                internal_definitions += symbol->linkage == IR_LINKAGE_INTERNAL;
            }
            BUSTER_TEST(arguments, external_definitions == 5);
            BUSTER_TEST(arguments, internal_definitions == 2);
        }
        scratch_end(static_bound_temporary);
    }
    {
        TemporalArena c17_constexpr_temporary = scratch_begin(0, 0);
        CPreprocessResult c17_constexpr_tokens = c_preprocess(c17_constexpr_temporary.arena,
                                                              S8("int constexpr = 3;"
                                                                 " int main(void) {"
                                                                 " return constexpr - 3;"
                                                                 " }\n"),
                                                              (CPreprocessOptions){
                                                                  .dialect = C_PREPROCESS_DIALECT_C17,
                                                              });
        CParseResult c17_constexpr_parse = c_parse(c17_constexpr_temporary.arena, c17_constexpr_tokens);
        CIRLowerResult c17_constexpr_ir =
            c_lower_to_ir(c17_constexpr_temporary.arena, S8("c17-constexpr-identifier.c"), c17_constexpr_tokens, c17_constexpr_parse, target_native);
        BUSTER_TEST(arguments, c17_constexpr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_constexpr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_constexpr_ir.diagnostic_count == 0);
        scratch_end(c17_constexpr_temporary);
    }
    {
        String8 invalid_constexpr_sources[] = {
            S8("constexpr int missing;\n"),
            S8("constexpr int function(void)"
               " { return 0; }\n"),
            S8("extern constexpr int external = 1;\n"),
            S8("_Thread_local constexpr int threaded = 1;\n"),
            S8("constexpr volatile int value = 1;\n"),
            S8("constexpr _Atomic(int) value = 1;\n"),
            S8("void f(int n) {"
               " constexpr int values[n] = { 1 };"
               " }\n"),
            S8("constexpr unsigned char value = 256;\n"),
            S8("constexpr unsigned long long value = -1;\n"),
            S8("constexpr _Bool value = 2;\n"),
            S8("constexpr int *pointer = (int *)1;\n"),
            S8("void f(int runtime) {"
               " constexpr int value = runtime;"
               " }\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_constexpr_sources); case_index += 1)
        {
            TemporalArena invalid_temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(invalid_temporary.arena, invalid_constexpr_sources[case_index],
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C23,
                                                            });
            CParseResult invalid_parse = c_parse(invalid_temporary.arena, invalid_tokens);
            bool found_constexpr_diagnostic = false;
            for (u32 diagnostic_index = 0; diagnostic_index < invalid_parse.diagnostic_count; diagnostic_index += 1)
            {
                found_constexpr_diagnostic |= invalid_parse.diagnostics[diagnostic_index].kind == C_DIAGNOSTIC_INVALID_CONSTEXPR;
            }
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, found_constexpr_diagnostic);
            scratch_end(invalid_temporary);
        }
    }
    {
        String8 invalid_constexpr_ir_sources[] = {
            S8("constexpr float value = 0.1;\n"),
            S8("static int target;"
               " struct Pointer { int *value; };"
               " constexpr struct Pointer pointer ="
               " { &target };\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_constexpr_ir_sources); case_index += 1)
        {
            TemporalArena invalid_temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(invalid_temporary.arena, invalid_constexpr_ir_sources[case_index],
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C23,
                                                            });
            CParseResult invalid_parse = c_parse(invalid_temporary.arena, invalid_tokens);
            CIRLowerResult invalid_ir = c_lower_to_ir(invalid_temporary.arena, S8("invalid-constexpr.c"), invalid_tokens, invalid_parse, target_native);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_ir.diagnostic_count == 1);
            if (invalid_ir.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_ir.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_CONSTEXPR);
            }
            scratch_end(invalid_temporary);
        }
    }
    {
        TemporalArena float_temporary = scratch_begin(0, 0);
        CPreprocessResult float_tokens = c_preprocess(float_temporary.arena,
                                                      S8("constexpr float value = 0.1f;"
                                                         " int main(void) {"
                                                         " return value == 0.1f ? 0 : 1;"
                                                         " }\n"),
                                                      (CPreprocessOptions){
                                                          .dialect = C_PREPROCESS_DIALECT_C23,
                                                      });
        CParseResult float_parse = c_parse(float_temporary.arena, float_tokens);
        CIRLowerResult float_ir = c_lower_to_ir(float_temporary.arena, S8("constexpr-float.c"), float_tokens, float_parse, target_native);
        BUSTER_TEST(arguments, float_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, float_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, float_ir.diagnostic_count == 0);
        if (float_ir.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(float_ir.program, float_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(float_temporary);
    }
    {
        String8 constexpr_mutations[] = {
            S8("constexpr int value = 1;"
               " int main(void) {"
               " value = 2; return value;"
               " }\n"),
            S8("constexpr int values[] = { 1 };"
               " int main(void) {"
               " values[0] += 1; return values[0];"
               " }\n"),
            S8("struct Pair { int x; };"
               " constexpr struct Pair pair = { 1 };"
               " int main(void) {"
               " pair.x++; return pair.x;"
               " }\n"),
            S8("int main(void) {"
               " constexpr int local = 1;"
               " ++local; return local;"
               " }\n"),
            S8("constexpr int value = 1;"
               " int main(void) {"
               " int const *pointer = &value;"
               " *pointer = 2; return value;"
               " }\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(constexpr_mutations); case_index += 1)
        {
            TemporalArena mutation_temporary = scratch_begin(0, 0);
            CPreprocessResult mutation_tokens = c_preprocess(mutation_temporary.arena, constexpr_mutations[case_index],
                                                             (CPreprocessOptions){
                                                                 .dialect = C_PREPROCESS_DIALECT_C23,
                                                             });
            CParseResult mutation_parse = c_parse(mutation_temporary.arena, mutation_tokens);
            CIRLowerResult mutation_ir = c_lower_to_ir(mutation_temporary.arena, S8("constexpr-mutation.c"), mutation_tokens, mutation_parse, target_native);
            BUSTER_TEST(arguments, mutation_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, mutation_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, mutation_ir.diagnostic_count == 1);
            scratch_end(mutation_temporary);
        }
    }
    {
        String8 c23_reserved_identifiers[] = {
            S8("nullptr"), S8("true"), S8("false"), S8("constexpr"), S8("typeof_unqual"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(c23_reserved_identifiers); case_index += 1)
        {
            TemporalArena reserved_temporary = scratch_begin(0, 0);
            String8 source = string_format(reserved_temporary.arena, S8("int {S8} = 0;\n"), c23_reserved_identifiers[case_index]);
            CPreprocessResult reserved_tokens = c_preprocess(reserved_temporary.arena, source,
                                                             (CPreprocessOptions){
                                                                 .dialect = C_PREPROCESS_DIALECT_C23,
                                                             });
            CParseResult reserved_parse = c_parse(reserved_temporary.arena, reserved_tokens);
            BUSTER_TEST(arguments, reserved_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, reserved_parse.diagnostic_count != 0);
            scratch_end(reserved_temporary);
        }
    }
    struct
    {
        String8 source;
        String8 message;
    } invalid_generic_cases[] = {
        {
            S8("int main(void) {"
               " return _Generic(1,"
               " default: 1, default: 2); }\n"),
            S8("in function 'main': _Generic selection has more than one default association"),
        },
        {
            S8("int main(void) {"
               " return _Generic(1,"
               " int: 1, signed int: 2); }\n"),
            S8("in function 'main': _Generic selection has multiple compatible type associations"),
        },
        {
            S8("int main(void) {"
               " return _Generic(1.0, int: 1); }\n"),
            S8("in function 'main': _Generic controlling type is not compatible with any association and no default was provided"),
        },
        {
            S8("int main(void) {"
               " return _Generic(1, void: 1,"
               " default: 2); }\n"),
            S8("in function 'main': _Generic association requires a complete object type"),
        },
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_generic_cases); case_index += 1)
    {
        TemporalArena invalid_generic_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_generic_tokens =
            c_preprocess(invalid_generic_temporary.arena, invalid_generic_cases[case_index].source, (CPreprocessOptions){0});
        CParseResult invalid_generic_parse = c_parse(invalid_generic_temporary.arena, invalid_generic_tokens);
        CIRLowerResult invalid_generic_ir =
            c_lower_to_ir(invalid_generic_temporary.arena, S8("invalid-generic.c"), invalid_generic_tokens, invalid_generic_parse, target_native);
        BUSTER_TEST(arguments, invalid_generic_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_generic_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_generic_ir.diagnostic_count == 1);
        if (invalid_generic_ir.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_generic_ir.diagnostics[0].message, invalid_generic_cases[case_index].message);
        }
        scratch_end(invalid_generic_temporary);
    }
    {
        TemporalArena direct_ir_temporary = scratch_begin(0, 0);
        CPreprocessResult direct_ir_tokens =
            c_preprocess(direct_ir_temporary.arena,
                         S8("typedef unsigned long Word;\n"
                            "typedef unsigned char Byte;\n"
                            "enum { BASE_INDEX = 3 };\n"
                            "struct Pair { char prefix; int values[3]; };\n"
                            "_Alignas(sizeof(int) * 2) static int aligned = (unsigned long)7;\n"
                            "static const int base = 4;\n"
                            "static int scalar = (unsigned long)(base + sizeof(Byte));\n"
                            "static int table[sizeof(struct Pair) / sizeof(int)] ="
                            " { [BASE_INDEX] = (int)(sizeof(Word) / sizeof(Byte)) };\n"
                            "static const int primitive_width = sizeof(long long) + sizeof(signed char) +"
                            " sizeof(_Bool) + sizeof(double);\n"
                            "static const int compound_width = sizeof((int[3]){ 1, 2, 3 });\n"
                            "static int compound_values[3] = { [1 + 1] = 9 };\n"
                            "int inspect(void) {\n"
                            "    struct Pair pair = { .values = { [1] = 5 } };\n"
                            "    int local[sizeof((pair).values) / sizeof((pair).values[0])];\n"
                            "    switch (sizeof((pair).values) / sizeof((pair).values[0])) {\n"
                            "        case BASE_INDEX: return local[1] + table[BASE_INDEX] + scalar + aligned;\n"
                            "        default: return compound_width + compound_values[2];\n"
                            "    }\n"
                            "}\n"),
                         (CPreprocessOptions){0});
        CParseResult direct_ir_parse = c_parse(direct_ir_temporary.arena, direct_ir_tokens);
        CIRLowerResult direct_ir = c_lower_to_ir(direct_ir_temporary.arena, S8("direct-ir-regression.c"), direct_ir_tokens, direct_ir_parse, target_native);
        BUSTER_TEST(arguments, direct_ir_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, direct_ir_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, direct_ir.diagnostic_count == 0);
        BUSTER_TEST(arguments, direct_ir.program != 0);
        if (direct_ir.program)
        {
            IrModule* module = &direct_ir.program->modules[0];
            bool found_aligned = false;
            bool found_base = false;
            bool found_scalar = false;
            bool found_table = false;
            bool found_primitive_width = false;
            bool found_compound_width = false;
            bool found_compound_values = false;
            u32 expected_word_bytes = target_native.os == OPERATING_SYSTEM_WINDOWS ? 4 : 8;
            BUSTER_TEST(arguments, module->global_count == 7);
            BUSTER_TEST(arguments, module->function_count == 1);
            for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
            {
                IrGlobal* global = module->globals + global_index;
                IrSymbol* symbol = ir_symbol_from_id(&direct_ir.program->symbols, global->symbol);
                if (!symbol)
                {
                    continue;
                }
                if (string_equal(symbol->name, S8("aligned")))
                {
                    found_aligned = true;
                    BUSTER_TEST(arguments, global->alignment == 8);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 7);
                }
                else if (string_equal(symbol->name, S8("base")))
                {
                    found_base = true;
                    BUSTER_TEST(arguments, global->is_read_only);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 4);
                }
                else if (string_equal(symbol->name, S8("scalar")))
                {
                    found_scalar = true;
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 5);
                }
                else if (string_equal(symbol->name, S8("table")))
                {
                    found_table = true;
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
                    BUSTER_TEST(arguments, global->bytes.length == 16);
                    if (global->bytes.pointer && global->bytes.length >= 4 * sizeof(u32))
                    {
                        u32 value = 0;
                        memcpy(&value, global->bytes.pointer + 3 * sizeof(value), sizeof(value));
                        BUSTER_TEST(arguments, value == expected_word_bytes);
                    }
                    else
                    {
                        BUSTER_TEST(arguments, false);
                    }
                }
                else if (string_equal(symbol->name, S8("primitive_width")))
                {
                    found_primitive_width = true;
                    BUSTER_TEST(arguments, global->is_read_only);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 18);
                }
                else if (string_equal(symbol->name, S8("compound_width")))
                {
                    found_compound_width = true;
                    BUSTER_TEST(arguments, global->is_read_only);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 12);
                }
                else if (string_equal(symbol->name, S8("compound_values")))
                {
                    found_compound_values = true;
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
                    BUSTER_TEST(arguments, global->bytes.length == 12);
                    if (global->bytes.pointer && global->bytes.length >= 3 * sizeof(u32))
                    {
                        u32 value = 0;
                        memcpy(&value, global->bytes.pointer + 2 * sizeof(value), sizeof(value));
                        BUSTER_TEST(arguments, value == 9);
                    }
                    else
                    {
                        BUSTER_TEST(arguments, false);
                    }
                }
            }
            BUSTER_TEST(arguments, found_aligned);
            BUSTER_TEST(arguments, found_base);
            BUSTER_TEST(arguments, found_scalar);
            BUSTER_TEST(arguments, found_table);
            BUSTER_TEST(arguments, found_primitive_width);
            BUSTER_TEST(arguments, found_compound_width);
            BUSTER_TEST(arguments, found_compound_values);
            IrFunction* inspect = module->functions;
            BUSTER_TEST(arguments, inspect->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, inspect->local_count >= 2);
            BUSTER_TEST(arguments, ir_validate_canonical_module(direct_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(direct_ir_temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_function_signatures(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("typedef struct WideStruct { long double value; } WideStruct;"
                        " typedef union WideUnion { long double value; int integer; } WideUnion;"
                        " typedef struct NestedStruct { WideStruct value; } NestedStruct;"
                        " typedef struct WideArray { long double values[2]; } WideArray;"
                        " typedef struct LargeStruct { long double values[2]; int tail; } LargeStruct;"
                        " typedef struct F64Struct { double value; } F64Struct;"
                        " long double long_double_round_trip(long double value) { return value; }"
                        " WideStruct struct_round_trip(WideStruct value) { return value; }"
                        " WideUnion union_round_trip(WideUnion value) { return value; }"
                        " NestedStruct nested_round_trip(NestedStruct value) { return value; }"
                        " WideArray array_round_trip(WideArray value) { return value; }"
                        " LargeStruct large_round_trip(LargeStruct value) { return value; }"
                        " long double *pointer_round_trip(long double *value) { return value; }"
                        " WideStruct *aggregate_pointer_round_trip(WideStruct *value) { return value; }"
                        " int decayed_array(long double values[2]) { return values != 0; }"
                        " F64Struct f64_round_trip(F64Struct value) { return value; }");
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-pc-windows-msvc"),
        S8("x86_64-apple-macos"),
        S8("aarch64-unknown-linux-gnu"),
        S8("aarch64-apple-macos"),
        S8("aarch64-pc-windows-msvc"),
    };
    String8 function_names[] = {
        S8("long_double_round_trip"),
        S8("struct_round_trip"),
        S8("union_round_trip"),
        S8("nested_round_trip"),
        S8("array_round_trip"),
        S8("large_round_trip"),
        S8("pointer_round_trip"),
        S8("aggregate_pointer_round_trip"),
        S8("decayed_array"),
        S8("f64_round_trip"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        Target target = parsed_target.target;
        bool wide_long_double = target_data_layout(target).long_double_type.bit_width > 64;
        bool f80_sysv = target.cpu_arch == CPU_ARCH_X86_64 &&
                        (target.os == OPERATING_SYSTEM_LINUX || target.os == OPERATING_SYSTEM_MACOS ||
                         target.os == OPERATING_SYSTEM_IOS) &&
                        wide_long_double;
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = c_preprocess(temporary.arena, source,
                                                    (CPreprocessOptions){
                                                        .target = target,
                                                        .data_layout = target_data_layout(target),
                                                    });
        CParseResult parse = c_parse(temporary.arena, preprocess);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, target_triples[target_index], preprocess, parse, target);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == (f80_sysv ? 0 : wide_long_double ? 6 : 0));
        for (u32 diagnostic_index = 0; diagnostic_index < lowered.diagnostic_count; diagnostic_index += 1)
        {
            CDiagnostic diagnostic = lowered.diagnostics[diagnostic_index];
            BUSTER_TEST(arguments, diagnostic.kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
            BUSTER_TEST(arguments,
                        string_starts_with_sequence(diagnostic.message,
                                                     S8("C IR lowering does not yet support the parameter or return value types of function '")));
        }
        BUSTER_TEST(arguments, lowered.program != 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            BUSTER_TEST(arguments, module->function_count == BUSTER_ARRAY_LENGTH(function_names));
            for (u32 function_index = 0; function_index < BUSTER_ARRAY_LENGTH(function_names); function_index += 1)
            {
                IrFunction* function = c_test_find_ir_function(module, function_names[function_index]);
                BUSTER_TEST(arguments, function != 0);
                if (!function)
                {
                    continue;
                }
                // Every one of these lowers on System V.  The single-member
                // wrapper and its nestings are the x87 pair; the union and
                // both arrays classify without an x87 class at all -- the
                // union because System V's merger prefers INTEGER over x87
                // and its X87_UP tail is then unaccompanied, the arrays
                // because they are larger than two eightbytes -- so they
                // travel as ordinary memory-class aggregates.  Clang compiles
                // all five to byval/sret against the same declarations.
                bool expected_rejected = !f80_sysv && wide_long_double && function_index < 6;
                BUSTER_TEST(arguments, function->state == (expected_rejected ? IR_FUNCTION_REJECTED : IR_FUNCTION_LOWERED));
            }
            if (wide_long_double)
            {
                IrFunction* struct_function = c_test_find_ir_function(module, S8("struct_round_trip"));
                IrFunction* large_function = c_test_find_ir_function(module, S8("large_round_trip"));
                IrType* struct_function_type = struct_function ? ir_type_from_id(&lowered.program->types, struct_function->canonical_type) : 0;
                IrType* large_function_type = large_function ? ir_type_from_id(&lowered.program->types, large_function->canonical_type) : 0;
                IrTypeId struct_type_id = struct_function_type && struct_function_type->parameter_count
                                               ? struct_function_type->parameter_types[0]
                                               : IR_TYPE_ID_INVALID;
                IrAbiValue struct_abi = ir_type_abi_value(lowered.program, struct_type_id,
                                                           ir_abi_convention_for_target(target), IR_ABI_USE_ARGUMENT);
                IrTypeId large_type_id = large_function_type ? large_function_type->return_type : IR_TYPE_ID_INVALID;
                IrAbiValue large_abi = ir_type_abi_value(lowered.program, large_type_id,
                                                          ir_abi_convention_for_target(target), IR_ABI_USE_RESULT);
                BUSTER_TEST(arguments, struct_abi.part_count != 0);
                BUSTER_TEST(arguments, large_abi.part_count != 0 && large_abi.indirect);
                if (f80_sysv)
                {
                    IrFunction* union_function = c_test_find_ir_function(module, S8("union_round_trip"));
                    IrType* union_function_type = union_function ? ir_type_from_id(&lowered.program->types, union_function->canonical_type) : 0;
                    IrTypeId union_type_id = union_function_type ? union_function_type->return_type : IR_TYPE_ID_INVALID;
                    IrAbiValue union_argument = ir_type_abi_value(lowered.program, union_type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64,
                                                                   IR_ABI_USE_ARGUMENT);
                    IrAbiValue union_result = ir_type_abi_value(lowered.program, union_type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64,
                                                                 IR_ABI_USE_RESULT);
                    // `union { long double; int; }` merges to INTEGER in the
                    // first eightbyte and leaves X87_UP alone in the second,
                    // which the post-merge cleanup sends to memory whole.
                    BUSTER_TEST(arguments, union_argument.memory && !union_argument.indirect);
                    BUSTER_TEST(arguments, union_result.indirect && !union_result.memory);
                    BUSTER_TEST(arguments, !ir_abi_value_has_x87_part(lowered.program, union_type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64,
                                                                       IR_ABI_USE_ARGUMENT));
                    BUSTER_TEST(arguments, !ir_abi_value_has_x87_part(lowered.program, union_type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64,
                                                                       IR_ABI_USE_RESULT));
                }
            }
            BUSTER_TEST(arguments, module->rejected_function_count == (f80_sysv ? 0 : wide_long_double ? 6 : 0));
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_signature_calls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("typedef struct WideStruct { long double value; } WideStruct;"
                        " typedef union WideUnion { long double value; int integer; } WideUnion;"
                        " typedef struct NestedStruct { WideStruct value; } NestedStruct;"
                        " extern long double extern_scalar_target(void);"
                        " extern WideStruct extern_struct_target(WideStruct value);"
                        " extern WideUnion extern_union_target(WideUnion value);"
                        " extern NestedStruct extern_nested_target(NestedStruct value);"
                        " extern void variadic_scalar_target(int fixed, ...);"
                        " extern void variadic_aggregate_target(int fixed, ...);"
                        " extern void variadic_f64_target(int fixed, ...);"
                        " extern void pointer_target(long double *value);"
                        " void function_pointer_sink(long double (*value)(void)) { (void)value; }"
                        " void call_scalar(void) { extern_scalar_target(); }"
                        " void call_struct(void) { WideStruct value = { 0 }; extern_struct_target(value); }"
                        " void call_union(void) { WideUnion value = { 0 }; extern_union_target(value); }"
                        " void call_nested(void) { NestedStruct value = { 0 }; extern_nested_target(value); }"
                        " void call_function_pointer(void) { long double (*value)(void) = extern_scalar_target; value(); }"
                        " void address_only(void) { function_pointer_sink(extern_scalar_target); }"
                        " void pointer_transport(long double *value) { pointer_target(value); }"
                        " void call_variadic_scalar(void) { long double value = 0.0L; variadic_scalar_target(0, value); }"
                        " void call_variadic_aggregate(void) { WideStruct value = { 0 }; variadic_aggregate_target(0, value); }"
                        " void call_variadic_f64(void) { double value = 0; variadic_f64_target(0, value); }"
                        " int main(void) { return 0; }");
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-pc-windows-msvc"),
        S8("x86_64-apple-macos"),
        S8("aarch64-unknown-linux-gnu"),
        S8("aarch64-apple-macos"),
        S8("aarch64-pc-windows-msvc"),
    };
    String8 rejected_names[] = {
        S8("call_scalar"),
        S8("call_struct"),
        S8("call_union"),
        S8("call_nested"),
        S8("call_function_pointer"),
        S8("call_variadic_scalar"),
        S8("call_variadic_aggregate"),
    };
    String8 preserved_names[] = {
        S8("address_only"),
        S8("pointer_transport"),
        S8("call_variadic_f64"),
        S8("function_pointer_sink"),
        S8("main"),
    };
    String8 extern_names[] = {
        S8("extern_scalar_target"),
        S8("extern_struct_target"),
        S8("extern_union_target"),
        S8("extern_nested_target"),
        S8("variadic_scalar_target"),
        S8("variadic_aggregate_target"),
        S8("variadic_f64_target"),
        S8("pointer_target"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        Target target = parsed_target.target;
        bool wide_long_double = target_data_layout(target).long_double_type.bit_width > 64;
        bool f80_sysv = target.cpu_arch == CPU_ARCH_X86_64 &&
                        (target.os == OPERATING_SYSTEM_LINUX || target.os == OPERATING_SYSTEM_MACOS ||
                         target.os == OPERATING_SYSTEM_IOS) &&
                        wide_long_double;
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lowered = c_test_lower_source(temporary.arena, source, target_triples[target_index], target, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == (f80_sysv ? 0 : wide_long_double ? BUSTER_ARRAY_LENGTH(rejected_names) : 0));
        for (u32 diagnostic_index = 0; diagnostic_index < lowered.diagnostic_count; diagnostic_index += 1)
        {
            CDiagnostic diagnostic = lowered.diagnostics[diagnostic_index];
            BUSTER_TEST(arguments, diagnostic.kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
            BUSTER_TEST(arguments, diagnostic.message.length != 0);
        }
        BUSTER_TEST(arguments, lowered.program != 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            for (u32 function_index = 0; function_index < BUSTER_ARRAY_LENGTH(rejected_names); function_index += 1)
            {
                IrFunction* function = c_test_find_ir_function(module, rejected_names[function_index]);
                BUSTER_TEST(arguments, function != 0);
                if (!function)
                {
                    continue;
                }
                // System V passes a variadic wide float exactly as it passes a
                // fixed one, in a sixteen-byte overflow slot, and the
                // single-member wrapper classifies identically, so both
                // variadic calls lower.  The union's classification carries no
                // x87 class at all, so it lowers as a memory-class aggregate.
                bool expected_rejected = !f80_sysv && wide_long_double;
                BUSTER_TEST(arguments, function->state == (expected_rejected ? IR_FUNCTION_REJECTED : IR_FUNCTION_LOWERED));
                if (expected_rejected)
                {
                    BUSTER_TEST(arguments, c_test_ir_call_count(function) == 0);
                }
            }
            for (u32 function_index = 0; function_index < BUSTER_ARRAY_LENGTH(preserved_names); function_index += 1)
            {
                IrFunction* function = c_test_find_ir_function(module, preserved_names[function_index]);
                BUSTER_TEST(arguments, function != 0);
                BUSTER_TEST(arguments, function && function->state == IR_FUNCTION_LOWERED);
            }
            for (u32 function_index = 0; function_index < BUSTER_ARRAY_LENGTH(extern_names); function_index += 1)
            {
                IrFunction* function = c_test_find_ir_function(module, extern_names[function_index]);
                BUSTER_TEST(arguments, function != 0);
                BUSTER_TEST(arguments, function && function->state == IR_FUNCTION_DECLARATION);
            }
            BUSTER_TEST(arguments, module->rejected_function_count == (f80_sysv ? 0 : wide_long_double ? BUSTER_ARRAY_LENGTH(rejected_names) : 0));
            BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_cleanup_signature_calls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("extern long double cleanup_wide(long double *value);"
                        " void cleanup_owner(void) {"
                        " long double value __attribute__((cleanup(cleanup_wide))) = 0.0L;"
                        " return;"
                        " }"
                        " int main(void) { return 0; }");
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-pc-windows-msvc"),
        S8("x86_64-apple-macos"),
        S8("aarch64-unknown-linux-gnu"),
        S8("aarch64-apple-macos"),
        S8("aarch64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        Target target = parsed_target.target;
        bool wide_long_double = target_data_layout(target).long_double_type.bit_width > 64;
        bool f80_sysv = target.cpu_arch == CPU_ARCH_X86_64 &&
                        (target.os == OPERATING_SYSTEM_LINUX || target.os == OPERATING_SYSTEM_MACOS ||
                         target.os == OPERATING_SYSTEM_IOS) &&
                        wide_long_double;
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        preprocess = c_preprocess(temporary.arena, source,
                                  (CPreprocessOptions){
                                      .dialect = C_PREPROCESS_DIALECT_GNU23,
                                      .target = target,
                                      .data_layout = target_data_layout(target),
                                  });
        parse = c_parse(temporary.arena, preprocess);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, target_triples[target_index], preprocess, parse, target);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == ((!f80_sysv && wide_long_double) ? 1 : 0));
        if (!f80_sysv && wide_long_double && lowered.diagnostic_count == 1)
        {
            BUSTER_TEST(arguments, lowered.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
            BUSTER_TEST(arguments, lowered.diagnostics[0].message.length != 0);
        }
        BUSTER_TEST(arguments, lowered.program != 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            IrFunction* callback = c_test_find_ir_function(module, S8("cleanup_wide"));
            IrFunction* owner = c_test_find_ir_function(module, S8("cleanup_owner"));
            BUSTER_TEST(arguments, callback != 0 && callback->state == IR_FUNCTION_DECLARATION);
            BUSTER_TEST(arguments, owner != 0);
            if (owner)
            {
                u32 call_count = 0;
                u32 branch_count = 0;
                for (u32 instruction_index = 0; instruction_index < owner->instruction_count; instruction_index += 1)
                {
                    IrOpcode opcode = owner->instructions[instruction_index].opcode;
                    call_count += opcode == IR_OPCODE_CALL;
                    branch_count += opcode == IR_OPCODE_BRANCH || opcode == IR_OPCODE_BRANCH_IF;
                }
                BUSTER_TEST(arguments, owner->state == ((!f80_sysv && wide_long_double) ? IR_FUNCTION_REJECTED : IR_FUNCTION_LOWERED));
                BUSTER_TEST(arguments, call_count == ((!f80_sysv && wide_long_double) ? 0 : 1));
                BUSTER_TEST(arguments, (!f80_sysv && wide_long_double) ? branch_count == 0 : branch_count != 0);
            }
            BUSTER_TEST(arguments, module->rejected_function_count == ((!f80_sysv && wide_long_double) ? 1 : 0));
            BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_local_transport(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("long double identity(long double value) { return value; }\n"
                        "long double positive_zero(void) { volatile long double value = +0.0L; return value; }\n"
                        "long double negative_zero(void) { volatile long double value = -0.0L; return value; }\n"
                        "long double assign(long double value) { volatile long double local = 0.0L; local = value; return local; }\n"
                        "long double call_identity(void) { return identity(-0.0L); }\n");
    String8 target_triple = S8("x86_64-unknown-linux-gnu");
    TargetParseResult parsed_target = target_parse_triple(target_triple);
    BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
    if (parsed_target.error == TARGET_PARSE_ERROR_NONE)
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lowered = c_test_lower_source(temporary.arena, source, target_triple, parsed_target.target, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.program != 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            BUSTER_TEST(arguments, module->function_count == 5);
            BUSTER_TEST(arguments, module->rejected_function_count == 0);
            u32 total_local_count = 0;
            u32 total_load_count = 0;
            u32 total_store_count = 0;
            u32 total_call_count = 0;
            u32 positive_zero_count = 0;
            u32 negative_zero_count = 0;
            u32 located_constant_count = 0;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    total_local_count += instruction->opcode == IR_OPCODE_LOCAL;
                    total_load_count += instruction->opcode == IR_OPCODE_LOAD;
                    total_store_count += instruction->opcode == IR_OPCODE_STORE;
                    total_call_count += instruction->opcode == IR_OPCODE_CALL;
                    if (instruction->opcode == IR_OPCODE_CONSTANT_FLOAT && instruction->immediate_count == 2 && instruction->immediates)
                    {
                        positive_zero_count += instruction->immediates[0] == 0 && instruction->immediates[1] == 0;
                        negative_zero_count += instruction->immediates[0] == 0 && instruction->immediates[1] == UINT64_C(0x8000);
                        IrSourceRange constant_source = ir_instruction_canonical_source(function, ir_instruction_self_id(function, instruction));
                        located_constant_count += constant_source.length != 0;
                    }
                }
            }
            BUSTER_TEST(arguments, total_local_count == 5);
            BUSTER_TEST(arguments, total_load_count == 5);
            BUSTER_TEST(arguments, total_store_count == 6);
            BUSTER_TEST(arguments, total_call_count == 1);
            BUSTER_TEST(arguments, positive_zero_count == 4);
            BUSTER_TEST(arguments, negative_zero_count == 2);
            BUSTER_TEST(arguments, located_constant_count == positive_zero_count + negative_zero_count);
            BUSTER_TEST(arguments, c_test_ir_direct_call_count(lowered.program, c_test_find_ir_function(module, S8("call_identity")), S8("identity")) == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(temporary);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrGlobal* c_test_find_ir_global(IrModule* module, IrProgram* program, String8 name)
{
    if (module && program)
    {
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = ir_symbol_from_id(&program->symbols, global->symbol);
            if (symbol && string_equal(symbol->name, name))
            {
                return global;
            }
        }
    }

    return 0;
}

BUSTER_GLOBAL_LOCAL bool c_test_ext80_global_bytes(IrProgram* program, IrGlobal* global, u8 const* expected, u32 length)
{
    IrType* type = program && global ? ir_type_from_id(&program->types, global->type) : 0;
    bool padding_zero = global && global->bytes.pointer && global->bytes.length >= 16;
    if (padding_zero)
    {
        for (u32 index = 10; index < 16; index += 1)
        {
            if (global->bytes.pointer[index])
            {
                padding_zero = false;
                break;
            }
        }
    }
    bool bytes_match = global && global->bytes.pointer && global->bytes.length == length;
    if (bytes_match)
    {
        for (u32 index = 0; index < length; index += 1)
        {
            if (global->bytes.pointer[index] != expected[index])
            {
                bytes_match = false;
                break;
            }
        }
    }
    return type && type->kind == IR_TYPE_FLOAT && type->bit_width == 80 && type->layout.size == 16 && global &&
           global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES && bytes_match && padding_zero;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_global_initializers(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("long double l_hex = 0x1.0000000000001p+0L;"
                        " long double l_decimal = 0.1L;"
                        " long double f64_widen = 0.1;"
                        " long double f32_widen = 0.1f;"
                        " long double hex_f64_round = 0x50a41ed3aeedba5.p36;"
                        " long double hex_f32_round = 0x50a41ed3aeedba5.p36f;"
                        " long double decimal_halfway = 1.00000000000000011102230246251565404236316680908203125;"
                        " long double decimal_halfway_f32 = 1.000000059604644775390625f;"
                        " long double decimal_double_round = 0.7e188;"
                        " long double parenthesized = ((1.0L));"
                        " long double parenthesized_sign = (((-1.0L)));"
                        " long double int_wide = 0x123456789abcdef0ULL;"
                        " long double i64_wide = 1i64;"
                        " long double ui64_wide = 2ui64;"
                        " long double suffix_ul = 3uL;"
                        " long double suffix_lu = 4Lu;"
                        " long double suffix_ll = 5LL;"
                        " long double suffix_ull = 6ull;"
                        " long double suffix_llu = 7LLU;"
                        " long double unsigned_neg = -0xffffffffffffffffULL;"
                        " long double mutable_zero = 0.0L;"
                        " long double negative_zero = -0.0L;"
                        " const long double const_zero = 0.0L;"
                        " int main(void) { return 0; }");
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-apple-macos"),
        S8("x86_64-apple-ios"),
        S8("x86_64-pc-windows-msvc"),
    };
    u8 expected_l_hex[] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_l_decimal[] = {0xcd, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_f64[] = {0x00, 0xd0, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_f32[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xcd, 0xcc, 0xcc, 0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_hex_f64_round[] = {0x00, 0x78, 0xdb, 0x5d, 0xa7, 0x3d, 0x48, 0xa1, 0x5d, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_hex_f32_round[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x48, 0xa1, 0x5d, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_decimal_halfway[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_decimal_halfway_f32[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_decimal_double_round[] = {0x00, 0xd0, 0xd6, 0x21, 0x87, 0x1f, 0xb4, 0x80, 0x6f, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_parenthesized[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_parenthesized_sign[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_integer[] = {0x80, 0xf7, 0xe6, 0xd5, 0xc4, 0xb3, 0xa2, 0x91, 0x3b, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_i64[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_ui64[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_suffix_ul[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_suffix_lu[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_suffix_ll[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_suffix_ull[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_suffix_llu[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    u8 expected_unsigned_neg[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        Target target = parsed_target.target;
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lowered = c_test_lower_source(temporary.arena, source, target_triples[target_index], target, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.program != 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            IrGlobal* l_hex = c_test_find_ir_global(module, lowered.program, S8("l_hex"));
            IrGlobal* l_decimal = c_test_find_ir_global(module, lowered.program, S8("l_decimal"));
            IrGlobal* f64_widen = c_test_find_ir_global(module, lowered.program, S8("f64_widen"));
            IrGlobal* f32_widen = c_test_find_ir_global(module, lowered.program, S8("f32_widen"));
            IrGlobal* hex_f64_round = c_test_find_ir_global(module, lowered.program, S8("hex_f64_round"));
            IrGlobal* hex_f32_round = c_test_find_ir_global(module, lowered.program, S8("hex_f32_round"));
            IrGlobal* decimal_halfway = c_test_find_ir_global(module, lowered.program, S8("decimal_halfway"));
            IrGlobal* decimal_halfway_f32 = c_test_find_ir_global(module, lowered.program, S8("decimal_halfway_f32"));
            IrGlobal* decimal_double_round = c_test_find_ir_global(module, lowered.program, S8("decimal_double_round"));
            IrGlobal* parenthesized = c_test_find_ir_global(module, lowered.program, S8("parenthesized"));
            IrGlobal* parenthesized_sign = c_test_find_ir_global(module, lowered.program, S8("parenthesized_sign"));
            IrGlobal* int_wide = c_test_find_ir_global(module, lowered.program, S8("int_wide"));
            IrGlobal* i64_wide = c_test_find_ir_global(module, lowered.program, S8("i64_wide"));
            IrGlobal* ui64_wide = c_test_find_ir_global(module, lowered.program, S8("ui64_wide"));
            IrGlobal* suffix_ul = c_test_find_ir_global(module, lowered.program, S8("suffix_ul"));
            IrGlobal* suffix_lu = c_test_find_ir_global(module, lowered.program, S8("suffix_lu"));
            IrGlobal* suffix_ll = c_test_find_ir_global(module, lowered.program, S8("suffix_ll"));
            IrGlobal* suffix_ull = c_test_find_ir_global(module, lowered.program, S8("suffix_ull"));
            IrGlobal* suffix_llu = c_test_find_ir_global(module, lowered.program, S8("suffix_llu"));
            IrGlobal* unsigned_neg = c_test_find_ir_global(module, lowered.program, S8("unsigned_neg"));
            IrGlobal* mutable_zero = c_test_find_ir_global(module, lowered.program, S8("mutable_zero"));
            IrGlobal* negative_zero = c_test_find_ir_global(module, lowered.program, S8("negative_zero"));
            IrGlobal* const_zero = c_test_find_ir_global(module, lowered.program, S8("const_zero"));
            bool ext80 = target.cpu_arch == CPU_ARCH_X86_64 &&
                         (target.os == OPERATING_SYSTEM_LINUX || target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS);
            if (ext80)
            {
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, l_hex, expected_l_hex, sizeof(expected_l_hex)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, l_decimal, expected_l_decimal, sizeof(expected_l_decimal)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, f64_widen, expected_f64, sizeof(expected_f64)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, f32_widen, expected_f32, sizeof(expected_f32)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, hex_f64_round, expected_hex_f64_round, sizeof(expected_hex_f64_round)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, hex_f32_round, expected_hex_f32_round, sizeof(expected_hex_f32_round)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, decimal_halfway, expected_decimal_halfway, sizeof(expected_decimal_halfway)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, decimal_halfway_f32, expected_decimal_halfway_f32, sizeof(expected_decimal_halfway_f32)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, decimal_double_round, expected_decimal_double_round, sizeof(expected_decimal_double_round)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, parenthesized, expected_parenthesized, sizeof(expected_parenthesized)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, parenthesized_sign, expected_parenthesized_sign, sizeof(expected_parenthesized_sign)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, int_wide, expected_integer, sizeof(expected_integer)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, i64_wide, expected_i64, sizeof(expected_i64)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, ui64_wide, expected_ui64, sizeof(expected_ui64)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, suffix_ul, expected_suffix_ul, sizeof(expected_suffix_ul)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, suffix_lu, expected_suffix_lu, sizeof(expected_suffix_lu)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, suffix_ll, expected_suffix_ll, sizeof(expected_suffix_ll)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, suffix_ull, expected_suffix_ull, sizeof(expected_suffix_ull)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, suffix_llu, expected_suffix_llu, sizeof(expected_suffix_llu)));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, unsigned_neg, expected_unsigned_neg, sizeof(expected_unsigned_neg)));
                BUSTER_TEST(arguments, mutable_zero && mutable_zero->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, negative_zero, (u8[]){0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0}, 16));
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, const_zero, (u8[16]){0}, 16));
            }
            else
            {
                BUSTER_TEST(arguments, l_hex && l_hex->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
                BUSTER_TEST(arguments, l_decimal && l_decimal->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
                BUSTER_TEST(arguments, f64_widen && f64_widen->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
                BUSTER_TEST(arguments, f32_widen && f32_widen->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
                BUSTER_TEST(arguments, int_wide && int_wide->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
                BUSTER_TEST(arguments, mutable_zero && mutable_zero->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
                BUSTER_TEST(arguments, negative_zero && negative_zero->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
            }
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_global_rejections(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-apple-macos"),
        S8("x86_64-apple-ios"),
    };
    String8 sources[] = {
        S8("long double cast_value = (long double)1; int main(void) { return 0; }"),
        S8("long double conditional_value = 1.0L ? 2.0L : 3.0L; int main(void) { return 0; }"),
        S8("long double integer_arithmetic = 1 + 2; int main(void) { return 0; }"),
        S8("void atomic_local(void) { _Atomic(long double) value = 0.0L; (void)value; } int main(void) { return 0; }"),
        S8("long double atomic_load(_Atomic(long double) *value) { return __c11_atomic_load(value, __ATOMIC_RELAXED); } int main(void) { return 0; }"),
        S8("void atomic_store(_Atomic(long double) *value) { __c11_atomic_store(value, 0.0L, __ATOMIC_RELAXED); } int main(void) { return 0; }"),
        S8("long double atomic_exchange(_Atomic(long double) *value) { return __c11_atomic_exchange(value, 0.0L, __ATOMIC_RELAXED); } int main(void) { return 0; }"),
        S8("int atomic_compare(_Atomic(long double) *value, long double *expected) { return __c11_atomic_compare_exchange_strong(value, expected, 0.0L, __ATOMIC_RELAXED, __ATOMIC_RELAXED); } int main(void) { return 0; }"),
        S8("void fixed_f80_variadic(long double value, ...) { (void)value; } int main(void) { return 0; }"),
        // `va_arg` reads a wide value back in the two shapes the argument side
        // passes one in; the shapes past the two eightbytes its copy covers --
        // a `long double _Complex`, an aggregate with a tail behind the
        // payload -- are named here rather than at code generation.
        S8("typedef void *va_list; int take(int count, ...) { va_list arguments; long double _Complex value = __builtin_va_arg(arguments, long double _Complex); return value != 0; } int main(void) { return 0; }"),
        S8("typedef void *va_list; struct ldlarge { long double f; int tail; }; int take(int count, ...) { va_list arguments; struct ldlarge value = __builtin_va_arg(arguments, struct ldlarge); return value.tail; } int main(void) { return 0; }"),
        S8("long double malformed_exponent = 0x1pL; int main(void) { return 0; }"),
        S8("long double invalid_i_suffix = 123i; int main(void) { return 0; }"),
        S8("long double invalid_i65_suffix = 123i65; int main(void) { return 0; }"),
        S8("long double invalid_u64_suffix = 123u64; int main(void) { return 0; }"),
        S8("long double invalid_lUl_suffix = 123lUl; int main(void) { return 0; }"),
        S8("long double invalid_lL_suffix = 123lL; int main(void) { return 0; }"),
        S8("long double invalid_uLl_suffix = 123uLl; int main(void) { return 0; }"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult preprocess = {0};
            CParseResult parse = {0};
            CIRLowerResult lowered = c_test_lower_source(temporary.arena, sources[source_index], target_triples[target_index],
                                                         parsed_target.target, &preprocess, &parse);
            BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
            BUSTER_TEST(arguments, parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, lowered.diagnostic_count == 1);
            if (lowered.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, lowered.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
            }
            scratch_end(temporary);
        }
        // The x87 vocabulary answers for these, so they must lower rather
        // than diagnose: negation, truth conversion, the conversions to and
        // from a wide float, a wide float passed through a variadic call and
        // read back out of a `va_list` in either of the two shapes that
        // admits, and the static initializers the constant folder covers --
        // an arithmetic expression over literals, parenthesized or not, and
        // an aggregate of them.  They are checked here, beside the shapes
        // that still refuse, so the boundary between the two stays one list
        // to read.
        //
        // The last group is where the value stops being finite.  C gives an
        // overflowing spelling an infinity, an underflowing one a signed zero,
        // and a division by zero an infinity of the operands' combined sign,
        // and Clang folds all three; a narrower operation rounds in its own
        // format and is folded too.  Only integer arithmetic still refuses,
        // because it does not round at all.
        String8 accepted[] = {
            S8("long double negate_variable(long double value) { return -value; } int main(void) { return 0; }"),
            S8("int truth_variable(long double value) { return value ? 1 : 0; } int main(void) { return 0; }"),
            S8("long double cast_local(void) { return (long double)1; } int main(void) { return 0; }"),
            S8("long double cast_from_double(double value) { return (long double)value; } int main(void) { return 0; }"),
            S8("unsigned long long cast_to_unsigned(long double value) { return (unsigned long long)value; } int main(void) { return 0; }"),
            S8("long double add(long double left, long double right) { return left + right; } int main(void) { return 0; }"),
            S8("int compare(long double left, long double right) { return left < right; } int main(void) { return 0; }"),
            S8("void variadic_call(int count, ...); void call(void) { variadic_call(0, 1.0L); } int main(void) { return 0; }"),
            S8("typedef void *va_list; int take(int count, ...) { va_list arguments; long double value = __builtin_va_arg(arguments, long double); return value != 0; } int main(void) { return 0; }"),
            S8("typedef void *va_list; union ldshape { long double f; struct { unsigned long m; unsigned short se; } i; }; unsigned long take(int count, ...) { va_list arguments; union ldshape value = __builtin_va_arg(arguments, union ldshape); return value.i.m; } int main(void) { return 0; }"),
            S8("long double arithmetic = 1.0L + 2.0L; int main(void) { return 0; }"),
            S8("long double parenthesized_arithmetic = (1.0L + 2.0L); int main(void) { return 0; }"),
            S8("long double aggregate[1] = { 1.0L }; int main(void) { return 0; }"),
            S8("void local_static(void) { static long double local = 1.0L; } int main(void) { return 0; }"),
            S8("long double narrow_arithmetic = 1.0 + 2.0; int main(void) { return 0; }"),
            S8("long double divide_by_zero = 1.0L/0.0L; int main(void) { return 0; }"),
            S8("long double overflow = 0x1p+16384L; int main(void) { return 0; }"),
            S8("long double underflow = 0x1p-16446L; int main(void) { return 0; }"),
            S8("long double hex_f64_underflow = 0x0.ep-10000; int main(void) { return 0; }"),
            S8("long double hex_f32_underflow = 0x0.ep-1000f; int main(void) { return 0; }"),
            S8("long double decimal_underflow = 1e-10000; int main(void) { return 0; }"),
            S8("long double decimal_overflow = 1e5000L; int main(void) { return 0; }"),
            S8("long double float_overflow = 1e5000f; int main(void) { return 0; }"),
            S8("long double quiet_nan = 0.0f/0.0f; int main(void) { return 0; }"),
            S8("long double local_overflow(void) { return 1e5000L; } int main(void) { return 0; }"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(accepted); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult preprocess = {0};
            CParseResult parse = {0};
            CIRLowerResult lowered = c_test_lower_source(temporary.arena, accepted[source_index], target_triples[target_index],
                                                         parsed_target.target, &preprocess, &parse);
            BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
            BUSTER_TEST(arguments, parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
            BUSTER_TEST(arguments, lowered.program != 0);
            if (lowered.program)
            {
                BUSTER_TEST(arguments, lowered.program->modules->rejected_function_count == 0);
                BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, lowered.program->modules).error == IR_VALIDATION_NONE);
            }
            scratch_end(temporary);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_android_rejection(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 target_triple = S8("x86_64-unknown-android");
    TargetParseResult parsed_target = target_parse_triple(target_triple);
    BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
    if (parsed_target.error == TARGET_PARSE_ERROR_NONE)
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lowered = c_test_lower_source(temporary.arena, S8("long double android_value = 1.0L; int main(void) { return 0; }"),
                                                     target_triple, parsed_target.target, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 1);
        if (lowered.diagnostic_count == 1)
        {
            BUSTER_TEST(arguments, lowered.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_global_boundaries(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("long double min_normal = 0x1p-16382L;"
                        " long double min_subnormal = 0x1p-16445L;"
                        " long double max_finite = 0x1.fffffffffffffffep+16383L;"
                        " long double normal_tie_down = 0x1.0000000000000001p+0L;"
                        " long double normal_tie_up = 0x1.0000000000000003p+0L;"
                        " long double subnormal_tie_up = 0x3p-16446L;"
                        " long double subnormal_tie_down = 0x5p-16446L;"
                        " long double subnormal_to_normal = 0x0.ffffffffffffffffp-16382L;"
                        " long double decimal_boundary = 1e-4932L;"
                        " long double exponent_boundary = 0x1p+16383L;"
                        " int main(void) { return 0; }");
    String8 names[] = {
        S8("min_normal"),
        S8("min_subnormal"),
        S8("max_finite"),
        S8("normal_tie_down"),
        S8("normal_tie_up"),
        S8("subnormal_tie_up"),
        S8("subnormal_tie_down"),
        S8("subnormal_to_normal"),
        S8("decimal_boundary"),
        S8("exponent_boundary"),
    };
    // Oracle payloads emitted by GCC for x86_64 SysV long double objects.
    u8 expected[][16] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xf0, 0x57, 0x93, 0xf2, 0xc8, 0x47, 0x12, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xfe, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-apple-macos"),
        S8("x86_64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lowered = c_test_lower_source(temporary.arena, source, target_triples[target_index], parsed_target.target, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            for (u32 global_index = 0; global_index < BUSTER_ARRAY_LENGTH(names); global_index += 1)
            {
                IrGlobal* global = c_test_find_ir_global(module, lowered.program, names[global_index]);
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, global, expected[global_index], 16));
            }
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_global_braces(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("long double brace = { 1.0L };"
                        " long double brace_trailing = { 0x1p+0L, };"
                        " int main(void) { return 0; }");
    u8 expected[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-apple-macos"),
        S8("x86_64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lowered = c_test_lower_source(temporary.arena, source, target_triples[target_index], parsed_target.target, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            IrGlobal* brace = c_test_find_ir_global(module, lowered.program, S8("brace"));
            IrGlobal* brace_trailing = c_test_find_ir_global(module, lowered.program, S8("brace_trailing"));
            BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, brace, expected, sizeof(expected)));
            BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, brace_trailing, expected, sizeof(expected)));
        }
        scratch_end(temporary);
    }
    return result;
}

// An x87 aggregate global carries one flat byte payload rather than the scalar
// pair, so it is checked against the whole run instead of against the ten-byte
// value plus its padding.
BUSTER_GLOBAL_LOCAL bool c_test_ext80_aggregate_bytes(IrGlobal* global, u8 const* expected, u32 length)
{
    bool bytes_match = global && global->bytes.pointer && global->bytes.length == length &&
                       global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES;
    for (u32 index = 0; bytes_match && index < length; index += 1)
    {
        bytes_match = global->bytes.pointer[index] == expected[index];
    }
    return bytes_match;
}

// A static x87 initializer is a constant expression over literals, and an
// aggregate of them is one too.  These are the shapes musl's src/math needs:
// `1/LDBL_EPSILON` in floorl.c and the coefficient tables in atanl.c.
//
// The expected payloads are what Clang emits for the same declarations.  What
// they pin is that the fold rounds once per operation in the operation's own
// format rather than accumulating through a double: `1/LDBL_EPSILON` is
// exactly 2^63 only because LDBL_EPSILON rounds to 2^-63 first, and
// folded_from_float and folded_from_double differ from each other and from the
// long double nearest 0.1 for the same reason.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_wide_float_global_folding(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    String8 source = S8("long double folded_quotient = 1/1.0842021724855044340e-19L;"
                        " long double folded_chain = 3.0L*1.0L/7.0L;"
                        " long double folded_sum = 1.0L + 1.0L/3.0L;"
                        " long double folded_difference = 1.0L - 1.0L/3.0L;"
                        " long double folded_grouped = (2.0L*(3.0L + 4.0L))/5.0L;"
                        " long double folded_negation = -(1.0L/3.0L);"
                        " long double folded_from_float = 0.1f*1.0L;"
                        " long double folded_from_double = 0.1*1.0L;"
                        " long double folded_from_unsigned = -1U*1.0L;"
                        " long double folded_subnormal = 0x1p-16445L*1.0L;"
                        " long double table[3] = {1.0L/3.0L, -2.5L, 0x1p-16445L};"
                        " struct Pair { long double head; long double tail; };"
                        " struct Pair pair = {1.0L/7.0L, -1.0L/9.0L};"
                        " int main(void) { return 0; }");
    String8 names[] = {
        S8("folded_quotient"),
        S8("folded_chain"),
        S8("folded_sum"),
        S8("folded_difference"),
        S8("folded_grouped"),
        S8("folded_negation"),
        S8("folded_from_float"),
        S8("folded_from_double"),
        S8("folded_from_unsigned"),
        S8("folded_subnormal"),
    };
    u8 expected[][16] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3e, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x6e, 0xdb, 0xb6, 0x6d, 0xdb, 0xb6, 0x6d, 0xdb, 0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfe, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0xb3, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfd, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0xcd, 0xcc, 0xcc, 0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0xd0, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x1e, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    u8 expected_table[] = {
        0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xfd, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    u8 expected_pair[] = {
        0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0x49, 0x92, 0xfc, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x8e, 0xe3, 0x38, 0x8e, 0xe3, 0x38, 0x8e, 0xe3, 0xfb, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    String8 target_triples[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("x86_64-apple-macos"),
        S8("x86_64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(target_triples); target_index += 1)
    {
        TargetParseResult parsed_target = target_parse_triple(target_triples[target_index]);
        BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
        if (parsed_target.error != TARGET_PARSE_ERROR_NONE)
        {
            continue;
        }
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult preprocess = {0};
        CParseResult parse = {0};
        CIRLowerResult lowered = c_test_lower_source(temporary.arena, source, target_triples[target_index], parsed_target.target, &preprocess, &parse);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            for (u32 global_index = 0; global_index < BUSTER_ARRAY_LENGTH(names); global_index += 1)
            {
                IrGlobal* global = c_test_find_ir_global(module, lowered.program, names[global_index]);
                BUSTER_TEST(arguments, c_test_ext80_global_bytes(lowered.program, global, expected[global_index], 16));
            }
            IrGlobal* table = c_test_find_ir_global(module, lowered.program, S8("table"));
            IrGlobal* pair = c_test_find_ir_global(module, lowered.program, S8("pair"));
            BUSTER_TEST(arguments, c_test_ext80_aggregate_bytes(table, expected_table, sizeof(expected_table)));
            BUSTER_TEST(arguments, c_test_ext80_aggregate_bytes(pair, expected_pair, sizeof(expected_pair)));
        }
        scratch_end(temporary);
    }
    return result;
}

// The IR constant resolver keeps a compatibility path for identifier tokens
// that were not bound by parsing (enumerator declarations are one such path).
// Compare the indexed probe with the historical ascending entity-table scan
// while forcing the probe through local names, including a nested shadow, and
// through a hand-built collision bucket with two different spellings.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_packed_and_aligned_layout(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    // The layout the IR carries, member by member. The runtime fixtures under
    // tests/ check that a program built from these numbers behaves; this
    // checks the numbers themselves, so a regression names the field whose
    // offset moved instead of an exit status.
    String8 source = S8("struct __attribute__((packed)) leading { char byte; int value; };\n"
                        "struct trailing { char byte; int value; } __attribute__((packed));\n"
                        "struct member_packed { char byte; int value __attribute__((packed)); };\n"
                        "struct __attribute__((packed, aligned(8))) packed_then_aligned { char byte; int value; };\n"
                        "struct plain { char byte; int value; };\n"
                        "union __attribute__((packed)) packed_union { char byte; int value; };\n"
                        "union __attribute__((packed)) packed_bit_union { char lead; int value : 5; };\n"
                        "union __attribute__((packed)) packed_bit_union_wide { char lead; unsigned int value : 12; };\n"
                        "union unpacked_bit_union { char lead; int value : 5; };\n"
                        "struct __attribute__((packed)) packed_bits { unsigned char low : 3; unsigned char high : 5; char tail; };\n"
                        "struct __attribute__((packed)) offset_bits { char lead; int value : 24; };\n"
                        "struct __attribute__((packed)) zero_width_bits { int before : 3; int : 0; int after : 3; };\n"
                        "struct __attribute__((packed)) narrow_unit { char lead; int value : 5; char tail; };\n"
                        "struct narrow_unit_member { char lead; __attribute__((packed)) int value : 5; char tail; };\n"
                        "struct narrow_unit_declarator { char lead; int value : 5 __attribute__((packed)); char tail; };\n"
                        "#pragma pack(push, 1)\n"
                        "struct pragma_packed { char byte; int value; };\n"
                        "#pragma pack(pop)\n"
                        "struct leading leading_object;\n"
                        "struct trailing trailing_object;\n"
                        "struct member_packed member_packed_object;\n"
                        "struct packed_then_aligned packed_then_aligned_object;\n"
                        "struct plain plain_object;\n"
                        "union packed_union packed_union_object;\n"
                        "union packed_bit_union packed_bit_union_object;\n"
                        "union packed_bit_union_wide packed_bit_union_wide_object;\n"
                        "union unpacked_bit_union unpacked_bit_union_object;\n"
                        "struct packed_bits packed_bits_object;\n"
                        "struct offset_bits offset_bits_object;\n"
                        "struct zero_width_bits zero_width_bits_object;\n"
                        "struct narrow_unit narrow_unit_object;\n"
                        "struct narrow_unit_member narrow_unit_member_object;\n"
                        "struct narrow_unit_declarator narrow_unit_declarator_object;\n"
                        "struct pragma_packed pragma_packed_object;\n"
                        "char trailing_aligned[3] __attribute__((aligned(64)));\n"
                        "__attribute__((aligned(128))) char specifier_aligned[3];\n"
                        "char plain_object_array[3];\n"
                        // The folded sizes have to agree with the IR layout
                        // above, or a sizeof contradicts the object it sizes.
                        "_Static_assert(sizeof(struct leading) == 5, \"leading\");\n"
                        "_Static_assert(_Alignof(struct leading) == 1, \"leading alignment\");\n"
                        "_Static_assert(sizeof(struct trailing) == 5, \"trailing\");\n"
                        "_Static_assert(sizeof(struct member_packed) == 5, \"member packed\");\n"
                        "_Static_assert(sizeof(struct packed_then_aligned) == 8, \"packed then aligned\");\n"
                        "_Static_assert(_Alignof(struct packed_then_aligned) == 8, \"packed then aligned alignment\");\n"
                        "_Static_assert(sizeof(struct plain) == 8, \"plain\");\n"
                        "_Static_assert(sizeof(union packed_union) == 4, \"packed union\");\n"
                        "_Static_assert(sizeof(union packed_bit_union) == 1, \"packed bit union\");\n"
                        "_Static_assert(_Alignof(union packed_bit_union) == 1, \"packed bit union alignment\");\n"
                        "_Static_assert(sizeof(union packed_bit_union_wide) == 2, \"packed bit union wide\");\n"
                        "_Static_assert(sizeof(union unpacked_bit_union) == 4, \"unpacked bit union\");\n"
                        "_Static_assert(_Alignof(union unpacked_bit_union) == 4, \"unpacked bit union alignment\");\n"
                        "_Static_assert(sizeof(struct packed_bits) == 2, \"packed bits\");\n"
                        "_Static_assert(sizeof(struct offset_bits) == 4, \"offset bits\");\n"
                        "_Static_assert(sizeof(struct zero_width_bits) == 5, \"zero width bits\");\n"
                        "_Static_assert(sizeof(struct narrow_unit) == 3, \"narrow unit\");\n"
                        "_Static_assert(_Alignof(struct narrow_unit) == 1, \"narrow unit alignment\");\n"
                        "_Static_assert(sizeof(struct narrow_unit_member) == 3, \"narrow unit member\");\n"
                        "_Static_assert(sizeof(struct narrow_unit_declarator) == 3, \"narrow unit declarator\");\n"
                        "_Static_assert(sizeof(struct pragma_packed) == 5, \"pragma packed\");\n");
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult preprocess = {0};
    CParseResult parse = {0};
    CIRLowerResult lowered = c_test_lower_source(temporary.arena, source, S8("packed-layout.c"), target_native, &preprocess, &parse);
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
    BUSTER_TEST(arguments, lowered.program != 0);
    if (lowered.program)
    {
        IrModule* module = lowered.program->modules;
        struct
        {
            String8 object;
            u64 size;
            u32 alignment;
            u64 second_field_offset;
        } aggregates[] = {
            {S8("leading_object"), 5, 1, 1},
            {S8("trailing_object"), 5, 1, 1},
            {S8("member_packed_object"), 5, 1, 1},
            {S8("packed_then_aligned_object"), 8, 8, 1},
            {S8("plain_object"), 8, 4, 4},
            {S8("pragma_packed_object"), 5, 1, 1},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(aggregates); index += 1)
        {
            IrGlobal* global = c_test_find_ir_global(module, lowered.program, aggregates[index].object);
            IrType* type = global ? ir_type_from_id(&lowered.program->types, global->type) : 0;
            BUSTER_TEST(arguments, type != 0 && type->field_count == 2);
            if (type && type->field_count == 2)
            {
                BUSTER_TEST(arguments, type->layout.size == aggregates[index].size);
                BUSTER_TEST(arguments, type->layout.alignment == aggregates[index].alignment);
                BUSTER_TEST(arguments, type->fields[0].offset == 0);
                BUSTER_TEST(arguments, type->fields[1].offset == aggregates[index].second_field_offset);
            }
        }
        IrGlobal* packed_union_object = c_test_find_ir_global(module, lowered.program, S8("packed_union_object"));
        IrType* packed_union_type = packed_union_object ? ir_type_from_id(&lowered.program->types, packed_union_object->type) : 0;
        BUSTER_TEST(arguments, packed_union_type != 0);
        if (packed_union_type)
        {
            BUSTER_TEST(arguments, packed_union_type->layout.size == 4 && packed_union_type->layout.alignment == 1);
        }
        // A union member starts at bit zero whether or not it is a bit-field,
        // so a packed union sizes to the bits its widest member occupies
        // rather than to that member's declared type (#706), and the unit the
        // field is read through narrows to what the union has room for. The
        // unpacked spelling is the control: the rounding to the alignment its
        // declared type asks for gives the four bytes and the declared unit
        // back, which is what one uniform arm has to answer.
        struct
        {
            String8 object;
            u64 size;
            u32 alignment;
            u8 access_size;
        } bit_unions[] = {
            {S8("packed_bit_union_object"), 1, 1, 1},
            {S8("packed_bit_union_wide_object"), 2, 1, 2},
            {S8("unpacked_bit_union_object"), 4, 4, 0},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(bit_unions); index += 1)
        {
            IrGlobal* bit_union_object = c_test_find_ir_global(module, lowered.program, bit_unions[index].object);
            IrType* bit_union_type = bit_union_object ? ir_type_from_id(&lowered.program->types, bit_union_object->type) : 0;
            BUSTER_TEST(arguments, bit_union_type != 0 && bit_union_type->field_count == 2);
            if (bit_union_type && bit_union_type->field_count == 2)
            {
                BUSTER_TEST(arguments, bit_union_type->layout.size == bit_unions[index].size);
                BUSTER_TEST(arguments, bit_union_type->layout.alignment == bit_unions[index].alignment);
                BUSTER_TEST(arguments, bit_union_type->fields[0].offset == 0);
                BUSTER_TEST(arguments, bit_union_type->fields[1].offset == 0 && bit_union_type->fields[1].bit_offset == 0);
                BUSTER_TEST(arguments, bit_union_type->fields[1].access_size == bit_unions[index].access_size);
            }
        }
        // The three packed bit-field shapes: contiguous bits, a field read
        // through a unit that starts before it, and a zero-width one that
        // still aligns to its declared type. The last one's storage unit is
        // slid back so it lies inside the five-byte aggregate.
        IrGlobal* packed_bits_object = c_test_find_ir_global(module, lowered.program, S8("packed_bits_object"));
        IrType* packed_bits_type = packed_bits_object ? ir_type_from_id(&lowered.program->types, packed_bits_object->type) : 0;
        BUSTER_TEST(arguments, packed_bits_type != 0 && packed_bits_type->field_count == 3);
        if (packed_bits_type && packed_bits_type->field_count == 3)
        {
            BUSTER_TEST(arguments, packed_bits_type->layout.size == 2 && packed_bits_type->layout.alignment == 1);
            BUSTER_TEST(arguments, packed_bits_type->fields[0].offset == 0 && packed_bits_type->fields[0].bit_offset == 0);
            BUSTER_TEST(arguments, packed_bits_type->fields[1].offset == 0 && packed_bits_type->fields[1].bit_offset == 3);
            BUSTER_TEST(arguments, packed_bits_type->fields[2].offset == 1);
        }
        IrGlobal* offset_bits_object = c_test_find_ir_global(module, lowered.program, S8("offset_bits_object"));
        IrType* offset_bits_type = offset_bits_object ? ir_type_from_id(&lowered.program->types, offset_bits_object->type) : 0;
        BUSTER_TEST(arguments, offset_bits_type != 0 && offset_bits_type->field_count == 2);
        if (offset_bits_type && offset_bits_type->field_count == 2)
        {
            BUSTER_TEST(arguments, offset_bits_type->layout.size == 4 && offset_bits_type->layout.alignment == 1);
            BUSTER_TEST(arguments, offset_bits_type->fields[1].offset == 0 && offset_bits_type->fields[1].bit_offset == 8);
        }
        IrGlobal* zero_width_bits_object = c_test_find_ir_global(module, lowered.program, S8("zero_width_bits_object"));
        IrType* zero_width_bits_type = zero_width_bits_object ? ir_type_from_id(&lowered.program->types, zero_width_bits_object->type) : 0;
        BUSTER_TEST(arguments, zero_width_bits_type != 0 && zero_width_bits_type->field_count == 3);
        if (zero_width_bits_type && zero_width_bits_type->field_count == 3)
        {
            BUSTER_TEST(arguments, zero_width_bits_type->layout.size == 5 && zero_width_bits_type->layout.alignment == 1);
            BUSTER_TEST(arguments, zero_width_bits_type->fields[0].offset == 0 && zero_width_bits_type->fields[0].bit_offset == 0);
            BUSTER_TEST(arguments, zero_width_bits_type->fields[2].offset == 1 && zero_width_bits_type->fields[2].bit_offset == 24);
        }
        // A field the aggregate leaves no room for a declared-type unit of is
        // read through a narrower one rather than refused: three bytes hold no
        // int anywhere, so `value` takes the byte at offset one, which is what
        // Clang and GCC do. All three spellings of the same request -- the
        // whole aggregate packed, the shared specifier, and the declarator's
        // own list after the width -- record the same unit.
        String8 narrow_objects[] = {S8("narrow_unit_object"), S8("narrow_unit_member_object"), S8("narrow_unit_declarator_object")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(narrow_objects); index += 1)
        {
            IrGlobal* narrow_object = c_test_find_ir_global(module, lowered.program, narrow_objects[index]);
            IrType* narrow_type = narrow_object ? ir_type_from_id(&lowered.program->types, narrow_object->type) : 0;
            BUSTER_TEST(arguments, narrow_type != 0 && narrow_type->field_count == 3);
            if (narrow_type && narrow_type->field_count == 3)
            {
                BUSTER_TEST(arguments, narrow_type->layout.size == 3 && narrow_type->layout.alignment == 1);
                BUSTER_TEST(arguments, narrow_type->fields[1].offset == 1 && narrow_type->fields[1].bit_offset == 0);
                BUSTER_TEST(arguments, narrow_type->fields[1].access_size == 1);
                BUSTER_TEST(arguments, ir_field_access_size(&lowered.program->types, narrow_type->fields + 1) == 1);
                BUSTER_TEST(arguments, narrow_type->fields[2].offset == 2);
            }
        }
        // Every other bit-field keeps the declared type's unit, so the
        // narrowing is confined to the layout that has no other answer.
        BUSTER_TEST(arguments, offset_bits_type != 0 && offset_bits_type->field_count == 2 && offset_bits_type->fields[1].access_size == 0);

        // The object-declarator attribute reaches the global's alignment from
        // either side of the name.
        IrGlobal* trailing_aligned = c_test_find_ir_global(module, lowered.program, S8("trailing_aligned"));
        IrGlobal* specifier_aligned = c_test_find_ir_global(module, lowered.program, S8("specifier_aligned"));
        IrGlobal* plain_object_array = c_test_find_ir_global(module, lowered.program, S8("plain_object_array"));
        BUSTER_TEST(arguments, trailing_aligned != 0 && trailing_aligned->alignment == 64);
        BUSTER_TEST(arguments, specifier_aligned != 0 && specifier_aligned->alignment == 128);
        BUSTER_TEST(arguments, plain_object_array != 0 && plain_object_array->alignment == 1);
    }
    scratch_end(temporary);

    // The attribute is recorded against the aggregate it decorates and no
    // other, whichever side of the body it is written on.
    TemporalArena record_temporary = scratch_begin(0, 0);
    CPreprocessResult record_preprocess = c_preprocess(record_temporary.arena,
                                                       S8("struct __attribute__((packed)) before { char c; int i; };\n"
                                                          "struct after { char c; int i; } __attribute__((aligned(32)));\n"
                                                          "struct neither { char c; int i; };\n"),
                                                       (CPreprocessOptions){
                                                           .target = target_native,
                                                           .data_layout = target_data_layout(target_native),
                                                       });
    CParseResult record_parse = c_parse(record_temporary.arena, record_preprocess);
    BUSTER_TEST(arguments, record_preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, record_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, record_parse.aggregate_attribute_count == 2);
    for (u32 type_index = 0; type_index < record_parse.type_count; type_index += 1)
    {
        CType* type = record_parse.types + type_index;
        if (type->kind != C_TYPE_STRUCT || !type->is_complete)
        {
            continue;
        }
        CAggregateAttributes attributes = c_parse_aggregate_attributes(&record_parse, (CTypeId){.value = type_index});
        if (string_equal(type->tag, S8("before")))
        {
            BUSTER_TEST(arguments, attributes.is_packed && attributes.alignment_count == 0);
        }
        else if (string_equal(type->tag, S8("after")))
        {
            BUSTER_TEST(arguments, !attributes.is_packed && attributes.alignment_count == 1);
        }
        else if (string_equal(type->tag, S8("neither")))
        {
            BUSTER_TEST(arguments, !attributes.is_packed && attributes.alignment_count == 0);
        }
    }
    scratch_end(record_temporary);

    // A bit-field declarator has exactly one place to carry a list of its own,
    // after the width, and reading that list as part of the width -- #693 --
    // left the width unfoldable, so the aggregate never reached a layout at
    // all. The numbers below are what it reaches now: `b` crosses the storage
    // unit `a` opened, so the packed spelling takes the next bit while the
    // unpacked one starts a new unit two bytes further on, and `tail` is the
    // member that separates them. `leading_bits` writes the list on the first
    // declarator of the same list instead, where packing a field that already
    // sits on a boundary moves nothing.
    TemporalArena bit_attribute_temporary = scratch_begin(0, 0);
    CPreprocessResult bit_attribute_preprocess = {0};
    CParseResult bit_attribute_parse = {0};
    CIRLowerResult bit_attribute =
        c_test_lower_source(bit_attribute_temporary.arena,
                            S8("struct trailing_bits { char byte; int a : 8; int b : 24 __attribute__((packed)); char tail; };\n"
                               "struct leading_bits { char byte; int a : 8 __attribute__((packed)), b : 24; char tail; };\n"
                               "struct trailing_bits trailing_bits_object;\n"
                               "struct leading_bits leading_bits_object;\n"
                               "_Static_assert(sizeof(struct trailing_bits) == 8, \"trailing bits\");\n"
                               "_Static_assert(_Alignof(struct trailing_bits) == 4, \"trailing bits alignment\");\n"
                               "_Static_assert(__builtin_offsetof(struct trailing_bits, tail) == 5, \"trailing bits tail\");\n"
                               "_Static_assert(sizeof(struct leading_bits) == 8, \"leading bits\");\n"
                               "_Static_assert(_Alignof(struct leading_bits) == 4, \"leading bits alignment\");\n"
                               "_Static_assert(__builtin_offsetof(struct leading_bits, tail) == 7, \"leading bits tail\");\n"),
                            S8("packed-bit-attribute.c"), target_native, &bit_attribute_preprocess, &bit_attribute_parse);
    BUSTER_TEST(arguments, bit_attribute_preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, bit_attribute_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, bit_attribute.diagnostic_count == 0);
    if (bit_attribute.program)
    {
        // The unpacked field names the storage unit that contains it and its
        // position inside; the packed one names the byte its bits start in.
        // So the (offset, bit_offset) pair of each bit-field is also which of
        // the two carries the attribute, and no other member has to be read
        // to tell the two declarations apart.
        struct
        {
            String8 object;
            u64 first_bit_field_offset;
            u32 first_bit_field_bit_offset;
            u64 second_bit_field_offset;
            u64 tail_offset;
        } bit_attribute_aggregates[] = {
            {S8("trailing_bits_object"), 0, 8, 2, 5},
            {S8("leading_bits_object"), 1, 0, 4, 7},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(bit_attribute_aggregates); index += 1)
        {
            IrGlobal* global = c_test_find_ir_global(bit_attribute.program->modules, bit_attribute.program, bit_attribute_aggregates[index].object);
            IrType* type = global ? ir_type_from_id(&bit_attribute.program->types, global->type) : 0;
            BUSTER_TEST(arguments, type != 0 && type->field_count == 4);
            if (type && type->field_count == 4)
            {
                BUSTER_TEST(arguments, type->layout.size == 8 && type->layout.alignment == 4);
                BUSTER_TEST(arguments, type->fields[1].is_bit_field && type->fields[1].bit_width == 8);
                BUSTER_TEST(arguments, type->fields[1].offset == bit_attribute_aggregates[index].first_bit_field_offset);
                BUSTER_TEST(arguments, type->fields[1].bit_offset == bit_attribute_aggregates[index].first_bit_field_bit_offset);
                BUSTER_TEST(arguments, type->fields[2].is_bit_field && type->fields[2].bit_width == 24);
                BUSTER_TEST(arguments, type->fields[2].offset == bit_attribute_aggregates[index].second_bit_field_offset);
                BUSTER_TEST(arguments, type->fields[2].bit_offset == 0);
                BUSTER_TEST(arguments, type->fields[3].offset == bit_attribute_aggregates[index].tail_offset);
            }
        }
    }
    scratch_end(bit_attribute_temporary);

    // A packed bit-field whose bits cross every storage unit that fits has no
    // single-unit access at all -- not even a narrower one: thirty bits at bit
    // three of a five-byte aggregate leave no position for a four-byte unit
    // and do not fit in a smaller one. The access is then the bytes the bits
    // occupy, which is what Clang spells `i40` and lowers to a sequence of
    // ordinary ones; `access_size` carries the span and
    // `ir_field_access_pieces` names the accesses. The widths that land here
    // are exactly the ones whose byte count is not a power of two.
    struct
    {
        String8 source;
        String8 name;
        u64 size;
        u64 offset;
        u32 bit_offset;
        u32 field;
        u32 field_count;
        u8 access_size;
        u8 piece_count;
    } split_units[] = {
        {S8("struct __attribute__((packed)) straddling { int low : 3; int high : 30; };\n"
            "struct straddling straddling_object;\n"),
         S8("straddling_object"), 5, 0, 3, 1, 2, 5, 2},
        {S8("union __attribute__((packed)) wide_union { long long value : 40; };\n"
            "union wide_union wide_union_object;\n"),
         S8("wide_union_object"), 5, 0, 0, 0, 1, 5, 2},
        {S8("struct __attribute__((packed)) three_pieces { long long lead : 1; long long value : 55; };\n"
            "struct three_pieces three_pieces_object;\n"),
         S8("three_pieces_object"), 7, 0, 1, 1, 2, 7, 3},
        {S8("struct __attribute__((packed)) nine_bytes { long long lead : 1; long long value : 64; };\n"
            "struct nine_bytes nine_bytes_object;\n"),
         S8("nine_bytes_object"), 9, 0, 1, 1, 2, 9, 2},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(split_units); index += 1)
    {
        TemporalArena straddle_temporary = scratch_begin(0, 0);
        CPreprocessResult straddle_preprocess = {0};
        CParseResult straddle_parse = {0};
        CIRLowerResult straddle = c_test_lower_source(straddle_temporary.arena, split_units[index].source, S8("packed-straddle.c"), target_native,
                                                      &straddle_preprocess, &straddle_parse);
        BUSTER_TEST(arguments, straddle_preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, straddle_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, straddle.diagnostic_count == 0);
        IrModule* straddle_module = straddle.program && straddle.program->module_count ? &straddle.program->modules[0] : 0;
        IrGlobal* straddle_object = straddle_module ? c_test_find_ir_global(straddle_module, straddle.program, split_units[index].name) : 0;
        IrType* straddle_type = straddle_object ? ir_type_from_id(&straddle.program->types, straddle_object->type) : 0;
        BUSTER_TEST(arguments, straddle_type != 0 && straddle_type->field_count == split_units[index].field_count);
        if (straddle_type && straddle_type->field_count == split_units[index].field_count)
        {
            IrField* straddle_field = straddle_type->fields + split_units[index].field;
            BUSTER_TEST(arguments, straddle_type->layout.size == split_units[index].size && straddle_type->layout.alignment == 1);
            BUSTER_TEST(arguments, straddle_field->offset == split_units[index].offset && straddle_field->bit_offset == split_units[index].bit_offset);
            BUSTER_TEST(arguments, straddle_field->access_size == split_units[index].access_size);
            BUSTER_TEST(arguments, ir_field_access_size(&straddle.program->types, straddle_field) == split_units[index].access_size);
            IrFieldAccessPiece straddle_pieces[IR_FIELD_ACCESS_PIECE_CAPACITY];
            u32 straddle_piece_count = ir_field_access_pieces(split_units[index].access_size, straddle_pieces);
            BUSTER_TEST(arguments, straddle_piece_count == split_units[index].piece_count);
            u64 straddle_covered = 0;
            for (u32 piece = 0; piece < straddle_piece_count; piece += 1)
            {
                BUSTER_TEST(arguments, straddle_pieces[piece].offset == straddle_covered);
                straddle_covered += straddle_pieces[piece].size;
            }
            BUSTER_TEST(arguments, straddle_covered == split_units[index].access_size);
        }
        scratch_end(straddle_temporary);
    }

    // An array element has to be addressable at its own alignment in every
    // slot, so its size has to be a multiple of that alignment. A typedef's
    // `aligned` is the one spelling that can break that -- every other type is
    // sized at a multiple of its alignment by construction -- and it breaks it
    // in every spelling that names an array of such a scalar: the typedef, the
    // object declarator, the member, and a single-element array, which Clang
    // and GCC refuse as well since the element itself would still be laid out
    // in a slot narrower than it claims. The column is the opening bracket,
    // which is where Clang's own caret points.
    //
    // The last three reach the same message down a different road (#713).  The
    // parenthesized declarator builds its array type only once the syntax scan
    // stops reading `cache_line (` as a function named `cache_line`; the array
    // type name in an expression never reaches the parse type table at all, so
    // it is the lowering-time resolver that records it and c_lower_to_ir that
    // reports it once at the end.
    struct
    {
        String8 source;
        String8 path;
        u32 line;
        u32 column;
    } over_aligned_elements[] = {
        {S8("typedef int cache_line __attribute__((aligned(64)));\n"
            "typedef cache_line cache_array[2];\n"),
         S8("over-aligned-typedef.c"), 2, 31},
        {S8("typedef int cache_line __attribute__((aligned(64)));\n"
            "cache_line object[2];\n"),
         S8("over-aligned-object.c"), 2, 18},
        {S8("typedef int cache_line __attribute__((aligned(64)));\n"
            "struct holder { cache_line field[2]; };\n"
            "struct holder holder_object;\n"),
         S8("over-aligned-member.c"), 2, 33},
        {S8("typedef int cache_line __attribute__((aligned(64)));\n"
            "cache_line single[1];\n"),
         S8("over-aligned-single.c"), 2, 18},
        {S8("typedef int cache_line __attribute__((aligned(64)));\n"
            "cache_line (*pointer)[2];\n"),
         S8("over-aligned-parenthesized.c"), 2, 22},
        {S8("typedef int cache_line __attribute__((aligned(64)));\n"
            "int measure(void) { return (int)sizeof(cache_line[2]); }\n"),
         S8("over-aligned-type-name.c"), 2, 50},
        {S8("typedef int cache_line __attribute__((aligned(64)));\n"
            "int* literal(void) { return (cache_line[2]){1, 2}; }\n"),
         S8("over-aligned-compound-literal.c"), 2, 40},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(over_aligned_elements); index += 1)
    {
        TemporalArena over_aligned_temporary = scratch_begin(0, 0);
        CPreprocessResult over_aligned_preprocess = {0};
        CParseResult over_aligned_parse = {0};
        CIRLowerResult over_aligned = c_test_lower_source(over_aligned_temporary.arena, over_aligned_elements[index].source, over_aligned_elements[index].path,
                                                          target_native, &over_aligned_preprocess, &over_aligned_parse);
        BUSTER_TEST(arguments, over_aligned_preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, over_aligned_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, over_aligned.diagnostic_count == 1);
        if (over_aligned.diagnostic_count == 1)
        {
            BUSTER_TEST(arguments, over_aligned.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
            BUSTER_STRING_TEST(arguments, over_aligned.diagnostics[0].message,
                               S8("size of array element (4 bytes) is not a multiple of the alignment of 64 that __attribute__((aligned)) gave its type"));
            BUSTER_TEST(arguments, over_aligned.diagnostics[0].location.line == over_aligned_elements[index].line &&
                                       over_aligned.diagnostics[0].location.column == over_aligned_elements[index].column);
        }
        scratch_end(over_aligned_temporary);
    }

    // The neighbouring shapes that stay well-formed, and the reason the rule
    // is "size is not a multiple of the alignment" rather than "alignment
    // above the size": an aggregate's own `aligned` rounds its *size* up to
    // the alignment, so it tiles; a typedef that lowers an alignment leaves a
    // size that still divides; and a request the type already satisfies moves
    // nothing. All three are accepted by Clang and GCC.
    TemporalArena tiling_temporary = scratch_begin(0, 0);
    CPreprocessResult tiling_preprocess = {0};
    CParseResult tiling_parse = {0};
    CIRLowerResult tiling = c_test_lower_source(tiling_temporary.arena,
                                                S8("struct __attribute__((aligned(16))) padded { char byte; };\n"
                                                   "typedef int lowered __attribute__((aligned(2)));\n"
                                                   "typedef int exact __attribute__((aligned(4)));\n"
                                                   "struct padded padded_array[2];\n"
                                                   "lowered lowered_array[2];\n"
                                                   "exact exact_array[2];\n"
                                                   "_Static_assert(sizeof(padded_array) == 32, \"padded array\");\n"
                                                   "_Static_assert(sizeof(lowered_array) == 8, \"lowered array\");\n"
                                                   "_Static_assert(sizeof(exact_array) == 8, \"exact array\");\n"),
                                                S8("tiling-elements.c"), target_native, &tiling_preprocess, &tiling_parse);
    BUSTER_TEST(arguments, tiling_preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, tiling_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, tiling.diagnostic_count == 0);
    if (tiling.program)
    {
        IrGlobal* padded_array = c_test_find_ir_global(tiling.program->modules, tiling.program, S8("padded_array"));
        IrType* padded_array_type = padded_array ? ir_type_from_id(&tiling.program->types, padded_array->type) : 0;
        BUSTER_TEST(arguments, padded_array_type != 0 && padded_array_type->layout.size == 32 && padded_array_type->layout.alignment == 16);
        IrGlobal* lowered_array = c_test_find_ir_global(tiling.program->modules, tiling.program, S8("lowered_array"));
        IrType* lowered_array_type = lowered_array ? ir_type_from_id(&tiling.program->types, lowered_array->type) : 0;
        BUSTER_TEST(arguments, lowered_array_type != 0 && lowered_array_type->layout.size == 8 && lowered_array_type->layout.alignment == 2);
    }
    scratch_end(tiling_temporary);

    // C reserves the zero width for the *unnamed* bit-field, where it declares
    // no member at all and only moves the next one to its type's boundary; a
    // named member of zero width is refused (C23 6.7.3.2p4), as it is by both
    // reference compilers. Accepted, it laid out a member that occupies no
    // bits and could still be assigned and read back. Each spelling is its own
    // lowering because one report is issued per aggregate: the literal width,
    // which the parse fast path also folds, the constant expression, which
    // only the lowering below folds, and the union, whose members never share
    // a storage unit and so reach the width along a different arm.
    struct
    {
        String8 source;
        String8 path;
    } zero_width_named_sources[] = {
        {S8("struct named_zero_width { char c; int b : 0; };\n"), S8("named-zero-width.c")},
        {S8("struct named_zero_width_expression { char c; int b : 1 - 1; };\n"), S8("named-zero-width-expression.c")},
        {S8("union __attribute__((packed)) named_zero_width_union { char c; int b : 0; };\n"), S8("named-zero-width-union.c")},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(zero_width_named_sources); index += 1)
    {
        TemporalArena named_zero_temporary = scratch_begin(0, 0);
        CPreprocessResult named_zero_preprocess = {0};
        CParseResult named_zero_parse = {0};
        CIRLowerResult named_zero = c_test_lower_source(named_zero_temporary.arena, zero_width_named_sources[index].source,
                                                        zero_width_named_sources[index].path, target_native, &named_zero_preprocess, &named_zero_parse);
        BUSTER_TEST(arguments, named_zero_preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, named_zero_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, named_zero.diagnostic_count == 1);
        if (named_zero.diagnostic_count == 1)
        {
            BUSTER_TEST(arguments, named_zero.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_BIT_FIELD_WIDTH);
            BUSTER_STRING_TEST(arguments, named_zero.diagnostics[0].message, S8("named bit-field 'b' has zero width"));
        }
        scratch_end(named_zero_temporary);
    }

    // The unnamed spelling is the one the rule exists for, and it keeps the
    // layout it has always had: three bits, a boundary, three more bits in the
    // second storage unit. `struct __attribute__((packed)) zero_width_bits` in
    // tests/basic_c_packed_layout.c covers the packed half of the same rule.
    TemporalArena unnamed_zero_temporary = scratch_begin(0, 0);
    CPreprocessResult unnamed_zero_preprocess = {0};
    CParseResult unnamed_zero_parse = {0};
    CIRLowerResult unnamed_zero = c_test_lower_source(unnamed_zero_temporary.arena,
                                                      S8("struct unnamed_zero_width { int a : 3; int : 0; int b : 3; };\n"
                                                         "struct unnamed_zero_width unnamed_zero_width_object;\n"
                                                         "_Static_assert(sizeof(struct unnamed_zero_width) == 8, \"unnamed zero width\");\n"),
                                                      S8("unnamed-zero-width.c"), target_native, &unnamed_zero_preprocess, &unnamed_zero_parse);
    BUSTER_TEST(arguments, unnamed_zero_preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, unnamed_zero_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, unnamed_zero.diagnostic_count == 0);
    if (unnamed_zero.program)
    {
        IrGlobal* unnamed_zero_global = c_test_find_ir_global(unnamed_zero.program->modules, unnamed_zero.program, S8("unnamed_zero_width_object"));
        IrType* unnamed_zero_type = unnamed_zero_global ? ir_type_from_id(&unnamed_zero.program->types, unnamed_zero_global->type) : 0;
        BUSTER_TEST(arguments, unnamed_zero_type != 0 && unnamed_zero_type->field_count == 3);
        if (unnamed_zero_type && unnamed_zero_type->field_count == 3)
        {
            BUSTER_TEST(arguments, unnamed_zero_type->layout.size == 8 && unnamed_zero_type->layout.alignment == 4);
            BUSTER_TEST(arguments, unnamed_zero_type->fields[1].is_bit_field && unnamed_zero_type->fields[1].bit_width == 0);
            BUSTER_TEST(arguments, unnamed_zero_type->fields[2].offset == 4 && unnamed_zero_type->fields[2].bit_offset == 0);
        }
    }
    scratch_end(unnamed_zero_temporary);

    // The parse-side half of #713.  A top-level `(` right after an identifier
    // was read as the parameter list of a function that identifier names, so
    // `plain (*pointer)[3]` became a function called `plain` and the object
    // the declaration really makes was dropped whole -- no global, and a
    // `sizeof(*pointer)` that folded nothing.  A parameter is a declaration
    // and so can never begin with `*`, which is what tells the two apart
    // without a typedef table.  The tag spelling took the same road, and the
    // function-pointer and ordinary-function shapes have to stay where they
    // were.
    TemporalArena pointee_temporary = scratch_begin(0, 0);
    CPreprocessResult pointee_preprocess = {0};
    CParseResult pointee_parse = {0};
    CIRLowerResult pointee = c_test_lower_source(pointee_temporary.arena,
                                                 S8("typedef int plain;\n"
                                                    "struct tag { int x; int y; };\n"
                                                    "plain (*typedef_pointer)[3];\n"
                                                    "struct tag (*tag_pointer)[2];\n"
                                                    "plain (*function_pointer)(void);\n"
                                                    "plain ordinary_function(int argument);\n"
                                                    "_Static_assert(sizeof(*typedef_pointer) == 12, \"typedef pointee\");\n"
                                                    "_Static_assert(sizeof(*tag_pointer) == 16, \"tag pointee\");\n"
                                                    "_Static_assert(sizeof(typedef_pointer) == sizeof(void*), \"pointer\");\n"),
                                                 S8("pointer-to-array.c"), target_native, &pointee_preprocess, &pointee_parse);
    BUSTER_TEST(arguments, pointee_preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, pointee_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, pointee.diagnostic_count == 0);
    if (pointee.program)
    {
        // The globals exist at all, which is the half of the defect no
        // diagnostic reported: the dropped declaration was silent.
        IrGlobal* typedef_pointer = c_test_find_ir_global(pointee.program->modules, pointee.program, S8("typedef_pointer"));
        IrType* typedef_pointer_type = typedef_pointer ? ir_type_from_id(&pointee.program->types, typedef_pointer->type) : 0;
        BUSTER_TEST(arguments, typedef_pointer_type != 0 && typedef_pointer_type->kind == IR_TYPE_POINTER);
        IrType* typedef_pointee = typedef_pointer_type ? ir_type_from_id(&pointee.program->types, typedef_pointer_type->element_type) : 0;
        BUSTER_TEST(arguments, typedef_pointee != 0 && typedef_pointee->kind == IR_TYPE_ARRAY && typedef_pointee->layout.size == 12);
        IrGlobal* tag_pointer = c_test_find_ir_global(pointee.program->modules, pointee.program, S8("tag_pointer"));
        IrType* tag_pointer_type = tag_pointer ? ir_type_from_id(&pointee.program->types, tag_pointer->type) : 0;
        BUSTER_TEST(arguments, tag_pointer_type != 0 && tag_pointer_type->kind == IR_TYPE_POINTER);
        IrType* tag_pointee = tag_pointer_type ? ir_type_from_id(&pointee.program->types, tag_pointer_type->element_type) : 0;
        BUSTER_TEST(arguments, tag_pointee != 0 && tag_pointee->kind == IR_TYPE_ARRAY && tag_pointee->layout.size == 16);
        IrGlobal* function_pointer = c_test_find_ir_global(pointee.program->modules, pointee.program, S8("function_pointer"));
        IrType* function_pointer_type = function_pointer ? ir_type_from_id(&pointee.program->types, function_pointer->type) : 0;
        BUSTER_TEST(arguments, function_pointer_type != 0 && function_pointer_type->kind == IR_TYPE_POINTER);
    }
    scratch_end(pointee_temporary);

    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_constant_entity_lookup(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    TemporalArena temporary = scratch_begin(0, 0);
    Arena* arena = temporary.arena;
    String8 source_parts[600] = {0};
    u32 source_part_count = 0;
    source_parts[source_part_count++] = S8("void constant_lookup(void) { enum { shadow = 1, ");
    for (u32 index = 0; index < 256; index += 1)
    {
        source_parts[source_part_count++] = string_format(arena, S8("lookup_{u32} = {u32}"), index, index);
        if (index + 1 < 256)
        {
            source_parts[source_part_count++] = S8(", ");
        }
    }
    source_parts[source_part_count++] = S8(" }; { enum { shadow = 2 }; (void)shadow; } }\n");
    String8 source = c_test_concatenate(arena, source_parts, source_part_count);
    CPreprocessResult preprocess = c_preprocess(arena, source,
                                                (CPreprocessOptions){
                                                    .target = target_native,
                                                    .data_layout = target_data_layout(target_native),
                                                });
    CParseResult parse = c_parse(arena, preprocess);
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.name_lookup_buckets != 0 && parse.entity_lookup_bucket_count != 0);

    CEntityId* token_entities = arena_allocate(arena, CEntityId, preprocess.token_count);
    memset(token_entities, 0xff, sizeof(*token_entities) * preprocess.token_count);

    CEntityId oldest_shadow = C_ENTITY_ID_INVALID;
    u32 shadow_count = 0;
    for (u32 entity_index = 0; entity_index < parse.entity_count; entity_index += 1)
    {
        CEntity* entity = parse.entities + entity_index;
        if (!string_equal(entity->name, S8("shadow")))
        {
            continue;
        }
        if (oldest_shadow.value == C_ID_UNDERLYING_INVALID)
        {
            oldest_shadow = (CEntityId){.value = entity_index};
        }
        shadow_count += 1;
    }
    BUSTER_TEST(arguments, shadow_count == 2);
    BUSTER_TEST(arguments, c_test_ir_constant_entity_index_equivalent(&parse, preprocess, token_entities, (u32)preprocess.token_count));
    BUSTER_TEST(arguments, c_test_ir_constant_entity_index_lifetime(&parse, preprocess, token_entities, (u32)preprocess.token_count));

    // A forged nonzero token symbol must not turn an empty symbol bucket into
    // an early invalid result. Validate the token spelling first, then keep
    // the ascending compatibility scan authoritative for this malformed case.
    CToken* malformed_symbol_tokens = arena_allocate(arena, CToken, preprocess.token_count);
    memcpy(malformed_symbol_tokens, preprocess.tokens, sizeof(*malformed_symbol_tokens) * preprocess.token_count);
    u32 shadow_token = UINT32_MAX;
    u32 wrong_symbol = 0;
    for (u32 token_index = 0; token_index < preprocess.token_count; token_index += 1)
    {
        String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]);
        if (preprocess.tokens[token_index].kind == C_TOKEN_IDENTIFIER && string_equal(spelling, S8("shadow")) && shadow_token == UINT32_MAX)
        {
            shadow_token = token_index;
        }
        if (preprocess.tokens[token_index].symbol && string_equal(spelling, S8("void")))
        {
            wrong_symbol = preprocess.tokens[token_index].symbol;
        }
    }
    BUSTER_TEST(arguments, shadow_token != UINT32_MAX && wrong_symbol != 0);
    if (shadow_token != UINT32_MAX && wrong_symbol != 0)
    {
        malformed_symbol_tokens[shadow_token].symbol = wrong_symbol;
        CPreprocessResult malformed_symbol_preprocess = preprocess;
        malformed_symbol_preprocess.tokens = malformed_symbol_tokens;
        BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&parse, malformed_symbol_preprocess, token_entities, shadow_token).value == oldest_shadow.value);
    }

    bool saw_shadow_fallback = false;
    for (u32 token_index = 0; token_index < preprocess.token_count; token_index += 1)
    {
        CToken token = preprocess.tokens[token_index];
        if (token.kind != C_TOKEN_IDENTIFIER)
        {
            continue;
        }
        String8 name = c_token_spelling(preprocess.spelling_base, token);
        CEntityId expected = C_ENTITY_ID_INVALID;
        for (u32 entity_index = 0; entity_index < parse.entity_count; entity_index += 1)
        {
            if (string_equal(parse.entities[entity_index].name, name))
            {
                expected = (CEntityId){.value = entity_index};
                break;
            }
        }
        CEntityId actual = c_test_ir_constant_entity_at(&parse, preprocess, token_entities, token_index);
        BUSTER_TEST(arguments, actual.value == expected.value);
        if (string_equal(name, S8("shadow")) && actual.value == oldest_shadow.value)
        {
            saw_shadow_fallback = true;
        }
    }
    BUSTER_TEST(arguments, saw_shadow_fallback);

    // A hand-built parse has no symbol table, so its name buckets use the
    // spelling hash. Put two different names in the same one-slot bucket to
    // exercise collision filtering independently of the real parse's dense
    // symbol-id buckets.
    String8 collision_spellings = S8("alpha beta");
    CToken collision_tokens[] = {
        {.offset = 0, .symbol = 0, .length = 5, .kind = C_TOKEN_IDENTIFIER},
        {.offset = 6, .symbol = 0, .length = 4, .kind = C_TOKEN_IDENTIFIER},
    };
    CEntity collision_entities[] = {
        {.name = S8("alpha"), .scope = {.value = 0}, .next_by_name = {.value = 1}},
        {.name = S8("beta"), .scope = {.value = 0}, .next_by_name = C_ENTITY_ID_INVALID},
    };
    CScope collision_scopes[] = {{.parent = C_SCOPE_ID_INVALID}};
    CEntityId collision_entity_buckets[] = {C_ENTITY_ID_INVALID};
    CEntityId collision_name_buckets[] = {{.value = 1}};
    CEntityId collision_token_entities[] = {C_ENTITY_ID_INVALID, C_ENTITY_ID_INVALID};
    CPreprocessResult collision_preprocess = {
        .tokens = collision_tokens,
        .spelling_base = collision_spellings.pointer,
        .token_count = BUSTER_ARRAY_LENGTH(collision_tokens),
    };
    CParseResult collision_parse = {
        .entities = collision_entities,
        .scopes = collision_scopes,
        .entity_lookup_buckets = collision_entity_buckets,
        .name_lookup_buckets = collision_name_buckets,
        .entity_count = BUSTER_ARRAY_LENGTH(collision_entities),
        .scope_count = BUSTER_ARRAY_LENGTH(collision_scopes),
        .entity_lookup_bucket_count = 1,
    };
    BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&collision_parse, collision_preprocess, collision_token_entities, 0).value == 0);
    BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&collision_parse, collision_preprocess, collision_token_entities, 1).value == 1);

    // Malformed hand-built chains must never dereference an out-of-range node
    // or loop forever. In either case, discard the partial bucket answer and
    // use the historical ascending entity scan.
    CEntity malformed_bucket_entities[] = {
        {.name = S8("alpha"), .next_by_name = {.value = 99}},
        {.name = S8("beta"), .next_by_name = C_ENTITY_ID_INVALID},
    };
    CEntityId malformed_bucket_head[] = {{.value = 0}};
    CParseResult malformed_bucket_parse = collision_parse;
    malformed_bucket_parse.entities = malformed_bucket_entities;
    malformed_bucket_parse.name_lookup_buckets = malformed_bucket_head;
    BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&malformed_bucket_parse, collision_preprocess, collision_token_entities, 0).value == 0);
    BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&malformed_bucket_parse, collision_preprocess, collision_token_entities, 1).value == 1);

    CEntity cycle_bucket_entities[] = {
        {.name = S8("alpha"), .next_by_name = {.value = 1}},
        {.name = S8("beta"), .next_by_name = {.value = 0}},
    };
    CEntityId cycle_bucket_head[] = {{.value = 0}};
    CParseResult cycle_bucket_parse = collision_parse;
    cycle_bucket_parse.entities = cycle_bucket_entities;
    cycle_bucket_parse.name_lookup_buckets = cycle_bucket_head;
    BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&cycle_bucket_parse, collision_preprocess, collision_token_entities, 0).value == 0);
    BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&cycle_bucket_parse, collision_preprocess, collision_token_entities, 1).value == 1);

    CEntity unordered_bucket_entities[] = {
        {.name = S8("alpha"), .next_by_name = {.value = 1}},
        {.name = S8("alpha"), .next_by_name = C_ENTITY_ID_INVALID},
    };
    CEntityId unordered_bucket_head[] = {{.value = 0}};
    CParseResult unordered_bucket_parse = collision_parse;
    unordered_bucket_parse.entities = unordered_bucket_entities;
    unordered_bucket_parse.entity_count = BUSTER_ARRAY_LENGTH(unordered_bucket_entities);
    unordered_bucket_parse.name_lookup_buckets = unordered_bucket_head;
    BUSTER_TEST(arguments, c_test_ir_constant_entity_at(&unordered_bucket_parse, collision_preprocess, collision_token_entities, 0).value == 0);
    scratch_end(temporary);
    return result;
}

#if BUSTER_COMPILER_CLANG
__attribute__((optnone))
#endif
UnitTestResult c_frontend_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    c_test_result_add(&result, c_test_frontend_lex_preprocess(arguments));
    c_test_result_add(&result, c_test_frontend_lex_differential(arguments));
    c_test_result_add(&result, c_test_position_index_tiles(arguments));
    c_test_result_add(&result, c_test_oversized_token_spellings(arguments));
    c_test_result_add(&result, c_test_frontend_source_metrics(arguments));
    c_test_result_add(&result, c_test_frontend_semantic_basics(arguments));
    c_test_result_add(&result, c_test_typedef_fallback_lookup(arguments));
    c_test_result_add(&result, c_test_frontend_global_types(arguments));
    c_test_result_add(&result, c_test_global_array_sizeof_bound(arguments));
    c_test_result_add(&result, c_test_sizeof_constant_expression(arguments));
    c_test_result_add(&result, c_test_sizeof_function_type_name(arguments));
    c_test_result_add(&result, c_test_declarator_ellipsis_depth(arguments));
    c_test_result_add(&result, c_test_unprototyped_call_arguments(arguments));
    c_test_result_add(&result, c_test_call_arity_diagnostics(arguments));
    c_test_result_add(&result, c_test_function_body_sizeof_expression(arguments));
    c_test_result_add(&result, c_test_frontend_control_flow(arguments));
    c_test_result_add(&result, c_test_for_declaration_scopes(arguments));
    c_test_result_add(&result, c_test_then_nested_conditionals(arguments));
    c_test_result_add(&result, c_test_conditional_type_prediction(arguments));
    c_test_result_add(&result, c_test_conditional_void_expression(arguments));
    c_test_result_add(&result, c_test_conditional_comma_assignment(arguments));
    c_test_result_add(&result, c_test_pointer_width_integer_conversion(arguments));
    c_test_result_add(&result, c_test_statement_expression_control_call(arguments));
    c_test_result_add(&result, c_test_statement_expression_nested_call(arguments));
    c_test_result_add(&result, c_test_statement_expression_control_value(arguments));
    c_test_result_add(&result, c_test_statement_expression_declaration_scope(arguments));
    c_test_result_add(&result, c_test_global_identifier_updates(arguments));
    c_test_result_add(&result, c_test_parenthesized_address_assignment_expression(arguments));
    c_test_result_add(&result, c_test_nested_offsetof_pointer_prediction(arguments));
    c_test_result_add(&result, c_test_casted_dereference_update(arguments));
    c_test_result_add(&result, c_test_parenthesized_function_declarations(arguments));
    c_test_result_add(&result, c_test_parenthesized_address_place_assignment(arguments));
    c_test_result_add(&result, c_test_frontend_vectors(arguments));
    c_test_result_add(&result, c_test_frontend_scratch_and_hardening(arguments));
    c_test_result_add(&result, c_test_inline_assembly_volatile_ir(arguments));
    c_test_result_add(&result, c_test_frontend_vla_and_ir(arguments));
    c_test_result_add(&result, c_test_local_static_aggregates(arguments));

    c_test_result_add(&result, c_test_wide_float_function_signatures(arguments));
    c_test_result_add(&result, c_test_wide_float_signature_calls(arguments));
    c_test_result_add(&result, c_test_wide_float_cleanup_signature_calls(arguments));
    c_test_result_add(&result, c_test_wide_float_local_transport(arguments));
    c_test_result_add(&result, c_test_wide_float_global_initializers(arguments));
    c_test_result_add(&result, c_test_wide_float_global_rejections(arguments));
    c_test_result_add(&result, c_test_wide_float_android_rejection(arguments));
    c_test_result_add(&result, c_test_wide_float_global_boundaries(arguments));
    c_test_result_add(&result, c_test_wide_float_global_braces(arguments));
    c_test_result_add(&result, c_test_wide_float_global_folding(arguments));
    c_test_result_add(&result, c_test_constant_entity_lookup(arguments));

    c_test_result_add(&result, c_test_static_range_designators(arguments));

    c_test_result_add(&result, c_test_u64_initializer_slots(arguments));

    c_test_result_add(&result, c_test_parse_storage_growth(arguments));

    c_test_result_add(&result, c_test_type_parse_rollback_growth(arguments));

    c_test_result_add(&result, c_test_aggregate_corrections(arguments));

    c_test_result_add(&result, c_test_brace_designators(arguments));

    c_test_result_add(&result, c_test_c23_attribute_positions(arguments));

    c_test_result_add(&result, c_test_c23_attribute_noreturn(arguments));

    c_test_result_add(&result, c_test_c23_empty_initializers(arguments));

    c_test_result_add(&result, c_test_initializer_separators(arguments));

    c_test_result_add(&result, c_test_ambiguous_promoted_ir(arguments));

    c_test_result_add(&result, c_test_ambiguous_promoted_parse(arguments));

    c_test_result_add(&result, c_test_invalid_union_initializer(arguments));

    c_test_result_add(&result, c_test_deferred_assert_positive(arguments));

    c_test_result_add(&result, c_test_deferred_assert_false(arguments));

    c_test_result_add(&result, c_test_deferred_assert_nonconstant(arguments));

    c_test_result_add(&result, c_test_local_tls(arguments));

    c_test_result_add(&result, c_test_invalid_local_static_initializer(arguments));

    c_test_result_add(&result, c_test_invalid_designators(arguments));

    c_test_result_add(&result, c_test_invalid_root_designators(arguments));

    c_test_result_add(&result, c_test_invalid_block_tls(arguments));

    {
        TemporalArena artifact_temporary = scratch_begin(0, 0);
        CPreprocessResult artifact_tokens = c_preprocess(
            artifact_temporary.arena,
            S8("typedef struct Pair { int left; int right; } Pair;"
               " typedef float Float4 __attribute__((vector_size(16)));"
               " typedef __int128 Wide;"
               " typedef void *va_list;"
               " static int callback(int value);"
               " static int callback_two(int value) { return value + 2; }"
               " static int (*callbacks[2])(int) = { callback, callback_two };"
               " static const int answer = 3 * 4 + (5 > 2 ? 1 : 0);"
               " static const float fraction = 1.25f + 0.5f;"
               " static Pair pair = { .right = 9, .left = 4 };"
               " static int matrix[2][3] = { { 1, 2, 3 }, 4, 5, 6 };"
               " static char exact[3] = \"abc\";"
               " static int *address = &matrix[1][2];"
               " static int pointer_target;"
               " static int *const pointer_source = &pointer_target;"
               " static int *pointer_alias = pointer_source;"
               " static int short_false = 0 && (1 / 0);"
               " static int short_true = 1 || (1 / 0);"
               " static int short_conditional = 0 ? (1 / 0) : 7;"
               " static int callback(int value) { return value + pair.left; }"
               " static int apply(int (*)(int), int values[2][3])"
               " { return callbacks[0](values[0][0]) + values[1][2]; }"
               " int external_value = 3;"
               " static int local_static_and_extern(void)"
               " { static Pair local_pair = { .right = 3, .left = 1 }; extern int external_value;"
               " return local_pair.left + external_value; }"
               " static int statement_and_builtins(int input, int values[2][3])"
               " { int local_values[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };"
               " int chosen = __builtin_choose_expr(__builtin_constant_p(1 + 2), 7, input);"
               " int runtime_constant = __builtin_constant_p(input);"
               " int compatible = __builtin_types_compatible_p(int, int);"
               " unsigned long object_size = __builtin_object_size(local_values, 0);"
               " int (*aligned)[3] = __builtin_assume_aligned(local_values, 16);"
               " return chosen + runtime_constant + compatible + (object_size == sizeof(local_values))"
               " + (aligned[1][2] == values[1][2]) + ({ int local = input + 1; local * 2; }); }"
               " Wide wide_identity(Wide value);"
               " Float4 vector_identity(Float4 value);"
               " va_list va_identity(va_list value);"
               " int main(void) { int values[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };"
               " return apply(callback, matrix) == 11 && local_static_and_extern() == 4 &&"
               " statement_and_builtins(3, values) == 18 ? 0 : 1; }\n"),
            (CPreprocessOptions){0});
        CParseResult artifact_parse = c_parse(artifact_temporary.arena, artifact_tokens);
        CIRLowerResult artifact_ir =
            c_lower_to_ir(artifact_temporary.arena, S8("c-frontend-artifacts.c"), artifact_tokens, artifact_parse, target_native);
        BUSTER_TEST(arguments, artifact_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, artifact_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, artifact_ir.diagnostic_count == 0);
        BUSTER_TEST(arguments, artifact_ir.program != 0);
        if (artifact_ir.program)
        {
            IrModule* module = &artifact_ir.program->modules[0];
            IrGlobal* callbacks_global = 0;
            IrGlobal* answer_global = 0;
            IrGlobal* fraction_global = 0;
            IrGlobal* pair_global = 0;
            IrGlobal* matrix_global = 0;
            IrGlobal* exact_global = 0;
            IrGlobal* address_global = 0;
            IrGlobal* pointer_alias_global = 0;
            IrGlobal* short_false_global = 0;
            IrGlobal* short_true_global = 0;
            IrGlobal* short_conditional_global = 0;
            IrType* pair_type = 0;
            IrType* wide_type = 0;
            IrType* vector_type = 0;
            IrType* va_list_type = 0;
            IrFunction* apply_function = 0;
            IrFunction* statement_function = 0;
            IrFunction* local_static_function = 0;
            for (u32 type_index = 0; type_index < artifact_ir.program->types.count; type_index += 1)
            {
                IrType* type = artifact_ir.program->types.types + type_index;
                if (type->kind == IR_TYPE_STRUCT && string_equal(type->name, S8("Pair")))
                {
                    pair_type = type;
                }
                else if (type->kind == IR_TYPE_INTEGER && type->bit_width == 128)
                {
                    wide_type = type;
                }
                else if (type->kind == IR_TYPE_VECTOR && type->layout.size == 16 && type->element_count == 4)
                {
                    vector_type = type;
                }
                else if (type->kind == IR_TYPE_VA_LIST)
                {
                    va_list_type = type;
                }
            }
            for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
            {
                IrGlobal* global = module->globals + global_index;
                IrSymbol* symbol = ir_symbol_from_id(&artifact_ir.program->symbols, global->symbol);
                if (!symbol)
                {
                    continue;
                }
                if (string_equal(symbol->name, S8("callbacks")))
                {
                    callbacks_global = global;
                }
                else if (string_equal(symbol->name, S8("answer")))
                {
                    answer_global = global;
                }
                else if (string_equal(symbol->name, S8("fraction")))
                {
                    fraction_global = global;
                }
                else if (string_equal(symbol->name, S8("pair")))
                {
                    pair_global = global;
                }
                else if (string_equal(symbol->name, S8("matrix")))
                {
                    matrix_global = global;
                }
                else if (string_equal(symbol->name, S8("exact")))
                {
                    exact_global = global;
                }
                else if (string_equal(symbol->name, S8("address")))
                {
                    address_global = global;
                }
                else if (string_equal(symbol->name, S8("pointer_alias")))
                {
                    pointer_alias_global = global;
                }
                else if (string_equal(symbol->name, S8("short_false")))
                {
                    short_false_global = global;
                }
                else if (string_equal(symbol->name, S8("short_true")))
                {
                    short_true_global = global;
                }
                else if (string_equal(symbol->name, S8("short_conditional")))
                {
                    short_conditional_global = global;
                }
            }
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                if (string_equal(function->name, S8("apply")))
                {
                    apply_function = function;
                }
                else if (string_equal(function->name, S8("statement_and_builtins")))
                {
                    statement_function = function;
                }
                else if (string_equal(function->name, S8("local_static_and_extern")))
                {
                    local_static_function = function;
                }
            }
            BUSTER_TEST(arguments, callbacks_global != 0);
            BUSTER_TEST(arguments, answer_global != 0);
            BUSTER_TEST(arguments, fraction_global != 0);
            BUSTER_TEST(arguments, pair_global != 0);
            BUSTER_TEST(arguments, matrix_global != 0);
            BUSTER_TEST(arguments, exact_global != 0);
            BUSTER_TEST(arguments, address_global != 0);
            BUSTER_TEST(arguments, pointer_alias_global != 0);
            BUSTER_TEST(arguments, short_false_global != 0);
            BUSTER_TEST(arguments, short_true_global != 0);
            BUSTER_TEST(arguments, short_conditional_global != 0);
            BUSTER_TEST(arguments, pair_type != 0);
            BUSTER_TEST(arguments, wide_type != 0);
            BUSTER_TEST(arguments, vector_type != 0);
            BUSTER_TEST(arguments, va_list_type != 0);
            BUSTER_TEST(arguments, apply_function != 0);
            BUSTER_TEST(arguments, statement_function != 0);
            BUSTER_TEST(arguments, local_static_function != 0);
            if (callbacks_global)
            {
                BUSTER_TEST(arguments, callbacks_global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
                BUSTER_TEST(arguments, callbacks_global->bytes.length == 16);
                BUSTER_TEST(arguments, callbacks_global->relocation_count == 2);
            }
            if (answer_global)
            {
                BUSTER_TEST(arguments, answer_global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                BUSTER_TEST(arguments, answer_global->initializer_bits == 13);
            }
            if (fraction_global)
            {
                f32 expected_fraction = 1.75f;
                u32 expected_fraction_bits = 0;
                memcpy(&expected_fraction_bits, &expected_fraction, sizeof(expected_fraction_bits));
                BUSTER_TEST(arguments, fraction_global->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
                BUSTER_TEST(arguments, fraction_global->initializer_bits == expected_fraction_bits);
            }
            if (pair_global && pair_global->bytes.pointer && pair_global->bytes.length == 8)
            {
                u32 left = 0;
                u32 right = 0;
                memcpy(&left, pair_global->bytes.pointer, sizeof(left));
                memcpy(&right, pair_global->bytes.pointer + sizeof(left), sizeof(right));
                BUSTER_TEST(arguments, left == 4);
                BUSTER_TEST(arguments, right == 9);
            }
            if (matrix_global && matrix_global->bytes.pointer && matrix_global->bytes.length == 24)
            {
                u32 last = 0;
                memcpy(&last, matrix_global->bytes.pointer + 5 * sizeof(last), sizeof(last));
                BUSTER_TEST(arguments, last == 6);
            }
            if (exact_global && exact_global->bytes.pointer)
            {
                BUSTER_TEST(arguments, exact_global->bytes.length == 3);
                BUSTER_TEST(arguments, exact_global->bytes.length >= 3 && exact_global->bytes.pointer[0] == 'a');
                BUSTER_TEST(arguments, exact_global->bytes.length >= 3 && exact_global->bytes.pointer[1] == 'b');
                BUSTER_TEST(arguments, exact_global->bytes.length >= 3 && exact_global->bytes.pointer[2] == 'c');
            }
            if (address_global)
            {
                BUSTER_TEST(arguments, address_global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
                BUSTER_TEST(arguments, address_global->initializer_symbol.value != IR_ID_UNDERLYING_INVALID);
                BUSTER_TEST(arguments, address_global->initializer_addend == 20);
                if (address_global->relocation_count == 1 && address_global->relocations)
                {
                    BUSTER_TEST(arguments, address_global->relocations[0].addend == 20);
                }
            }
            if (pointer_alias_global)
            {
                BUSTER_TEST(arguments, pointer_alias_global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
                BUSTER_TEST(arguments, pointer_alias_global->initializer_symbol.value != IR_ID_UNDERLYING_INVALID);
                BUSTER_TEST(arguments, pointer_alias_global->initializer_addend == 0);
            }
            if (short_false_global)
            {
                BUSTER_TEST(arguments, short_false_global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
                BUSTER_TEST(arguments, short_false_global->initializer_bits == 0);
            }
            if (short_true_global)
            {
                BUSTER_TEST(arguments, short_true_global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                BUSTER_TEST(arguments, short_true_global->initializer_bits == 1);
            }
            if (short_conditional_global)
            {
                BUSTER_TEST(arguments, short_conditional_global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                BUSTER_TEST(arguments, short_conditional_global->initializer_bits == 7);
            }
            if (apply_function)
            {
                IrType* function_type = ir_type_from_id(&artifact_ir.program->types, apply_function->canonical_type);
                IrType* callback_parameter = function_type && function_type->parameter_count > 0
                                                  ? ir_type_from_id(&artifact_ir.program->types, function_type->parameter_types[0])
                                                  : 0;
                IrType* callback_function = callback_parameter && callback_parameter->kind == IR_TYPE_POINTER
                                                 ? ir_type_from_id(&artifact_ir.program->types, callback_parameter->element_type)
                                                 : 0;
                IrType* array_parameter = function_type && function_type->parameter_count > 1
                                               ? ir_type_from_id(&artifact_ir.program->types, function_type->parameter_types[1])
                                               : 0;
                IrType* array_element = array_parameter && array_parameter->kind == IR_TYPE_POINTER
                                             ? ir_type_from_id(&artifact_ir.program->types, array_parameter->element_type)
                                             : 0;
                BUSTER_TEST(arguments, function_type && function_type->parameter_count == 2);
                BUSTER_TEST(arguments, callback_function && callback_function->kind == IR_TYPE_FUNCTION);
                BUSTER_TEST(arguments, array_parameter && array_parameter->kind == IR_TYPE_POINTER);
                BUSTER_TEST(arguments, array_element && array_element->kind == IR_TYPE_ARRAY && array_element->element_count == 3);
                BUSTER_TEST(arguments, apply_function->state == IR_FUNCTION_LOWERED);
            }
            if (statement_function)
            {
                BUSTER_TEST(arguments, statement_function->state == IR_FUNCTION_LOWERED);
            }
            if (local_static_function)
            {
                BUSTER_TEST(arguments, local_static_function->state == IR_FUNCTION_LOWERED);
            }
            IrType* abi_types[] = {pair_type, wide_type, vector_type, va_list_type};
            for (u32 type_index = 0; type_index < BUSTER_ARRAY_LENGTH(abi_types); type_index += 1)
            {
                if (!abi_types[type_index])
                {
                    continue;
                }
                for (u32 convention = 0; convention < IR_ABI_CONVENTION_COUNT; convention += 1)
                {
                    IrAbiValue argument_abi = ir_type_abi_value(artifact_ir.program, abi_types[type_index]->id,
                                                                 (IrAbiConvention)convention, IR_ABI_USE_ARGUMENT);
                    BUSTER_TEST(arguments, argument_abi.part_count || argument_abi.indirect);
                    BUSTER_TEST(arguments, argument_abi.part_count <= IR_ABI_MAX_PARTS);
                }
            }
            if (wide_type)
            {
                IrAbiValue system_v = ir_type_abi_value(artifact_ir.program, wide_type->id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
                IrAbiValue win64 = ir_type_abi_value(artifact_ir.program, wide_type->id, IR_ABI_CONVENTION_WIN64_X86_64, IR_ABI_USE_ARGUMENT);
                IrAbiValue aapcs = ir_type_abi_value(artifact_ir.program, wide_type->id, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
                IrAbiValue darwin = ir_type_abi_value(artifact_ir.program, wide_type->id, IR_ABI_CONVENTION_DARWIN_AARCH64, IR_ABI_USE_ARGUMENT);
                IrAbiValue windows_aarch64 =
                    ir_type_abi_value(artifact_ir.program, wide_type->id, IR_ABI_CONVENTION_WINDOWS_AARCH64, IR_ABI_USE_ARGUMENT);
                BUSTER_TEST(arguments, system_v.part_count == 2 && !system_v.indirect);
                BUSTER_TEST(arguments, win64.indirect);
                BUSTER_TEST(arguments, aapcs.part_count == 2 && !aapcs.indirect);
                BUSTER_TEST(arguments, darwin.part_count == 2 && !darwin.indirect);
                BUSTER_TEST(arguments, windows_aarch64.part_count == 2 && !windows_aarch64.indirect);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(artifact_ir.program, module).error == IR_VALIDATION_NONE);
        }
        String8 abi_triples[] = {
            S8("x86_64-unknown-linux-gnu"),
            S8("x86_64-pc-windows-msvc"),
            S8("aarch64-unknown-linux-gnu"),
            S8("aarch64-apple-macos"),
            S8("aarch64-pc-windows-msvc"),
        };
        IrAbiConvention abi_conventions[] = {
            IR_ABI_CONVENTION_SYSTEMV_X86_64,
            IR_ABI_CONVENTION_WIN64_X86_64,
            IR_ABI_CONVENTION_AAPCS64,
            IR_ABI_CONVENTION_DARWIN_AARCH64,
            IR_ABI_CONVENTION_WINDOWS_AARCH64,
        };
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(abi_triples); target_index += 1)
        {
            TargetParseResult parsed_target = target_parse_triple(abi_triples[target_index]);
            BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
            if (parsed_target.error == TARGET_PARSE_ERROR_NONE)
            {
                BUSTER_TEST(arguments, ir_abi_convention_for_target(parsed_target.target) == abi_conventions[target_index]);
            }
        }
        scratch_end(artifact_temporary);
    }
    enum
    {
        C_TYPE_FUNCTION_POINTER_STRESS_DEPTH = 4096,
        C_TYPE_AGGREGATE_STRESS_DEPTH = 1024,
        C_TYPE_SPECIFIER_STRESS_DEPTH = 1024,
        C_IR_ARRAY_BOUND_STRESS_DEPTH = 1024,
        C_IR_NESTED_CALL_STRESS_DEPTH = 256,
    };
    {
        Arena* declarator_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
        String8 declarator_prefix = S8("int root(");
        String8 parameter_prefix = S8("int (*callback)(");
        String8 declarator_suffix = S8(");");
        u64 declarator_source_length = declarator_prefix.length +
                                       (u64)C_TYPE_FUNCTION_POINTER_STRESS_DEPTH * (parameter_prefix.length + 1) + S8("void").length +
                                       declarator_suffix.length;
        char8* declarator_source_pointer = arena_allocate(declarator_arena, char8, declarator_source_length);
        u64 declarator_at = 0;
        memcpy(declarator_source_pointer + declarator_at, declarator_prefix.pointer, declarator_prefix.length);
        declarator_at += declarator_prefix.length;
        for (u32 depth = 0; depth < C_TYPE_FUNCTION_POINTER_STRESS_DEPTH; depth += 1)
        {
            memcpy(declarator_source_pointer + declarator_at, parameter_prefix.pointer, parameter_prefix.length);
            declarator_at += parameter_prefix.length;
        }
        memcpy(declarator_source_pointer + declarator_at, S8("void").pointer, S8("void").length);
        declarator_at += S8("void").length;
        for (u32 depth = 0; depth < C_TYPE_FUNCTION_POINTER_STRESS_DEPTH; depth += 1)
        {
            declarator_source_pointer[declarator_at++] = ')';
        }
        memcpy(declarator_source_pointer + declarator_at, declarator_suffix.pointer, declarator_suffix.length);
        declarator_at += declarator_suffix.length;
        BUSTER_TEST(arguments, declarator_at == declarator_source_length);
        CPreprocessResult declarator_tokens = c_preprocess(
            declarator_arena, (String8){.pointer = declarator_source_pointer, .length = declarator_source_length}, (CPreprocessOptions){0});
        CParseResult declarator_parse = c_parse(declarator_arena, declarator_tokens);
        CIRLowerResult declarator_ir =
            c_lower_to_ir(declarator_arena, S8("function-pointer-stress.c"), declarator_tokens, declarator_parse, target_native);
        BUSTER_TEST(arguments, declarator_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, declarator_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, declarator_ir.diagnostic_count == 0);
        if (declarator_ir.program)
        {
            BUSTER_TEST(arguments, declarator_ir.program->types.count >= C_TYPE_FUNCTION_POINTER_STRESS_DEPTH * 2);
            BUSTER_TEST(arguments,
                        ir_validate_canonical_module(declarator_ir.program, declarator_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(declarator_arena, 1));
    }
    {
        Arena* typeof_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
        String8 typeof_prefix = S8("__typeof__(");
        String8 typeof_suffix = S8(" value; int main(void) { return 0; }");
        u64 typeof_source_length = (u64)C_TYPE_SPECIFIER_STRESS_DEPTH * (typeof_prefix.length + 1) + S8("int").length + typeof_suffix.length;
        char8* typeof_source_pointer = arena_allocate(typeof_arena, char8, typeof_source_length);
        u64 typeof_at = 0;
        for (u32 depth = 0; depth < C_TYPE_SPECIFIER_STRESS_DEPTH; depth += 1)
        {
            memcpy(typeof_source_pointer + typeof_at, typeof_prefix.pointer, typeof_prefix.length);
            typeof_at += typeof_prefix.length;
        }
        memcpy(typeof_source_pointer + typeof_at, S8("int").pointer, S8("int").length);
        typeof_at += S8("int").length;
        for (u32 depth = 0; depth < C_TYPE_SPECIFIER_STRESS_DEPTH; depth += 1)
        {
            typeof_source_pointer[typeof_at++] = ')';
        }
        memcpy(typeof_source_pointer + typeof_at, typeof_suffix.pointer, typeof_suffix.length);
        typeof_at += typeof_suffix.length;
        BUSTER_TEST(arguments, typeof_at == typeof_source_length);
        CPreprocessResult typeof_tokens =
            c_preprocess(typeof_arena, (String8){.pointer = typeof_source_pointer, .length = typeof_source_length}, (CPreprocessOptions){0});
        CParseResult typeof_parse = c_parse(typeof_arena, typeof_tokens);
        CIRLowerResult typeof_ir = c_lower_to_ir(typeof_arena, S8("typeof-stress.c"), typeof_tokens, typeof_parse, target_native);
        BUSTER_TEST(arguments, typeof_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, typeof_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, typeof_ir.diagnostic_count == 0);
        if (typeof_ir.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(typeof_ir.program, typeof_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(typeof_arena, 1));
    }
    {
        Arena* atomic_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
        String8 atomic_prefix = S8("_Atomic(");
        String8 atomic_suffix = S8(" value;");
        u64 atomic_source_length = (u64)C_TYPE_SPECIFIER_STRESS_DEPTH * (atomic_prefix.length + 1) + S8("int").length + atomic_suffix.length;
        char8* atomic_source_pointer = arena_allocate(atomic_arena, char8, atomic_source_length);
        u64 atomic_at = 0;
        for (u32 depth = 0; depth < C_TYPE_SPECIFIER_STRESS_DEPTH; depth += 1)
        {
            memcpy(atomic_source_pointer + atomic_at, atomic_prefix.pointer, atomic_prefix.length);
            atomic_at += atomic_prefix.length;
        }
        memcpy(atomic_source_pointer + atomic_at, S8("int").pointer, S8("int").length);
        atomic_at += S8("int").length;
        for (u32 depth = 0; depth < C_TYPE_SPECIFIER_STRESS_DEPTH; depth += 1)
        {
            atomic_source_pointer[atomic_at++] = ')';
        }
        memcpy(atomic_source_pointer + atomic_at, atomic_suffix.pointer, atomic_suffix.length);
        atomic_at += atomic_suffix.length;
        BUSTER_TEST(arguments, atomic_at == atomic_source_length);
        CPreprocessResult atomic_tokens =
            c_preprocess(atomic_arena, (String8){.pointer = atomic_source_pointer, .length = atomic_source_length}, (CPreprocessOptions){0});
        CParseResult atomic_parse = c_parse(atomic_arena, atomic_tokens);
        BUSTER_TEST(arguments, atomic_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, atomic_parse.diagnostic_count == 1);
        if (atomic_parse.diagnostic_count == 1)
        {
            BUSTER_TEST(arguments, atomic_parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ATOMIC_TYPE);
            BUSTER_TEST(arguments,
                        atomic_parse.diagnostics[0].location.offset == (u64)(C_TYPE_SPECIFIER_STRESS_DEPTH - 2) * atomic_prefix.length);
        }
        BUSTER_TEST(arguments, arena_destroy(atomic_arena, 1));
    }
    {
        Arena* aggregate_stress_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
        String8 aggregate_prefix = S8("struct Root { ");
        String8 aggregate_open = S8("struct { ");
        String8 aggregate_leaf = S8("int value;");
        String8 aggregate_close = S8(" } member;");
        String8 aggregate_suffix = S8(" }; struct Root root; int main(void) { return 0; }");
        u64 aggregate_source_length = aggregate_prefix.length +
                                      (u64)C_TYPE_AGGREGATE_STRESS_DEPTH * (aggregate_open.length + aggregate_close.length) +
                                      aggregate_leaf.length + aggregate_suffix.length;
        char8* aggregate_source_pointer = arena_allocate(aggregate_stress_arena, char8, aggregate_source_length);
        u64 aggregate_at = 0;
        memcpy(aggregate_source_pointer + aggregate_at, aggregate_prefix.pointer, aggregate_prefix.length);
        aggregate_at += aggregate_prefix.length;
        for (u32 depth = 0; depth < C_TYPE_AGGREGATE_STRESS_DEPTH; depth += 1)
        {
            memcpy(aggregate_source_pointer + aggregate_at, aggregate_open.pointer, aggregate_open.length);
            aggregate_at += aggregate_open.length;
        }
        memcpy(aggregate_source_pointer + aggregate_at, aggregate_leaf.pointer, aggregate_leaf.length);
        aggregate_at += aggregate_leaf.length;
        for (u32 depth = 0; depth < C_TYPE_AGGREGATE_STRESS_DEPTH; depth += 1)
        {
            memcpy(aggregate_source_pointer + aggregate_at, aggregate_close.pointer, aggregate_close.length);
            aggregate_at += aggregate_close.length;
        }
        memcpy(aggregate_source_pointer + aggregate_at, aggregate_suffix.pointer, aggregate_suffix.length);
        aggregate_at += aggregate_suffix.length;
        BUSTER_TEST(arguments, aggregate_at == aggregate_source_length);
        CPreprocessResult aggregate_stress_tokens = c_preprocess(
            aggregate_stress_arena, (String8){.pointer = aggregate_source_pointer, .length = aggregate_source_length}, (CPreprocessOptions){0});
        CParseResult aggregate_stress_parse = c_parse(aggregate_stress_arena, aggregate_stress_tokens);
        CIRLowerResult aggregate_stress_ir = c_lower_to_ir(aggregate_stress_arena, S8("aggregate-stress.c"), aggregate_stress_tokens,
                                                           aggregate_stress_parse, target_native);
        BUSTER_TEST(arguments, aggregate_stress_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, aggregate_stress_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, aggregate_stress_ir.diagnostic_count == 0);
        if (aggregate_stress_ir.program)
        {
            BUSTER_TEST(arguments, aggregate_stress_parse.member_count >= C_TYPE_AGGREGATE_STRESS_DEPTH + 1);
            BUSTER_TEST(arguments,
                        ir_validate_canonical_module(aggregate_stress_ir.program, aggregate_stress_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(aggregate_stress_arena, 1));
    }
    {
        Arena* array_bound_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
        String8 array_bound_prefix = S8("int values[");
        String8 sizeof_open = S8("sizeof(char[");
        String8 sizeof_close = S8("])");
        String8 array_bound_suffix = S8("]; int main(void) { return values[0]; }");
        u64 array_bound_source_length = array_bound_prefix.length +
                                        (u64)C_IR_ARRAY_BOUND_STRESS_DEPTH * (sizeof_open.length + sizeof_close.length) + 1 +
                                        array_bound_suffix.length;
        char8* array_bound_source_pointer = arena_allocate(array_bound_arena, char8, array_bound_source_length);
        u64 array_bound_at = 0;
        memcpy(array_bound_source_pointer + array_bound_at, array_bound_prefix.pointer, array_bound_prefix.length);
        array_bound_at += array_bound_prefix.length;
        for (u32 depth = 0; depth < C_IR_ARRAY_BOUND_STRESS_DEPTH; depth += 1)
        {
            memcpy(array_bound_source_pointer + array_bound_at, sizeof_open.pointer, sizeof_open.length);
            array_bound_at += sizeof_open.length;
        }
        array_bound_source_pointer[array_bound_at++] = '1';
        for (u32 depth = 0; depth < C_IR_ARRAY_BOUND_STRESS_DEPTH; depth += 1)
        {
            memcpy(array_bound_source_pointer + array_bound_at, sizeof_close.pointer, sizeof_close.length);
            array_bound_at += sizeof_close.length;
        }
        memcpy(array_bound_source_pointer + array_bound_at, array_bound_suffix.pointer, array_bound_suffix.length);
        array_bound_at += array_bound_suffix.length;
        BUSTER_TEST(arguments, array_bound_at == array_bound_source_length);
        CPreprocessResult array_bound_tokens = c_preprocess(
            array_bound_arena, (String8){.pointer = array_bound_source_pointer, .length = array_bound_source_length}, (CPreprocessOptions){0});
        CParseResult array_bound_parse = c_parse(array_bound_arena, array_bound_tokens);
        CIRLowerResult array_bound_ir =
            c_lower_to_ir(array_bound_arena, S8("array-bound-stress.c"), array_bound_tokens, array_bound_parse, target_native);
        BUSTER_TEST(arguments, array_bound_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, array_bound_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, array_bound_ir.diagnostic_count == 0);
        if (array_bound_ir.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(array_bound_ir.program, array_bound_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(array_bound_arena, 1));
    }
    c_test_result_add(&result, c_test_repeated_incomplete_arrays(arguments));

    c_test_result_add(&result, c_test_packed_and_aligned_layout(arguments));

    TemporalArena nested_temporary = scratch_begin(0, 0);
    String8 nested_prefix = S8("static int identity(int value)"
                               " { return value; }"
                               " int main(void) { return ");
    String8 nested_call = S8("identity(");
    String8 nested_suffix = S8("1; }");
    u64 nested_source_length = nested_prefix.length + (u64)C_IR_NESTED_CALL_STRESS_DEPTH * (nested_call.length + 1) + nested_suffix.length;
    char8* nested_source_pointer = arena_allocate(nested_temporary.arena, char8, nested_source_length);
    u64 nested_source_at = 0;
    memcpy(nested_source_pointer + nested_source_at, nested_prefix.pointer, nested_prefix.length);
    nested_source_at += nested_prefix.length;
    for (u32 depth = 0; depth < C_IR_NESTED_CALL_STRESS_DEPTH; depth += 1)
    {
        memcpy(nested_source_pointer + nested_source_at, nested_call.pointer, nested_call.length);
        nested_source_at += nested_call.length;
    }
    nested_source_pointer[nested_source_at++] = '1';
    for (u32 depth = 0; depth < C_IR_NESTED_CALL_STRESS_DEPTH; depth += 1)
    {
        nested_source_pointer[nested_source_at++] = ')';
    }
    memcpy(nested_source_pointer + nested_source_at, nested_suffix.pointer + 1, nested_suffix.length - 1);
    nested_source_at += nested_suffix.length - 1;
    BUSTER_TEST(arguments, nested_source_at == nested_source_length);
    String8 nested_source = {
        .pointer = nested_source_pointer,
        .length = nested_source_length,
    };
    CPreprocessResult nested_tokens = c_preprocess(nested_temporary.arena, nested_source, (CPreprocessOptions){0});
    CParseResult nested_parse = c_parse(nested_temporary.arena, nested_tokens);
    CIRLowerResult nested_ir = c_lower_to_ir(nested_temporary.arena, S8("nested-calls.c"), nested_tokens, nested_parse, target_native);
    BUSTER_TEST(arguments, nested_ir.diagnostic_count == 0);
    if (nested_ir.program)
    {
        IrFunction* nested_main = nested_ir.program->modules[0].functions + 1;
        u32 nested_call_count = 0;
        for (u32 instruction_index = 0; instruction_index < nested_main->instruction_count; instruction_index += 1)
        {
            nested_call_count += nested_main->instructions[instruction_index].opcode == IR_OPCODE_CALL;
        }
        BUSTER_TEST(arguments, nested_call_count == C_IR_NESTED_CALL_STRESS_DEPTH);
        BUSTER_TEST(arguments, ir_validate_canonical_module(nested_ir.program, &nested_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(nested_temporary);

    enum
    {
        C_IR_ASSIGNMENT_STRESS_DEPTH = 4096,
        C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH = 1024,
        C_IR_COMPOUND_LITERAL_STRESS_DEPTH = 1024,
        C_IR_CONDITIONAL_STRESS_DEPTH = 1024,
        C_IR_VLA_ASSEMBLY_STRESS_DEPTH = 1024,
        C_IR_SUBSCRIPT_STRESS_DEPTH = 1024,
        C_IR_FUNCTION_NAME_STRESS_COUNT = 1024,
    };
    {
        TemporalArena assignment_temporary = scratch_begin(0, 0);
        String8 assignment_prefix = S8("static int identity(int value) { return value; } int main(void) { int value; return identity(");
        String8 assignment = S8("value = ");
        String8 assignment_suffix = S8("1); }");
        u64 assignment_source_length =
            assignment_prefix.length + (u64)C_IR_ASSIGNMENT_STRESS_DEPTH * assignment.length + assignment_suffix.length;
        char8* assignment_source_pointer = arena_allocate(assignment_temporary.arena, char8, assignment_source_length);
        u64 assignment_at = 0;
        memcpy(assignment_source_pointer + assignment_at, assignment_prefix.pointer, assignment_prefix.length);
        assignment_at += assignment_prefix.length;
        for (u32 depth = 0; depth < C_IR_ASSIGNMENT_STRESS_DEPTH; depth += 1)
        {
            memcpy(assignment_source_pointer + assignment_at, assignment.pointer, assignment.length);
            assignment_at += assignment.length;
        }
        memcpy(assignment_source_pointer + assignment_at, assignment_suffix.pointer, assignment_suffix.length);
        assignment_at += assignment_suffix.length;
        BUSTER_TEST(arguments, assignment_at == assignment_source_length);
        CPreprocessResult assignment_tokens = c_preprocess(
            assignment_temporary.arena, (String8){.pointer = assignment_source_pointer, .length = assignment_source_length}, (CPreprocessOptions){0});
        CParseResult assignment_parse = c_parse(assignment_temporary.arena, assignment_tokens);
        CIRLowerResult assignment_lowered =
            c_lower_to_ir(assignment_temporary.arena, S8("assignment-stress.c"), assignment_tokens, assignment_parse, target_native);
        BUSTER_TEST(arguments, assignment_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, assignment_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, assignment_lowered.diagnostic_count == 0);
        if (assignment_lowered.program)
        {
            IrModule* assignment_module = assignment_lowered.program->modules;
            IrFunction* assignment_function = assignment_module->functions + 1;
            u32 assignment_store_count = 0;
            for (u32 instruction_index = 0; instruction_index < assignment_function->instruction_count; instruction_index += 1)
            {
                assignment_store_count += assignment_function->instructions[instruction_index].opcode == IR_OPCODE_STORE;
            }
            BUSTER_TEST(arguments, assignment_store_count == C_IR_ASSIGNMENT_STRESS_DEPTH);
            BUSTER_TEST(arguments, ir_validate_canonical_module(assignment_lowered.program, assignment_module).error == IR_VALIDATION_NONE);
        }
        scratch_end(assignment_temporary);
    }
    {
        TemporalArena statement_temporary = scratch_begin(0, 0);
        String8 statement_prefix = S8("int main(void) { return ");
        String8 statement_open = S8("({");
        String8 statement_close = S8(";})");
        String8 statement_suffix = S8("; }");
        u64 statement_source_length = statement_prefix.length +
                                      (u64)C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH * (statement_open.length + statement_close.length) + 1 +
                                      statement_suffix.length;
        char8* statement_source_pointer = arena_allocate(statement_temporary.arena, char8, statement_source_length);
        u64 statement_at = 0;
        memcpy(statement_source_pointer + statement_at, statement_prefix.pointer, statement_prefix.length);
        statement_at += statement_prefix.length;
        for (u32 depth = 0; depth < C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH; depth += 1)
        {
            memcpy(statement_source_pointer + statement_at, statement_open.pointer, statement_open.length);
            statement_at += statement_open.length;
        }
        statement_source_pointer[statement_at++] = '7';
        for (u32 depth = 0; depth < C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH; depth += 1)
        {
            memcpy(statement_source_pointer + statement_at, statement_close.pointer, statement_close.length);
            statement_at += statement_close.length;
        }
        memcpy(statement_source_pointer + statement_at, statement_suffix.pointer, statement_suffix.length);
        statement_at += statement_suffix.length;
        BUSTER_TEST(arguments, statement_at == statement_source_length);
        CPreprocessResult statement_tokens = c_preprocess(
            statement_temporary.arena, (String8){.pointer = statement_source_pointer, .length = statement_source_length}, (CPreprocessOptions){0});
        CParseResult statement_parse = c_parse(statement_temporary.arena, statement_tokens);
        CIRLowerResult statement_lowered =
            c_lower_to_ir(statement_temporary.arena, S8("statement-expression-stress.c"), statement_tokens, statement_parse, target_native);
        BUSTER_TEST(arguments, statement_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, statement_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, statement_lowered.diagnostic_count == 0);
        if (statement_lowered.program)
        {
            IrModule* statement_module = statement_lowered.program->modules;
            IrFunction* statement_function = statement_module->functions;
            IrInstruction* return_instruction = 0;
            for (u32 instruction_index = 0; instruction_index < statement_function->instruction_count; instruction_index += 1)
            {
                if (statement_function->instructions[instruction_index].opcode == IR_OPCODE_RETURN)
                {
                    return_instruction = statement_function->instructions + instruction_index;
                }
            }
            BUSTER_TEST(arguments, return_instruction && return_instruction->operand_count == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(statement_lowered.program, statement_module).error == IR_VALIDATION_NONE);
        }
        scratch_end(statement_temporary);
    }
    {
        Arena* statement_nontrivial_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
        u64 statement_part_count = (u64)C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH * 2 + 3;
        String8* statement_parts = arena_allocate(statement_nontrivial_arena, String8, statement_part_count);
        u64 statement_part_index = 0;
        statement_parts[statement_part_index++] = S8("int main(void) { return ");
        for (u32 depth = 0; depth < C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH; depth += 1)
        {
            statement_parts[statement_part_index++] = S8("({ ");
        }
        statement_parts[statement_part_index++] = S8("1");
        for (u32 depth = 0; depth < C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH; depth += 1)
        {
            u32 local_index = C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH - depth - 1;
            statement_parts[statement_part_index++] =
                string_format(statement_nontrivial_arena, S8("; int local{u32} = 1; local{u32} + 1; }})"), local_index, local_index);
        }
        statement_parts[statement_part_index++] = S8("; }");
        BUSTER_TEST(arguments, statement_part_index == statement_part_count);
        String8 statement_source =
            string_join_arena(statement_nontrivial_arena, (SliceString8){.pointer = statement_parts, .length = statement_part_count}, false);
        CPreprocessResult statement_tokens = c_preprocess(
            statement_nontrivial_arena, statement_source, (CPreprocessOptions){0});
        CParseResult statement_parse = c_parse(statement_nontrivial_arena, statement_tokens);
        CIRLowerResult statement_lowered =
            c_lower_to_ir(statement_nontrivial_arena, S8("statement-expression-nontrivial-stress.c"), statement_tokens, statement_parse, target_native);
        BUSTER_TEST(arguments, statement_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, statement_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, statement_lowered.diagnostic_count == 0);
        if (statement_lowered.program)
        {
            IrModule* statement_module = statement_lowered.program->modules;
            IrFunction* statement_function = statement_module->functions;
            BUSTER_TEST(arguments, statement_function->local_count >= C_IR_STATEMENT_EXPRESSION_STRESS_DEPTH);
            BUSTER_TEST(arguments, ir_validate_canonical_module(statement_lowered.program, statement_module).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(statement_nontrivial_arena, 1));
    }
    {
        Arena* compound_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
        String8 compound_prefix = S8("int main(void) { return ");
        String8 compound_open = S8("(int){");
        String8 compound_suffix = S8("; }");
        u64 compound_source_length = compound_prefix.length +
                                     (u64)C_IR_COMPOUND_LITERAL_STRESS_DEPTH * (compound_open.length + 1) + 1 + compound_suffix.length;
        char8* compound_source_pointer = arena_allocate(compound_arena, char8, compound_source_length);
        u64 compound_at = 0;
        memcpy(compound_source_pointer + compound_at, compound_prefix.pointer, compound_prefix.length);
        compound_at += compound_prefix.length;
        for (u32 depth = 0; depth < C_IR_COMPOUND_LITERAL_STRESS_DEPTH; depth += 1)
        {
            memcpy(compound_source_pointer + compound_at, compound_open.pointer, compound_open.length);
            compound_at += compound_open.length;
        }
        compound_source_pointer[compound_at++] = '1';
        for (u32 depth = 0; depth < C_IR_COMPOUND_LITERAL_STRESS_DEPTH; depth += 1)
        {
            compound_source_pointer[compound_at++] = '}';
        }
        memcpy(compound_source_pointer + compound_at, compound_suffix.pointer, compound_suffix.length);
        compound_at += compound_suffix.length;
        BUSTER_TEST(arguments, compound_at == compound_source_length);
        CPreprocessResult compound_stress_tokens = c_preprocess(
            compound_arena, (String8){.pointer = compound_source_pointer, .length = compound_source_length}, (CPreprocessOptions){0});
        CParseResult compound_stress_parse = c_parse(compound_arena, compound_stress_tokens);
        CIRLowerResult compound_stress_lowered =
            c_lower_to_ir(compound_arena, S8("compound-literal-stress.c"), compound_stress_tokens, compound_stress_parse, target_native);
        BUSTER_TEST(arguments, compound_stress_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, compound_stress_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, compound_stress_lowered.diagnostic_count == 0);
        if (compound_stress_lowered.program)
        {
            IrFunction* compound_function = compound_stress_lowered.program->modules->functions;
            u32 compound_local_count = 0;
            u32 compound_store_count = 0;
            u32 compound_return_count = 0;
            for (u32 instruction_index = 0; instruction_index < compound_function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = compound_function->instructions[instruction_index].opcode;
                compound_local_count += opcode == IR_OPCODE_LOCAL;
                compound_store_count += opcode == IR_OPCODE_STORE;
                compound_return_count += opcode == IR_OPCODE_RETURN;
            }
            BUSTER_TEST(arguments, compound_local_count >= C_IR_COMPOUND_LITERAL_STRESS_DEPTH);
            BUSTER_TEST(arguments, compound_store_count >= C_IR_COMPOUND_LITERAL_STRESS_DEPTH);
            BUSTER_TEST(arguments, compound_return_count == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(compound_stress_lowered.program, compound_stress_lowered.program->modules).error ==
                                       IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(compound_arena, 1));
    }
    {
        Arena* conditional_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
        String8 conditional_prefix = S8("int main(void) { return ");
        String8 conditional_open = S8("(1 ? 1 + ");
        String8 conditional_close = S8(" : 0)");
        String8 conditional_suffix = S8("; }");
        u64 conditional_source_length = conditional_prefix.length +
                                        (u64)C_IR_CONDITIONAL_STRESS_DEPTH * (conditional_open.length + conditional_close.length) + 1 +
                                        conditional_suffix.length;
        char8* conditional_source_pointer = arena_allocate(conditional_arena, char8, conditional_source_length);
        u64 conditional_at = 0;
        memcpy(conditional_source_pointer + conditional_at, conditional_prefix.pointer, conditional_prefix.length);
        conditional_at += conditional_prefix.length;
        for (u32 depth = 0; depth < C_IR_CONDITIONAL_STRESS_DEPTH; depth += 1)
        {
            memcpy(conditional_source_pointer + conditional_at, conditional_open.pointer, conditional_open.length);
            conditional_at += conditional_open.length;
        }
        conditional_source_pointer[conditional_at++] = '1';
        for (u32 depth = 0; depth < C_IR_CONDITIONAL_STRESS_DEPTH; depth += 1)
        {
            memcpy(conditional_source_pointer + conditional_at, conditional_close.pointer, conditional_close.length);
            conditional_at += conditional_close.length;
        }
        memcpy(conditional_source_pointer + conditional_at, conditional_suffix.pointer, conditional_suffix.length);
        conditional_at += conditional_suffix.length;
        BUSTER_TEST(arguments, conditional_at == conditional_source_length);
        CPreprocessResult conditional_tokens = c_preprocess(
            conditional_arena, (String8){.pointer = conditional_source_pointer, .length = conditional_source_length}, (CPreprocessOptions){0});
        CParseResult conditional_parse = c_parse(conditional_arena, conditional_tokens);
        CIRLowerResult conditional_lowered =
            c_lower_to_ir(conditional_arena, S8("conditional-stress.c"), conditional_tokens, conditional_parse, target_native);
        BUSTER_TEST(arguments, conditional_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, conditional_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, conditional_lowered.diagnostic_count == 0);
        if (conditional_lowered.program)
        {
            IrFunction* conditional_function = conditional_lowered.program->modules->functions;
            u32 conditional_branch_count = 0;
            u32 conditional_branch_if_count = 0;
            u32 conditional_return_count = 0;
            for (u32 instruction_index = 0; instruction_index < conditional_function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = conditional_function->instructions[instruction_index].opcode;
                conditional_branch_count += opcode == IR_OPCODE_BRANCH;
                conditional_branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
                conditional_return_count += opcode == IR_OPCODE_RETURN;
            }
            BUSTER_TEST(arguments, conditional_branch_count >= C_IR_CONDITIONAL_STRESS_DEPTH * 2);
            // Constant conditions lower to unconditional branches, so the folded
            // '1 ?' conditions must not leave any conditional branches behind.
            BUSTER_TEST(arguments, conditional_branch_if_count == 0);
            BUSTER_TEST(arguments, conditional_return_count == 1);
            BUSTER_TEST(arguments,
                        ir_validate_canonical_module(conditional_lowered.program, conditional_lowered.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(conditional_arena, 1));
    }
    {
        Arena* vla_assembly_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
        String8 vla_assembly_prefix = S8("int main(int count) { int values[");
        String8 vla_assembly_open = S8("(count ? 1 + ");
        String8 vla_assembly_close = S8(" : 1)");
        String8 vla_assembly_middle = S8("]; __asm__ volatile (\"\" : : \"r\"(");
        String8 vla_assembly_suffix = S8(")); return values[0]; }");
        u64 nested_expression_length = (u64)C_IR_VLA_ASSEMBLY_STRESS_DEPTH * (vla_assembly_open.length + vla_assembly_close.length) +
                                       S8("count").length;
        u64 vla_assembly_source_length = vla_assembly_prefix.length + nested_expression_length + vla_assembly_middle.length +
                                         nested_expression_length + vla_assembly_suffix.length;
        char8* vla_assembly_source_pointer = arena_allocate(vla_assembly_arena, char8, vla_assembly_source_length);
        u64 vla_assembly_at = 0;
        memcpy(vla_assembly_source_pointer + vla_assembly_at, vla_assembly_prefix.pointer, vla_assembly_prefix.length);
        vla_assembly_at += vla_assembly_prefix.length;
        for (u32 expression = 0; expression < 2; expression += 1)
        {
            for (u32 depth = 0; depth < C_IR_VLA_ASSEMBLY_STRESS_DEPTH; depth += 1)
            {
                memcpy(vla_assembly_source_pointer + vla_assembly_at, vla_assembly_open.pointer, vla_assembly_open.length);
                vla_assembly_at += vla_assembly_open.length;
            }
            memcpy(vla_assembly_source_pointer + vla_assembly_at, S8("count").pointer, S8("count").length);
            vla_assembly_at += S8("count").length;
            for (u32 depth = 0; depth < C_IR_VLA_ASSEMBLY_STRESS_DEPTH; depth += 1)
            {
                memcpy(vla_assembly_source_pointer + vla_assembly_at, vla_assembly_close.pointer, vla_assembly_close.length);
                vla_assembly_at += vla_assembly_close.length;
            }
            String8 separator = expression ? vla_assembly_suffix : vla_assembly_middle;
            memcpy(vla_assembly_source_pointer + vla_assembly_at, separator.pointer, separator.length);
            vla_assembly_at += separator.length;
        }
        BUSTER_TEST(arguments, vla_assembly_at == vla_assembly_source_length);
        CPreprocessResult vla_assembly_tokens = c_preprocess(
            vla_assembly_arena, (String8){.pointer = vla_assembly_source_pointer, .length = vla_assembly_source_length}, (CPreprocessOptions){0});
        CParseResult vla_assembly_parse = c_parse(vla_assembly_arena, vla_assembly_tokens);
        CIRLowerResult vla_assembly_lowered =
            c_lower_to_ir(vla_assembly_arena, S8("vla-assembly-stress.c"), vla_assembly_tokens, vla_assembly_parse, target_native);
        BUSTER_TEST(arguments, vla_assembly_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, vla_assembly_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, vla_assembly_lowered.diagnostic_count == 0);
        if (vla_assembly_lowered.program)
        {
            IrModule* vla_assembly_module = vla_assembly_lowered.program->modules;
            IrFunction* vla_assembly_function = vla_assembly_module->functions;
            u32 vla_assembly_instruction_count = 0;
            u32 branch_instruction_count = 0;
            for (u32 instruction_index = 0; instruction_index < vla_assembly_function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = vla_assembly_function->instructions[instruction_index].opcode;
                vla_assembly_instruction_count += opcode == IR_OPCODE_INLINE_ASSEMBLY;
                branch_instruction_count += opcode == IR_OPCODE_BRANCH || opcode == IR_OPCODE_BRANCH_IF;
            }
            BUSTER_TEST(arguments, vla_assembly_instruction_count == 1);
            BUSTER_TEST(arguments, branch_instruction_count >= C_IR_VLA_ASSEMBLY_STRESS_DEPTH * 2);
            BUSTER_TEST(arguments,
                        ir_validate_canonical_module(vla_assembly_lowered.program, vla_assembly_module).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(vla_assembly_arena, 1));
    }
    {
        TemporalArena subscript_temporary = scratch_begin(0, 0);
        String8 subscript_prefix = S8("int main(void) { int values[1] = { 0 }; return values[");
        String8 subscript_nested = S8("values[");
        String8 subscript_suffix = S8("] = 1; }");
        u64 subscript_source_length = subscript_prefix.length + (u64)(C_IR_SUBSCRIPT_STRESS_DEPTH - 1) * subscript_nested.length + 1 +
                                      (u64)C_IR_SUBSCRIPT_STRESS_DEPTH + subscript_suffix.length - 1;
        char8* subscript_source_pointer = arena_allocate(subscript_temporary.arena, char8, subscript_source_length);
        u64 subscript_at = 0;
        memcpy(subscript_source_pointer + subscript_at, subscript_prefix.pointer, subscript_prefix.length);
        subscript_at += subscript_prefix.length;
        for (u32 depth = 1; depth < C_IR_SUBSCRIPT_STRESS_DEPTH; depth += 1)
        {
            memcpy(subscript_source_pointer + subscript_at, subscript_nested.pointer, subscript_nested.length);
            subscript_at += subscript_nested.length;
        }
        subscript_source_pointer[subscript_at++] = '0';
        for (u32 depth = 0; depth < C_IR_SUBSCRIPT_STRESS_DEPTH; depth += 1)
        {
            subscript_source_pointer[subscript_at++] = ']';
        }
        memcpy(subscript_source_pointer + subscript_at, subscript_suffix.pointer + 1, subscript_suffix.length - 1);
        subscript_at += subscript_suffix.length - 1;
        BUSTER_TEST(arguments, subscript_at == subscript_source_length);
        CPreprocessResult subscript_tokens = c_preprocess(
            subscript_temporary.arena, (String8){.pointer = subscript_source_pointer, .length = subscript_source_length}, (CPreprocessOptions){0});
        CParseResult subscript_parse = c_parse(subscript_temporary.arena, subscript_tokens);
        CIRLowerResult subscript_lowered =
            c_lower_to_ir(subscript_temporary.arena, S8("subscript-stress.c"), subscript_tokens, subscript_parse, target_native);
        BUSTER_TEST(arguments, subscript_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, subscript_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, subscript_lowered.diagnostic_count == 0);
        if (subscript_lowered.program)
        {
            BUSTER_TEST(arguments,
                        ir_validate_canonical_module(subscript_lowered.program, subscript_lowered.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(subscript_temporary);
    }
    {
        TemporalArena names_temporary = scratch_begin(0, 0);
        u64 names_source_capacity = (u64)C_IR_FUNCTION_NAME_STRESS_COUNT * 40 + 1024;
        char8* names_source_pointer = arena_allocate(names_temporary.arena, char8, names_source_capacity);
        u64 names_at = 0;
        for (u32 function_index = 0; function_index < C_IR_FUNCTION_NAME_STRESS_COUNT; function_index += 1)
        {
            String8 names_declaration = string_format(names_temporary.arena, S8("int function_{u32}(void);\n"), function_index);
            BUSTER_TEST(arguments, names_declaration.length <= names_source_capacity - names_at);
            memcpy(names_source_pointer + names_at, names_declaration.pointer, names_declaration.length);
            names_at += names_declaration.length;
        }
        String8 names_suffix = S8("int select_large(int value) __asm__(\"select_large_signed\");\n"
                                  "int select_large(unsigned value) __attribute__((__overloadable__)) __asm__(\"select_large_unsigned\");\n"
                                  "int main(void) { int *first; int *second; return select_large(1) + select_large(1U) + (first == second); }\n");
        BUSTER_TEST(arguments, names_suffix.length <= names_source_capacity - names_at);
        memcpy(names_source_pointer + names_at, names_suffix.pointer, names_suffix.length);
        names_at += names_suffix.length;
        CPreprocessResult names_tokens =
            c_preprocess(names_temporary.arena, (String8){.pointer = names_source_pointer, .length = names_at}, (CPreprocessOptions){0});
        CParseResult names_parse = c_parse(names_temporary.arena, names_tokens);
        CIRLowerResult names_lowered =
            c_lower_to_ir(names_temporary.arena, S8("function-name-stress.c"), names_tokens, names_parse, target_native);
        BUSTER_TEST(arguments, names_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, names_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, names_lowered.diagnostic_count == 0);
        if (names_lowered.program)
        {
            bool found_signed_call = false;
            bool found_unsigned_call = false;
            for (u32 type_index = 0; type_index < names_lowered.program->types.count; type_index += 1)
            {
                IrType* type = names_lowered.program->types.types + type_index;
                if (type->kind != IR_TYPE_POINTER || type->is_atomic || type->is_nullptr)
                {
                    continue;
                }
                for (u32 previous = 0; previous < type_index; previous += 1)
                {
                    IrType* earlier = names_lowered.program->types.types + previous;
                    BUSTER_TEST(arguments, earlier->kind != IR_TYPE_POINTER || earlier->is_atomic || earlier->is_nullptr ||
                                               earlier->element_type.value != type->element_type.value);
                }
            }
            IrModule* names_module = names_lowered.program->modules;
            for (u32 function_index = 0; function_index < names_module->function_count; function_index += 1)
            {
                IrFunction* names_function = names_module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < names_function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = names_function->instructions + instruction_index;
                    if (instruction->opcode != IR_OPCODE_CALL)
                    {
                        continue;
                    }
                    IrSymbol* symbol = ir_symbol_from_id(&names_lowered.program->symbols, instruction->symbol);
                    found_signed_call |= symbol && string_equal(symbol->link_name, S8("select_large_signed"));
                    found_unsigned_call |= symbol && string_equal(symbol->link_name, S8("select_large_unsigned"));
                }
            }
            BUSTER_TEST(arguments, found_signed_call);
            BUSTER_TEST(arguments, found_unsigned_call);
            BUSTER_TEST(arguments, ir_validate_canonical_module(names_lowered.program, names_module).error == IR_VALIDATION_NONE);
        }
        scratch_end(names_temporary);
    }
    enum
    {
        C_TYPE_MIXED_MACHINE_STRESS_DEPTH = 32,
        C_IR_MIXED_MACHINE_STRESS_DEPTH = 32,
    };
    {
        Arena* mixed_type_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
        String8 mixed_type_open = S8("_Alignas(8) int value; struct {");
        String8 mixed_type_close = S8("} member;");
        String8 mixed_type_suffix =
            S8("}; struct Root root; int mixed_type_main(void) { return atomic0 + inferred0 + callback0(0); }");
        u32 mixed_type_part_count = C_TYPE_MIXED_MACHINE_STRESS_DEPTH * 3 + 3;
        String8* mixed_type_parts = arena_allocate(mixed_type_arena, String8, mixed_type_part_count);
        u32 mixed_type_part_index = 0;
        for (u32 depth = 0; depth < C_TYPE_MIXED_MACHINE_STRESS_DEPTH; depth += 1)
        {
            mixed_type_parts[mixed_type_part_index++] =
                string_format(mixed_type_arena,
                              S8("__typeof__(sizeof(int)) inferred{u32}; _Atomic(int) atomic{u32};"
                                 " int (*callback{u32})(__typeof__(sizeof(int)) argument);"),
                              depth, depth, depth);
        }
        mixed_type_parts[mixed_type_part_index++] = S8("struct Root {");
        for (u32 depth = 0; depth < C_TYPE_MIXED_MACHINE_STRESS_DEPTH; depth += 1)
        {
            mixed_type_parts[mixed_type_part_index++] = mixed_type_open;
        }
        mixed_type_parts[mixed_type_part_index++] = S8("_Alignas(8) int value;");
        for (u32 depth = 0; depth < C_TYPE_MIXED_MACHINE_STRESS_DEPTH; depth += 1)
        {
            mixed_type_parts[mixed_type_part_index++] = mixed_type_close;
        }
        mixed_type_parts[mixed_type_part_index++] = mixed_type_suffix;
        BUSTER_TEST(arguments, mixed_type_part_index == mixed_type_part_count);
        String8 mixed_type_source =
            string_join_arena(mixed_type_arena, (SliceString8){.pointer = mixed_type_parts, .length = mixed_type_part_count}, false);
        CPreprocessResult mixed_type_tokens = c_preprocess(mixed_type_arena, mixed_type_source, (CPreprocessOptions){0});
        CParseResult mixed_type_parse = c_parse(mixed_type_arena, mixed_type_tokens);
        CIRLowerResult mixed_type_lowered =
            c_lower_to_ir(mixed_type_arena, S8("mixed-type-machine-stress.c"), mixed_type_tokens, mixed_type_parse, target_native);
        BUSTER_TEST(arguments, mixed_type_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, mixed_type_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, mixed_type_lowered.diagnostic_count == 0);
        BUSTER_TEST(arguments, mixed_type_parse.member_count >= C_TYPE_MIXED_MACHINE_STRESS_DEPTH);
        BUSTER_TEST(arguments, mixed_type_parse.parameter_count >= C_TYPE_MIXED_MACHINE_STRESS_DEPTH);
        BUSTER_TEST(arguments, mixed_type_parse.alignment_count >= C_TYPE_MIXED_MACHINE_STRESS_DEPTH);
        if (mixed_type_lowered.program)
        {
            BUSTER_TEST(arguments,
                        ir_validate_canonical_module(mixed_type_lowered.program, mixed_type_lowered.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(mixed_type_arena, 1));
    }
    {
        Arena* mixed_ir_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
        u32 mixed_ir_part_count = C_IR_MIXED_MACHINE_STRESS_DEPTH * 2 + 3;
        String8* mixed_ir_parts = arena_allocate(mixed_ir_arena, String8, mixed_ir_part_count);
        u32 mixed_ir_part_index = 0;
        mixed_ir_parts[mixed_ir_part_index++] =
            S8("static int global_values[2];"
               " static int *pointer_source(void) { return global_values; }"
               " static int identity(int value) { return value; }"
               " int mixed_ir_main(int count) {"
               " _Atomic(int) atomic_value = 0;"
               " int values[2] = { 0, 0 };"
               " __asm__ volatile (\"\" : : \"r\"(");
        for (u32 depth = 0; depth < C_IR_MIXED_MACHINE_STRESS_DEPTH; depth += 1)
        {
            mixed_ir_parts[mixed_ir_part_index++] = S8("identity(({");
        }
        mixed_ir_parts[mixed_ir_part_index++] = S8("count");
        for (u32 depth = 0; depth < C_IR_MIXED_MACHINE_STRESS_DEPTH; depth += 1)
        {
            u32 local_index = C_IR_MIXED_MACHINE_STRESS_DEPTH - depth - 1;
            mixed_ir_parts[mixed_ir_part_index++] =
                string_format(mixed_ir_arena,
                              S8("; int local{u32} = (int){{1}}; int vla{u32}[local{u32} ? local{u32} : 1];"
                                 " values[local{u32} ? 0 : 1] = (atomic_value += local{u32});"
                                 " local{u32} + (int)sizeof(vla{u32}); }}))"),
                              local_index, local_index, local_index, local_index, local_index, local_index, local_index, local_index);
        }
        mixed_ir_parts[mixed_ir_part_index++] =
            S8(")); (global_values[0] = 1, global_values[1] = 2); *pointer_source() = 3; return atomic_value; }");
        BUSTER_TEST(arguments, mixed_ir_part_index == mixed_ir_part_count);
        String8 mixed_ir_source =
            string_join_arena(mixed_ir_arena, (SliceString8){.pointer = mixed_ir_parts, .length = mixed_ir_part_count}, false);
        CPreprocessResult mixed_ir_tokens = c_preprocess(mixed_ir_arena, mixed_ir_source, (CPreprocessOptions){0});
        CParseResult mixed_ir_parse = c_parse(mixed_ir_arena, mixed_ir_tokens);
        CIRLowerResult mixed_ir_lowered =
            c_lower_to_ir(mixed_ir_arena, S8("mixed-ir-machine-stress.c"), mixed_ir_tokens, mixed_ir_parse, target_native);
        BUSTER_TEST(arguments, mixed_ir_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, mixed_ir_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, mixed_ir_lowered.diagnostic_count == 0);
        if (mixed_ir_lowered.program)
        {
            IrModule* mixed_ir_module = mixed_ir_lowered.program->modules;
            BUSTER_TEST(arguments, mixed_ir_module->function_count == 3);
            if (mixed_ir_module->function_count == 3)
            {
                IrFunction* mixed_ir_function = mixed_ir_module->functions + 2;
                u32 call_count = 0;
                u32 inline_assembly_count = 0;
                u32 atomic_rmw_count = 0;
                u32 branch_if_count = 0;
                u32 local_count = 0;
                u32 store_count = 0;
                u32 return_count = 0;
                u32 first_inline_assembly = UINT32_MAX;
                u32 first_atomic_rmw = UINT32_MAX;
                u32 first_call = UINT32_MAX;
                for (u32 instruction_index = 0; instruction_index < mixed_ir_function->instruction_count; instruction_index += 1)
                {
                    IrOpcode opcode = mixed_ir_function->instructions[instruction_index].opcode;
                    call_count += opcode == IR_OPCODE_CALL;
                    inline_assembly_count += opcode == IR_OPCODE_INLINE_ASSEMBLY;
                    atomic_rmw_count += opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE;
                    branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
                    local_count += opcode == IR_OPCODE_LOCAL;
                    store_count += opcode == IR_OPCODE_STORE;
                    return_count += opcode == IR_OPCODE_RETURN;
                    if (opcode == IR_OPCODE_INLINE_ASSEMBLY && first_inline_assembly == UINT32_MAX)
                    {
                        first_inline_assembly = instruction_index;
                    }
                    if (opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE && first_atomic_rmw == UINT32_MAX)
                    {
                        first_atomic_rmw = instruction_index;
                    }
                    if (opcode == IR_OPCODE_CALL && first_call == UINT32_MAX)
                    {
                        first_call = instruction_index;
                    }
                }
                BUSTER_TEST(arguments, call_count == C_IR_MIXED_MACHINE_STRESS_DEPTH + 1);
                BUSTER_TEST(arguments, inline_assembly_count == 1);
                BUSTER_TEST(arguments, atomic_rmw_count == C_IR_MIXED_MACHINE_STRESS_DEPTH);
                BUSTER_TEST(arguments, branch_if_count >= C_IR_MIXED_MACHINE_STRESS_DEPTH * 2);
                BUSTER_TEST(arguments, local_count >= C_IR_MIXED_MACHINE_STRESS_DEPTH * 2);
                BUSTER_TEST(arguments, store_count >= C_IR_MIXED_MACHINE_STRESS_DEPTH * 3);
                BUSTER_TEST(arguments, return_count == 1);
                BUSTER_TEST(arguments, mixed_ir_function->block_count >= C_IR_MIXED_MACHINE_STRESS_DEPTH * 6);
                BUSTER_TEST(arguments, first_atomic_rmw < first_call);
                BUSTER_TEST(arguments, first_call < first_inline_assembly);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(mixed_ir_lowered.program, mixed_ir_module).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, arena_destroy(mixed_ir_arena, 1));
    }
    {
        TemporalArena call_reachability_temporary = scratch_begin(0, 0);
        CPreprocessResult call_lower_scalar_tokens = {0};
        CParseResult call_lower_scalar_parse = {0};
        CIRLowerResult call_lower_scalar_ir = c_test_lower_source(
            call_reachability_temporary.arena,
            S8("static int scalar_helper(int value) { return value + 1; }\n"
               "static int scalar_caller(int value) { return scalar_helper(value); }\n"
               "int main(void) { return scalar_caller(0); }\n"),
            S8("static-call-scalar.c"), target_native, &call_lower_scalar_tokens, &call_lower_scalar_parse);
        BUSTER_TEST(arguments, call_lower_scalar_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, call_lower_scalar_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, call_lower_scalar_ir.diagnostic_count == 0);
        if (call_lower_scalar_ir.program)
        {
            IrModule* module = call_lower_scalar_ir.program->modules;
            IrFunction* helper = c_test_find_ir_function(module, S8("scalar_helper"));
            IrFunction* caller = c_test_find_ir_function(module, S8("scalar_caller"));
            IrFunction* main_function = c_test_find_ir_function(module, S8("main"));
            BUSTER_TEST(arguments, helper && caller && main_function);
            BUSTER_TEST(arguments, helper && helper->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, caller && caller->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, main_function && main_function->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, c_test_ir_direct_call_count(call_lower_scalar_ir.program, caller, S8("scalar_helper")) == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(call_lower_scalar_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(call_reachability_temporary);
    }
    {
        TemporalArena aggregate_call_temporary = scratch_begin(0, 0);
        CPreprocessResult aggregate_call_tokens = {0};
        CParseResult aggregate_call_parse = {0};
        String8 aggregate_call_source = S8("typedef struct AggregatePair { int left; int right; } AggregatePair;\n"
                                            "static AggregatePair aggregate_helper(AggregatePair left, AggregatePair right) {\n"
                                            "    AggregatePair result = { left.left + right.left, left.right + right.right };\n"
                                            "    return result;\n"
                                            "}\n"
                                            "static AggregatePair aggregate_caller(AggregatePair value) {\n"
                                            "    AggregatePair other = { 3, 4 };\n"
                                            "    return aggregate_helper(value, other);\n"
                                            "}\n"
                                            "int main(void) {\n"
                                            "    AggregatePair value = { 1, 2 };\n"
                                            "    AggregatePair result = aggregate_caller(value);\n"
                                            "    return result.left == 4 && result.right == 6 ? 0 : 1;\n"
                                            "}\n");
        CIRLowerResult aggregate_call_ir = c_test_lower_source(
            aggregate_call_temporary.arena,
            aggregate_call_source,
            S8("static-call-aggregate.c"), target_native, &aggregate_call_tokens, &aggregate_call_parse);
        BUSTER_TEST(arguments, aggregate_call_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, aggregate_call_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, aggregate_call_ir.diagnostic_count == 0);
        if (aggregate_call_ir.program)
        {
            IrModule* module = aggregate_call_ir.program->modules;
            IrFunction* helper = c_test_find_ir_function(module, S8("aggregate_helper"));
            IrFunction* caller = c_test_find_ir_function(module, S8("aggregate_caller"));
            BUSTER_TEST(arguments, helper && caller);
            BUSTER_TEST(arguments, helper && helper->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, caller && caller->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, c_test_ir_direct_call_count(aggregate_call_ir.program, caller, S8("aggregate_helper")) == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(aggregate_call_ir.program, module).error == IR_VALIDATION_NONE);
        }
        Target aggregate_call_targets[] = {
            target_native,
            {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS},
            {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
            {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS},
        };
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(aggregate_call_targets); target_index += 1)
        {
            TemporalArena cross_target_temporary = scratch_begin(0, 0);
            CPreprocessResult cross_target_tokens = {0};
            CParseResult cross_target_parse = {0};
            CIRLowerResult cross_target_ir = c_test_lower_source(cross_target_temporary.arena, aggregate_call_source,
                                                                  S8("static-call-aggregate-cross-target.c"), aggregate_call_targets[target_index],
                                                                  &cross_target_tokens, &cross_target_parse);
            BUSTER_TEST(arguments, cross_target_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, cross_target_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, cross_target_ir.diagnostic_count == 0);
            if (cross_target_ir.program)
            {
                IrModule* module = cross_target_ir.program->modules;
                IrFunction* helper = c_test_find_ir_function(module, S8("aggregate_helper"));
                IrFunction* caller = c_test_find_ir_function(module, S8("aggregate_caller"));
                BUSTER_TEST(arguments, helper && caller);
                BUSTER_TEST(arguments, helper && helper->state == IR_FUNCTION_LOWERED);
                BUSTER_TEST(arguments, caller && caller->state == IR_FUNCTION_LOWERED);
                BUSTER_TEST(arguments, c_test_ir_direct_call_count(cross_target_ir.program, caller, S8("aggregate_helper")) == 1);
                BUSTER_TEST(arguments, ir_validate_canonical_module(cross_target_ir.program, module).error == IR_VALIDATION_NONE);
            }
            scratch_end(cross_target_temporary);
        }
        scratch_end(aggregate_call_temporary);
    }
    {
        TemporalArena prototype_call_temporary = scratch_begin(0, 0);
        CPreprocessResult prototype_call_tokens = {0};
        CParseResult prototype_call_parse = {0};
        CIRLowerResult prototype_call_ir = c_test_lower_source(
            prototype_call_temporary.arena,
            S8("static int prototyped_helper(int value);\n"
               "static int prototyped_helper(int value) { return value + 1; }\n"
               "static int prototyped_caller(int value) { return prototyped_helper(value); }\n"
               "int main(void) { return prototyped_caller(0); }\n"),
            S8("static-call-prototype.c"), target_native, &prototype_call_tokens, &prototype_call_parse);
        BUSTER_TEST(arguments, prototype_call_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, prototype_call_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, prototype_call_ir.diagnostic_count == 0);
        if (prototype_call_ir.program)
        {
            IrModule* module = prototype_call_ir.program->modules;
            IrFunction* helper = c_test_find_ir_function(module, S8("prototyped_helper"));
            IrFunction* caller = c_test_find_ir_function(module, S8("prototyped_caller"));
            BUSTER_TEST(arguments, helper && caller);
            BUSTER_TEST(arguments, helper && helper->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, caller && caller->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, c_test_ir_direct_call_count(prototype_call_ir.program, caller, S8("prototyped_helper")) == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(prototype_call_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(prototype_call_temporary);
    }
    {
        TemporalArena dead_call_temporary = scratch_begin(0, 0);
        CPreprocessResult dead_call_tokens = {0};
        CParseResult dead_call_parse = {0};
        CIRLowerResult dead_call_ir = c_test_lower_source(
            dead_call_temporary.arena,
            S8("static int dead_caller(int value);\n"
               "static int dead_helper(int value) { return value + 1; }\n"
               "static int dead_caller(int value) { return dead_helper(value); }\n"
               "int main(void) { return 0; }\n"),
            S8("static-call-dead.c"), target_native, &dead_call_tokens, &dead_call_parse);
        BUSTER_TEST(arguments, dead_call_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, dead_call_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, dead_call_ir.diagnostic_count == 0);
        if (dead_call_ir.program)
        {
            IrModule* module = dead_call_ir.program->modules;
            IrFunction* caller = c_test_find_ir_function(module, S8("dead_caller"));
            IrFunction* helper = c_test_find_ir_function(module, S8("dead_helper"));
            BUSTER_TEST(arguments, caller != 0);
            BUSTER_TEST(arguments, caller && caller->state != IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, helper == 0);
            BUSTER_TEST(arguments, module->lowered_function_count == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(dead_call_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(dead_call_temporary);
    }
    {
        TemporalArena vulkan_call_temporary = scratch_begin(0, 0);
        String8 vulkan_source = S8("typedef unsigned char u8;\n"
                                    "typedef unsigned int u32;\n"
                                    "typedef unsigned long long u64;\n"
                                    "typedef _Bool bool;\n"
                                    "typedef struct String8 String8;\n"
                                    "struct String8 { char *pointer; u64 length; };\n"
                                    "typedef struct QueueFamilySelection QueueFamilySelection;\n"
                                    "struct QueueFamilySelection {\n"
                                    "    u32 graphics_family_index;\n"
                                    "    u32 present_family_index;\n"
                                    "    bool eligible;\n"
                                    "    u8 reserved[3];\n"
                                    "};\n"
                                    "typedef struct Candidate Candidate;\n"
                                    "struct Candidate {\n"
                                    "    String8 name;\n"
                                    "    u32 vendor_id;\n"
                                    "    u32 device_id;\n"
                                    "    u32 enumeration_index;\n"
                                    "    u32 device_type;\n"
                                    "    QueueFamilySelection queues;\n"
                                    "    bool has_required_extension;\n"
                                    "    bool has_required_features;\n"
                                    "    bool has_surface_support;\n"
                                    "    bool excluded;\n"
                                    "    u8 reserved[4];\n"
                                    "};\n"
                                    "typedef struct CandidateSlice CandidateSlice;\n"
                                    "struct CandidateSlice { Candidate *pointer; u64 length; };\n"
                                    "typedef struct Selection Selection;\n"
                                    "struct Selection { u32 candidate_index; u64 score; bool found; u8 reserved[3]; };\n"
                                    "static Selection vulkan_select_device(CandidateSlice candidates);\n"
                                    "static int vulkan_device_name_compare(String8 left, String8 right)\n"
                                    "{\n"
                                    "    if (left.length < right.length) return -1;\n"
                                    "    if (left.length > right.length) return 1;\n"
                                    "    return 0;\n"
                                    "}\n"
                                    "static bool vulkan_device_is_better(Candidate candidate, Candidate current, u64 candidate_score, u64 current_score)\n"
                                    "{\n"
                                    "    if (candidate_score != current_score) return candidate_score > current_score;\n"
                                    "    int name_comparison = vulkan_device_name_compare(candidate.name, current.name);\n"
                                    "    if (name_comparison != 0) return name_comparison < 0;\n"
                                    "    if (candidate.vendor_id != current.vendor_id) return candidate.vendor_id < current.vendor_id;\n"
                                    "    if (candidate.device_id != current.device_id) return candidate.device_id < current.device_id;\n"
                                    "    return candidate.enumeration_index < current.enumeration_index;\n"
                                    "}\n"
                                    "static Selection vulkan_select_device(CandidateSlice candidates)\n"
                                    "{\n"
                                    "    Selection result = { 0 };\n"
                                    "    for (u32 i = 0; i < candidates.length; i += 1)\n"
                                    "    {\n"
                                    "        Candidate candidate = candidates.pointer[i];\n"
                                    "        if (candidate.excluded) continue;\n"
                                    "        u64 score = candidate.vendor_id;\n"
                                    "        if (!result.found || vulkan_device_is_better(candidate, candidates.pointer[result.candidate_index], score, result.score))\n"
                                    "        {\n"
                                    "            result.candidate_index = i;\n"
                                    "            result.score = score;\n"
                                    "            result.found = 1;\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return result;\n"
                                    "}\n");
        CPreprocessResult vulkan_dead_tokens = {0};
        CParseResult vulkan_dead_parse = {0};
        CIRLowerResult vulkan_dead_ir = c_test_lower_source(vulkan_call_temporary.arena, vulkan_source, S8("vulkan-selection-dead.c"), target_native,
                                                             &vulkan_dead_tokens, &vulkan_dead_parse);
        BUSTER_TEST(arguments, vulkan_dead_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, vulkan_dead_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, vulkan_dead_ir.diagnostic_count == 0);
        if (vulkan_dead_ir.program)
        {
            IrModule* module = vulkan_dead_ir.program->modules;
            IrFunction* select = c_test_find_ir_function(module, S8("vulkan_select_device"));
            IrFunction* helper = c_test_find_ir_function(module, S8("vulkan_device_is_better"));
            BUSTER_TEST(arguments, select != 0);
            BUSTER_TEST(arguments, select && select->state != IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, helper == 0);
            BUSTER_TEST(arguments, module->lowered_function_count == 0);
            BUSTER_TEST(arguments, ir_validate_canonical_module(vulkan_dead_ir.program, module).error == IR_VALIDATION_NONE);
        }
        String8 vulkan_reachable_parts[] = {
            vulkan_source,
            S8("int main(void) { CandidateSlice values = { 0 }; return vulkan_select_device(values).found; }\n"),
        };
        String8 vulkan_reachable_source = string_join_arena(vulkan_call_temporary.arena,
                                                             (SliceString8)BUSTER_ARRAY_TO_SLICE(vulkan_reachable_parts), false);
        CPreprocessResult vulkan_reachable_tokens = {0};
        CParseResult vulkan_reachable_parse = {0};
        CIRLowerResult vulkan_reachable_ir = c_test_lower_source(vulkan_call_temporary.arena, vulkan_reachable_source,
                                                                  S8("vulkan-selection-reachable.c"), target_native, &vulkan_reachable_tokens,
                                                                  &vulkan_reachable_parse);
        BUSTER_TEST(arguments, vulkan_reachable_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, vulkan_reachable_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, vulkan_reachable_ir.diagnostic_count == 0);
        if (vulkan_reachable_ir.program)
        {
            IrModule* module = vulkan_reachable_ir.program->modules;
            IrFunction* select = c_test_find_ir_function(module, S8("vulkan_select_device"));
            IrFunction* helper = c_test_find_ir_function(module, S8("vulkan_device_is_better"));
            BUSTER_TEST(arguments, select && helper);
            BUSTER_TEST(arguments, select && select->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, helper && helper->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, c_test_ir_direct_call_count(vulkan_reachable_ir.program, select, S8("vulkan_device_is_better")) == 1);
            BUSTER_TEST(arguments, ir_validate_canonical_module(vulkan_reachable_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(vulkan_call_temporary);
        TemporalArena labels_temporary = scratch_begin(0, 0);
        String8 labels_source = S8("int labels_and_asm(int selector) {"
                                   " void *target = (void *)&&dispatch;"
                                   " if (selector) goto *target;"
                                   " return 3;"
                                   "dispatch: __asm__ goto (\"\" : : \"r\"(selector) : \"cc\" : done);"
                                   " return 5;"
                                   "done: return 7; }\n");
        CPreprocessResult labels_tokens = c_preprocess(labels_temporary.arena, labels_source,
                                                       (CPreprocessOptions){
                                                           .target = target_native,
                                                           .data_layout = target_data_layout(target_native),
                                                           .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                       });
        CParseResult labels_parse = c_parse(labels_temporary.arena, labels_tokens);
        CIRLowerResult labels_lowered = c_lower_to_ir(labels_temporary.arena, S8("labels-as-values.c"), labels_tokens, labels_parse, target_native);
        BUSTER_TEST(arguments, labels_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, labels_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, labels_lowered.diagnostic_count == 0);
        BUSTER_TEST(arguments, labels_lowered.program != 0);
        if (labels_lowered.program)
        {
            IrModule* labels_module = labels_lowered.program->modules;
            BUSTER_TEST(arguments, labels_module->function_count == 1);
            if (labels_module->function_count == 1)
            {
                IrFunction* labels_function = labels_module->functions;
                u32 label_address_count = 0;
                u32 indirect_branch_count = 0;
                u32 asm_goto_count = 0;
                IrInstruction* label_address = 0;
                IrInstruction* indirect_branch = 0;
                IrInstruction* asm_goto = 0;
                for (u32 instruction_index = 0; instruction_index < labels_function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = labels_function->instructions + instruction_index;
                    if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
                    {
                        label_address_count += 1;
                        label_address = instruction;
                    }
                    else if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                    {
                        indirect_branch_count += 1;
                        indirect_branch = instruction;
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && instruction->target_count)
                    {
                        asm_goto_count += 1;
                        asm_goto = instruction;
                    }
                }
                BUSTER_TEST(arguments, label_address_count == 1);
                BUSTER_TEST(arguments, indirect_branch_count == 1);
                BUSTER_TEST(arguments, asm_goto_count == 1);
                if (label_address && label_address->result.value < labels_function->value_count)
                {
                    IrValueLabelMetadata* label_value = ir_value_label_metadata_find(labels_function, label_address->result);
                    BUSTER_TEST(arguments, label_value && label_value->is_label_value);
                    BUSTER_TEST(arguments, label_value && ir_label_provenance_valid(label_value) && label_value->label_block_count == 1 &&
                                             label_address->target_count == 1 && label_value->label_blocks[0].value == label_address->targets[0].value);
                }
                if (indirect_branch && indirect_branch->operand_count == 1 && indirect_branch->target_count >= 1 &&
                    indirect_branch->operands[0].value < labels_function->value_count)
                {
                    IrValueLabelMetadata target_value = ir_value_label_metadata(labels_function, indirect_branch->operands[0]);
                    BUSTER_TEST(arguments, target_value.is_label_value);
                    bool target_in_successors = ir_label_provenance_valid(&target_value) &&
                                                indirect_branch->target_count == target_value.label_block_count;
                    for (u32 label_index = 0; target_in_successors && label_index < target_value.label_block_count; label_index += 1)
                    {
                        bool found = false;
                        for (u32 target_index = 0; target_index < indirect_branch->target_count; target_index += 1)
                        {
                            found |= indirect_branch->targets[target_index].value == target_value.label_blocks[label_index].value;
                        }
                        target_in_successors &= found;
                    }
                    for (u32 target_index = 0; target_in_successors && target_index < indirect_branch->target_count; target_index += 1)
                    {
                        bool found = false;
                        for (u32 label_index = 0; label_index < target_value.label_block_count; label_index += 1)
                        {
                            found |= indirect_branch->targets[target_index].value == target_value.label_blocks[label_index].value;
                        }
                        target_in_successors &= found;
                    }
                    BUSTER_TEST(arguments, target_in_successors);
                }
                if (asm_goto)
                {
                    BUSTER_TEST(arguments, asm_goto->target_count == 2);
                    BUSTER_TEST(arguments, ir_instruction_extra(labels_function, ir_instruction_self_id(labels_function, asm_goto)).literal.length == 0);
                }
                BUSTER_TEST(arguments, ir_validate_canonical_module(labels_lowered.program, labels_module).error == IR_VALIDATION_NONE);
                if (indirect_branch)
                {
                    u32 saved_operand_count = indirect_branch->operand_count;
                    IrValueId* saved_operands = indirect_branch->operands;
                    indirect_branch->operands = 0;
                    IrValidationResult missing_operand = ir_validate_canonical_module(labels_lowered.program, labels_module);
                    BUSTER_TEST(arguments, missing_operand.error != IR_VALIDATION_NONE);
                    indirect_branch->operands = saved_operands;
                    indirect_branch->operand_count = 0;
                    IrValidationResult wrong_shape = ir_validate_canonical_module(labels_lowered.program, labels_module);
                    BUSTER_TEST(arguments, wrong_shape.error != IR_VALIDATION_NONE);
                    indirect_branch->operand_count = saved_operand_count;
                    IrValueId saved_operand = indirect_branch->operands[0];
                    indirect_branch->operands[0].value = labels_function->value_count + 1;
                    IrValidationResult invalid_operand = ir_validate_canonical_module(labels_lowered.program, labels_module);
                    BUSTER_TEST(arguments, invalid_operand.error != IR_VALIDATION_NONE);
                    indirect_branch->operands[0] = saved_operand;
                    IrBlockId saved_target = indirect_branch->targets[0];
                    indirect_branch->targets[0].value = labels_function->block_count + 1;
                    IrValidationResult invalid_target = ir_validate_canonical_module(labels_lowered.program, labels_module);
                    BUSTER_TEST(arguments, invalid_target.error != IR_VALIDATION_NONE);
                    indirect_branch->targets[0] = saved_target;
                    if (indirect_branch->target_count < UINT16_MAX)
                    {
                        u16 saved_target_count = indirect_branch->target_count;
                        IrBlockId* saved_targets = indirect_branch->targets;
                        IrBlockId* extra_targets = arena_allocate(labels_temporary.arena, IrBlockId, saved_target_count + 1);
                        memcpy(extra_targets, saved_targets, sizeof(IrBlockId) * saved_target_count);
                        extra_targets[saved_target_count] = saved_targets[0];
                        indirect_branch->targets = extra_targets;
                        indirect_branch->target_count += 1;
                        IrValidationResult extra_target = ir_validate_canonical_module(labels_lowered.program, labels_module);
                        BUSTER_TEST(arguments, extra_target.error != IR_VALIDATION_NONE);
                        indirect_branch->targets = saved_targets;
                        indirect_branch->target_count = saved_target_count;
                    }
                }
                if (label_address)
                {
                    IrTypeId saved_type = label_address->canonical_type;
                    IrTypeId non_void_pointer_type = IR_TYPE_ID_INVALID;
                    for (u32 value_index = 0; value_index < labels_function->value_count; value_index += 1)
                    {
                        IrType* candidate_type = ir_type_from_id(&labels_lowered.program->types, labels_function->values[value_index].canonical_type);
                        if (candidate_type && candidate_type->kind != IR_TYPE_POINTER)
                        {
                            non_void_pointer_type = labels_function->values[value_index].canonical_type;
                            break;
                        }
                    }
                    if (non_void_pointer_type.value != IR_ID_UNDERLYING_INVALID)
                    {
                        label_address->canonical_type = non_void_pointer_type;
                        IrValidationResult invalid_type = ir_validate_canonical_module(labels_lowered.program, labels_module);
                        BUSTER_TEST(arguments, invalid_type.error != IR_VALIDATION_NONE);
                        label_address->canonical_type = saved_type;
                    }
                    IrValueLabelMetadata* label_value =
                        label_address->result.value < labels_function->value_count ? ir_value_label_metadata_find(labels_function, label_address->result) : 0;
                    if (label_value && label_value->label_block_count == 1 && label_value->label_blocks && labels_function->block_count > 1)
                    {
                        IrBlockId alternate = IR_BLOCK_ID_INVALID;
                        for (u32 block_index = 0; block_index < labels_function->block_count; block_index += 1)
                        {
                            if (block_index != label_value->label_blocks[0].value)
                            {
                                alternate = (IrBlockId){.value = block_index};
                                break;
                            }
                        }
                        if (alternate.value != IR_ID_UNDERLYING_INVALID)
                        {
                            IrBlockId* saved_blocks = label_value->label_blocks;
                            label_value->label_blocks = arena_allocate(labels_temporary.arena, IrBlockId, 1);
                            label_value->label_blocks[0] = alternate;
                            IrValidationResult invalid_provenance = ir_validate_canonical_module(labels_lowered.program, labels_module);
                            BUSTER_TEST(arguments, invalid_provenance.error != IR_VALIDATION_NONE);
                            label_value->label_blocks = saved_blocks;
                        }
                    }
                    IrValueId saved_result = label_address->result;
                    label_address->result.value = labels_function->value_count + 1;
                    IrValidationResult invalid_result = ir_validate_canonical_module(labels_lowered.program, labels_module);
                    BUSTER_TEST(arguments, invalid_result.error != IR_VALIDATION_NONE);
                    label_address->result = saved_result;
                }
            }
        }
        scratch_end(labels_temporary);
    }
    {
        TemporalArena label_flow_temporary = scratch_begin(0, 0);
        String8 label_flow_source = S8("int conditional_labels(int selector) {"
                                       " goto *(selector ? &&one : &&zero);"
                                       "zero: return 13;"
                                       "one: return 17;"
                                       "}"
                                       "int table_labels(int selector) {"
                                       " void *targets[2] = { &&zero, &&one };"
                                       " goto *targets[selector & 1];"
                                       "zero: return 19;"
                                       "one: return 23;"
                                       "}"
                                       "int copied_table_labels(int selector) {"
                                       " void *source[2] = { &&zero, &&one };"
                                       " void *targets[2] = { source[0], source[1] };"
                                       " goto *targets[selector & 1];"
                                       "zero: return 29;"
                                       "one: return 31;"
                                       "}"
                                       "int overwritten_table_labels(int selector) {"
                                       " void *targets[2];"
                                       " targets[0] = &&zero;"
                                       " targets[1] = &&one;"
                                       " targets[0] = 0;"
                                       " goto *targets[1];"
                                       "zero: return 37;"
                                       "one: return 41;"
                                       "}"
                                       "int typedef_labels(void) {"
                                       " typedef void *P;"
                                       " P target = (P)&&typed;"
                                       " goto *target;"
                                       "typed: return 43;"
                                       "}"
                                       "int read_write_asm_label(int selector) {"
                                       " int value = selector;"
                                       " __asm__ goto (\"jmp %l2\" : \"+r\"(value) : : : target);"
                                       " return 29;"
                                       "target: return value + 31;"
                                       "}"
                                       "int named_asm_operand(int value) {"
                                       " __asm__(\"%[named]\" : [named] \"+r\"(value));"
                                       " return value;"
                                       "}"
                                       "int outputs_only_asm(void) {"
                                       " int generic, fixed;"
                                       " __asm__(\"\" : \"=r\"(generic), \"=a\"(fixed));"
                                       " return generic + fixed;"
                                       "}"
                                       "int named_asm_label(int selector) {"
                                       " int value = selector;"
                                       " __asm__ goto (\"jmp %l[target]\" : \"+r\"(value) : : : target);"
                                       " return 37;"
                                       "target: return value + 41;"
                                       "}\n");
        CPreprocessResult label_flow_tokens = c_preprocess(label_flow_temporary.arena, label_flow_source,
                                                           (CPreprocessOptions){
                                                               .target = target_native,
                                                               .data_layout = target_data_layout(target_native),
                                                               .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                           });
        CParseResult label_flow_parse = c_parse(label_flow_temporary.arena, label_flow_tokens);
        CIRLowerResult label_flow_lowered = c_lower_to_ir(label_flow_temporary.arena, S8("label-flow.c"), label_flow_tokens, label_flow_parse, target_native);
        BUSTER_TEST(arguments, label_flow_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_flow_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_flow_lowered.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_flow_lowered.program != 0);
        if (label_flow_lowered.program)
        {
            IrModule* module = label_flow_lowered.program->modules;
            u32 indirect_count = 0;
            u32 inline_goto_count = 0;
            u32 set_valued_indirect_count = 0;
            u32 conditional_set_count = 0;
            u32 table_set_count = 0;
            u32 copied_table_set_count = 0;
            u32 overwritten_table_set_count = 0;
            u32 typedef_indirect_count = 0;
            IrInstruction* numeric_asm = 0;
            IrInstruction* named_asm = 0;
            IrInstruction* named_operand_asm = 0;
            IrInstruction* outputs_only_asm = 0;
            IrFunction* numeric_asm_function = 0;
            IrFunction* named_asm_function = 0;
            IrInstructionExtra numeric_asm_extra = {0};
            IrInstructionExtra named_asm_extra = {0};
            IrInstructionExtra named_operand_asm_extra = {0};
            IrInstruction* set_valued_indirect = 0;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                    {
                        indirect_count += 1;
                        if (instruction->operand_count == 1 && instruction->operands[0].value < function->value_count)
                        {
                            IrValueLabelMetadata target = ir_value_label_metadata(function, instruction->operands[0]);
                            set_valued_indirect_count += target.label_block_count == 2 && instruction->target_count == 2;
                            if (target.label_block_count == 2 && instruction->target_count == 2)
                            {
                                set_valued_indirect = instruction;
                                conditional_set_count += string_equal(function->name, S8("conditional_labels"));
                                table_set_count += string_equal(function->name, S8("table_labels"));
                                copied_table_set_count += string_equal(function->name, S8("copied_table_labels"));
                                overwritten_table_set_count += string_equal(function->name, S8("overwritten_table_labels"));
                            }
                            if (target.label_block_count == 1 && instruction->target_count == 1)
                            {
                                typedef_indirect_count += string_equal(function->name, S8("typedef_labels"));
                            }
                            for (u32 label_index = 0; label_index < target.label_block_count; label_index += 1)
                            {
                                bool found = false;
                                for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
                                {
                                    found |= instruction->targets[target_index].value == target.label_blocks[label_index].value;
                                }
                                BUSTER_TEST(arguments, found);
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && instruction->target_count)
                    {
                        inline_goto_count += 1;
                        IrInstructionExtra instruction_extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
                        if (instruction_extra.literal.length && instruction_extra.literal.pointer[instruction_extra.literal.length - 1] == '2')
                        {
                            numeric_asm = instruction;
                            numeric_asm_function = function;
                            numeric_asm_extra = instruction_extra;
                        }
                        else
                        {
                            named_asm = instruction;
                            named_asm_function = function;
                            named_asm_extra = instruction_extra;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && string_equal(function->name, S8("named_asm_operand")))
                    {
                        named_operand_asm = instruction;
                        named_operand_asm_extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && string_equal(function->name, S8("outputs_only_asm")))
                    {
                        outputs_only_asm = instruction;
                    }
                }
            }
            BUSTER_TEST(arguments, indirect_count == 5);
            BUSTER_TEST(arguments, set_valued_indirect_count == 3);
            BUSTER_TEST(arguments, conditional_set_count == 1);
            BUSTER_TEST(arguments, table_set_count == 1);
            BUSTER_TEST(arguments, copied_table_set_count == 1);
            BUSTER_TEST(arguments, overwritten_table_set_count == 0);
            BUSTER_TEST(arguments, typedef_indirect_count == 1);
            BUSTER_TEST(arguments, inline_goto_count == 2);
            BUSTER_TEST(arguments, numeric_asm && numeric_asm_extra.label_name_count == 1);
            BUSTER_TEST(arguments, named_asm && named_asm_extra.label_name_count == 1);
            BUSTER_TEST(arguments, named_operand_asm && named_operand_asm_extra.operand_name_count == 1);
            BUSTER_TEST(arguments, outputs_only_asm && outputs_only_asm->operand_count == 2 && outputs_only_asm->target_count == 0);
            if (named_operand_asm)
            {
                BUSTER_STRING_TEST(arguments, named_operand_asm_extra.operand_names[0], S8("named"));
                BUSTER_STRING_TEST(arguments, named_operand_asm_extra.literal, S8("%0"));
            }
            if (numeric_asm)
            {
                BUSTER_TEST(arguments, ir_inline_assembly_label_operand_base(numeric_asm) == 2);
                u32 target_index = 0;
                BUSTER_TEST(arguments, ir_inline_assembly_jump_target(numeric_asm_function, numeric_asm, numeric_asm_extra.literal, S8("jmp %l"), &target_index));
                BUSTER_TEST(arguments, target_index == 1);
                BUSTER_TEST(arguments, !ir_inline_assembly_jump_target(numeric_asm_function, numeric_asm, S8("jmp %l1"), S8("jmp %l"), &target_index));
            }
            if (named_asm)
            {
                u32 target_index = 0;
                BUSTER_TEST(arguments, ir_inline_assembly_jump_target(named_asm_function, named_asm, named_asm_extra.literal, S8("jmp %l"), &target_index));
                BUSTER_TEST(arguments, target_index == 1);
                BUSTER_STRING_TEST(arguments, named_asm_extra.label_names[0], S8("target"));
            }
            IrValidationResult label_flow_validation = ir_validate_canonical_module(label_flow_lowered.program, module);
            BUSTER_TEST(arguments, label_flow_validation.error == IR_VALIDATION_NONE);
            IrLabelProvenancePath* mutable_path = 0;
            for (u32 function_index = 0; function_index < module->function_count && !mutable_path; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 entry_index = 0; entry_index < function->label_metadata_count && !mutable_path; entry_index += 1)
                {
                    IrValueLabelMetadata* value = function->label_metadata + entry_index;
                    if (value->label_path_count && value->label_paths && value->label_paths[0].size)
                    {
                        mutable_path = value->label_paths;
                    }
                }
            }
            BUSTER_TEST(arguments, mutable_path != 0);
            if (mutable_path)
            {
                u64 saved_offset = mutable_path->offset;
                u64 saved_size = mutable_path->size;
                mutable_path->offset = UINT64_MAX - saved_size + 1;
                IrValidationResult invalid_path = ir_validate_canonical_module(label_flow_lowered.program, module);
                BUSTER_TEST(arguments, invalid_path.error != IR_VALIDATION_NONE);
                mutable_path->offset = saved_offset;
                mutable_path->size = saved_size;
            }
            if (set_valued_indirect)
            {
                IrFunction* set_function = 0;
                for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
                {
                    IrFunction* candidate = module->functions + function_index;
                    for (u32 instruction_index = 0; instruction_index < candidate->instruction_count; instruction_index += 1)
                    {
                        if (candidate->instructions + instruction_index == set_valued_indirect)
                        {
                            set_function = candidate;
                            break;
                        }
                    }
                    if (set_function)
                    {
                        break;
                    }
                }
                BUSTER_TEST(arguments, set_function != 0);
                if (set_function)
                {
                    IrBlockId* saved_targets = set_valued_indirect->targets;
                    u16 saved_target_count = set_valued_indirect->target_count;
                    set_valued_indirect->target_count = 1;
                    IrValidationResult missing_successor = ir_validate_canonical_module(label_flow_lowered.program, module);
                    BUSTER_TEST(arguments, missing_successor.error != IR_VALIDATION_NONE);
                    set_valued_indirect->targets = arena_allocate(label_flow_temporary.arena, IrBlockId, saved_target_count + 1);
                    memcpy(set_valued_indirect->targets, saved_targets, sizeof(IrBlockId) * saved_target_count);
                    IrBlockId extra_target = IR_BLOCK_ID_INVALID;
                    IrValue* set_value = set_valued_indirect->operands[0].value < set_function->value_count
                                             ? set_function->values + set_valued_indirect->operands[0].value
                                             : 0;
                    for (u32 block_index = 0; set_value && block_index < set_function->block_count; block_index += 1)
                    {
                        bool present = false;
                        for (u32 target_index = 0; target_index < saved_target_count; target_index += 1)
                        {
                            present |= saved_targets[target_index].value == block_index;
                        }
                        if (!present)
                        {
                            extra_target = (IrBlockId){.value = block_index};
                            break;
                        }
                    }
                    if (extra_target.value == IR_ID_UNDERLYING_INVALID)
                    {
                        extra_target = saved_targets[0];
                    }
                    set_valued_indirect->targets[saved_target_count] = extra_target;
                    set_valued_indirect->target_count = (u16)(saved_target_count + 1);
                    IrValidationResult extra_successor = ir_validate_canonical_module(label_flow_lowered.program, module);
                    BUSTER_TEST(arguments, extra_successor.error != IR_VALIDATION_NONE);
                    set_valued_indirect->targets = saved_targets;
                    set_valued_indirect->target_count = saved_target_count;
                }
            }
        }
        scratch_end(label_flow_temporary);
    }
    {
        TemporalArena dynamic_aggregate_temporary = scratch_begin(0, 0);
        CPreprocessResult dynamic_aggregate_tokens = c_preprocess(
            dynamic_aggregate_temporary.arena,
            S8("struct dynamic_label_pair { void *target; };"
               "int dynamic_aggregate_index(int index) {"
               " struct dynamic_label_pair table[2] = { { &&dynamic_zero }, { &&dynamic_one } };"
               " goto *table[index].target;"
               "dynamic_zero: return 13;"
               "dynamic_one: return 17;"
               "}\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult dynamic_aggregate_parse = c_parse(dynamic_aggregate_temporary.arena, dynamic_aggregate_tokens);
        CIRLowerResult dynamic_aggregate_lowered = c_lower_to_ir(dynamic_aggregate_temporary.arena, S8("dynamic-aggregate-label.c"),
                                                                  dynamic_aggregate_tokens, dynamic_aggregate_parse, target_native);
        BUSTER_TEST(arguments, dynamic_aggregate_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, dynamic_aggregate_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, dynamic_aggregate_lowered.diagnostic_count == 1);
        if (dynamic_aggregate_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, dynamic_aggregate_lowered.diagnostics[0].message,
                               S8("in function 'dynamic_aggregate_index': dynamic indexing of label-containing aggregate elements is unsupported"));
        }
        scratch_end(dynamic_aggregate_temporary);
    }
    {
        IrBlockId scalar_a = {.value = 11};
        IrBlockId scalar_b = {.value = 13};
        IrBlockId scalar_blocks_a[] = {scalar_a};
        IrBlockId scalar_blocks_b[] = {scalar_b};
        IrBlockId scalar_blocks_union[] = {scalar_a, scalar_b};
        IrValue scalar_values[3] = {0};
        IrValueId scalar_metadata_values[3] = {{.value = 0}, {.value = 1}, {.value = 2}};
        IrValueLabelMetadata scalar_metadata[3] = {
            [0] = {.is_label_value = true, .label_blocks = scalar_blocks_a, .label_block_count = 1},
            [1] = {.is_label_value = true, .label_blocks = scalar_blocks_b, .label_block_count = 1},
            [2] = {.has_label_provenance = true, .label_blocks = scalar_blocks_union, .label_block_count = 2},
        };
        IrIncoming scalar_incoming_b = {.predecessor = {.value = 1}, .value = {.value = 1}};
        IrIncoming scalar_incoming_a = {.next = &scalar_incoming_b, .predecessor = {.value = 0}, .value = {.value = 0}};
        IrBlockParameter scalar_parameter = {
            .first_incoming = &scalar_incoming_a,
            .last_incoming = &scalar_incoming_b,
            .value = {.value = 2},
            .incoming_count = 2,
        };
        IrFunction scalar_function = {.values = scalar_values,
                                      .value_count = BUSTER_ARRAY_LENGTH(scalar_values),
                                      .label_metadata_values = scalar_metadata_values,
                                      .label_metadata = scalar_metadata,
                                      .label_metadata_count = BUSTER_ARRAY_LENGTH(scalar_metadata)};
        BUSTER_TEST(arguments, ir_label_block_parameter_provenance_valid(&scalar_function, &scalar_parameter));
        scalar_blocks_union[1] = (IrBlockId){.value = 17};
        BUSTER_TEST(arguments, !ir_label_block_parameter_provenance_valid(&scalar_function, &scalar_parameter));
        scalar_blocks_union[1] = scalar_b;

        IrLabelProvenancePath aggregate_path_a = {
            .label_blocks = scalar_blocks_a,
            .size = 8,
            .label_block_count = 1,
        };
        IrLabelProvenancePath aggregate_path_b = {
            .label_blocks = scalar_blocks_b,
            .size = 8,
            .label_block_count = 1,
        };
        IrLabelProvenancePath aggregate_path_union = {
            .label_blocks = scalar_blocks_union,
            .size = 8,
            .label_block_count = 2,
        };
        IrValue aggregate_values[3] = {0};
        IrValueId aggregate_metadata_values[3] = {{.value = 0}, {.value = 1}, {.value = 2}};
        IrValueLabelMetadata aggregate_metadata[3] = {
            [0] = {.has_label_provenance = true, .label_blocks = scalar_blocks_a, .label_block_count = 1, .label_paths = &aggregate_path_a, .label_path_count = 1},
            [1] = {.has_label_provenance = true, .label_blocks = scalar_blocks_b, .label_block_count = 1, .label_paths = &aggregate_path_b, .label_path_count = 1},
            [2] = {.has_label_provenance = true,
                   .label_blocks = scalar_blocks_union,
                   .label_block_count = 2,
                   .label_paths = &aggregate_path_union,
                   .label_path_count = 1},
        };
        IrIncoming aggregate_incoming_b = {.predecessor = {.value = 1}, .value = {.value = 1}};
        IrIncoming aggregate_incoming_a = {.next = &aggregate_incoming_b, .predecessor = {.value = 0}, .value = {.value = 0}};
        IrBlockParameter aggregate_parameter = {
            .first_incoming = &aggregate_incoming_a,
            .last_incoming = &aggregate_incoming_b,
            .value = {.value = 2},
            .incoming_count = 2,
        };
        IrFunction aggregate_function = {.values = aggregate_values,
                                         .value_count = BUSTER_ARRAY_LENGTH(aggregate_values),
                                         .label_metadata_values = aggregate_metadata_values,
                                         .label_metadata = aggregate_metadata,
                                         .label_metadata_count = BUSTER_ARRAY_LENGTH(aggregate_metadata)};
        BUSTER_TEST(arguments, ir_label_block_parameter_provenance_valid(&aggregate_function, &aggregate_parameter));
    }
    {
        TemporalArena cleanup_label_temporary = scratch_begin(0, 0);
        CPreprocessResult cleanup_label_tokens = c_preprocess(
            cleanup_label_temporary.arena,
            S8("extern void cleanup_dispatch_callback(int *);"
               "int cleanup_computed_dispatch(int selector) {"
               " { int value __attribute__((cleanup(cleanup_dispatch_callback))) = selector;"
               "   goto *(selector ? &&one : &&zero);"
               " }"
               "zero: return 0;"
               "one: return 1;"
               "}"
               "int cleanup_asm_dispatch(int selector) {"
               " { int value __attribute__((cleanup(cleanup_dispatch_callback))) = selector;"
               "   if (selector)"
               "     __asm__ goto(\"jmp %l1\" : : \"r\"(selector) : \"cc\" : taken);"
               " }"
               " return 0;"
               "taken: return 1;"
               "}\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult cleanup_label_parse = c_parse(cleanup_label_temporary.arena, cleanup_label_tokens);
        CIRLowerResult cleanup_label_lowered = c_lower_to_ir(cleanup_label_temporary.arena, S8("cleanup-labels.c"), cleanup_label_tokens,
                                                             cleanup_label_parse, target_native);
        BUSTER_TEST(arguments, cleanup_label_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, cleanup_label_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, cleanup_label_lowered.diagnostic_count == 0);
        if (cleanup_label_lowered.program)
        {
            IrFunction* function = 0;
            for (u32 function_index = 0; function_index < cleanup_label_lowered.program->modules[0].function_count; function_index += 1)
            {
                IrFunction* candidate = cleanup_label_lowered.program->modules[0].functions + function_index;
                if (string_equal(candidate->name, S8("cleanup_computed_dispatch")))
                {
                    function = candidate;
                    break;
                }
            }
            u32 cleanup_call_count = 0;
            u32 indirect_branch_count = 0;
            u32 branch_if_count = 0;
            IrFunction* asm_function = 0;
            if (function)
            {
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrOpcode opcode = function->instructions[instruction_index].opcode;
                    cleanup_call_count += opcode == IR_OPCODE_CALL;
                    indirect_branch_count += opcode == IR_OPCODE_INDIRECT_BRANCH;
                    branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
                }
            }
            BUSTER_TEST(arguments, function != 0);
            BUSTER_TEST(arguments, cleanup_call_count != 0);
            BUSTER_TEST(arguments, indirect_branch_count == 0);
            BUSTER_TEST(arguments, branch_if_count >= 2);
            for (u32 function_index = 0; function_index < cleanup_label_lowered.program->modules[0].function_count; function_index += 1)
            {
                IrFunction* candidate = cleanup_label_lowered.program->modules[0].functions + function_index;
                if (string_equal(candidate->name, S8("cleanup_asm_dispatch")))
                {
                    asm_function = candidate;
                    break;
                }
            }
            BUSTER_TEST(arguments, asm_function != 0);
            if (asm_function)
            {
                u32 asm_goto_count = 0;
                u32 asm_cleanup_call_count = 0;
                for (u32 instruction_index = 0; instruction_index < asm_function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = asm_function->instructions + instruction_index;
                    asm_goto_count += instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && instruction->target_count == 2;
                    asm_cleanup_call_count += instruction->opcode == IR_OPCODE_CALL;
                }
                BUSTER_TEST(arguments, asm_goto_count == 1);
                BUSTER_TEST(arguments, asm_cleanup_call_count != 0);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(cleanup_label_lowered.program, &cleanup_label_lowered.program->modules[0]).error == IR_VALIDATION_NONE);
        }
        scratch_end(cleanup_label_temporary);
    }
    {
        TemporalArena invalid_labels_temporary = scratch_begin(0, 0);
        String8 invalid_labels_source = S8("int invalid_labels(void) { void *target = (void *)0; goto *target; }\n");
        CPreprocessResult invalid_labels_tokens = c_preprocess(invalid_labels_temporary.arena, invalid_labels_source,
                                                               (CPreprocessOptions){
                                                                   .target = target_native,
                                                                   .data_layout = target_data_layout(target_native),
                                                                   .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                               });
        CParseResult invalid_labels_parse = c_parse(invalid_labels_temporary.arena, invalid_labels_tokens);
        CIRLowerResult invalid_labels_lowered = c_lower_to_ir(invalid_labels_temporary.arena, S8("invalid-label-target.c"), invalid_labels_tokens,
                                                              invalid_labels_parse, target_native);
        BUSTER_TEST(arguments, invalid_labels_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_labels_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_labels_lowered.diagnostic_count == 1);
        if (invalid_labels_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_labels_lowered.diagnostics[0].message,
                               S8("in function 'invalid_labels': computed goto requires a function-local void pointer label value"));
        }
        scratch_end(invalid_labels_temporary);
    }
    {
        TemporalArena invalid_static_label_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_static_label_tokens = c_preprocess(invalid_static_label_temporary.arena,
                                                                      S8("void *saved_label; int invalid_static_label(void) { saved_label = &&target; target: return 0; }\n"),
                                                                      (CPreprocessOptions){
                                                                          .target = target_native,
                                                                          .data_layout = target_data_layout(target_native),
                                                                          .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                      });
        CParseResult invalid_static_label_parse = c_parse(invalid_static_label_temporary.arena, invalid_static_label_tokens);
        CIRLowerResult invalid_static_label_lowered = c_lower_to_ir(invalid_static_label_temporary.arena, S8("invalid-static-label.c"),
                                                                      invalid_static_label_tokens, invalid_static_label_parse, target_native);
        BUSTER_TEST(arguments, invalid_static_label_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_static_label_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_static_label_lowered.diagnostic_count == 1);
        if (invalid_static_label_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_static_label_lowered.diagnostics[0].message,
                               S8("in function 'invalid_static_label': a label address may not be stored in static storage"));
        }
        scratch_end(invalid_static_label_temporary);
    }
    {
        TemporalArena duplicate_labels_temporary = scratch_begin(0, 0);
        CPreprocessResult duplicate_labels_tokens = c_preprocess(duplicate_labels_temporary.arena,
                                                                  S8("int duplicate_labels(void) { goto *&&target; target: return 0; target: return 1; }\n"),
                                                                  (CPreprocessOptions){
                                                                      .target = target_native,
                                                                      .data_layout = target_data_layout(target_native),
                                                                      .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                  });
        CParseResult duplicate_labels_parse = c_parse(duplicate_labels_temporary.arena, duplicate_labels_tokens);
        CIRLowerResult duplicate_labels_lowered = c_lower_to_ir(duplicate_labels_temporary.arena, S8("duplicate-labels.c"), duplicate_labels_tokens,
                                                                  duplicate_labels_parse, target_native);
        BUSTER_TEST(arguments, duplicate_labels_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, duplicate_labels_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, duplicate_labels_lowered.diagnostic_count == 1);
        if (duplicate_labels_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, duplicate_labels_lowered.diagnostics[0].message, S8("in function 'duplicate_labels': duplicate label 'target'"));
        }
        scratch_end(duplicate_labels_temporary);
    }
    {
        TemporalArena invalid_label_cast_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_label_cast_tokens = c_preprocess(invalid_label_cast_temporary.arena,
                                                                    S8("int invalid_label_cast(void) { return (int)&&target; target: return 0; }\n"),
                                                                    (CPreprocessOptions){
                                                                        .target = target_native,
                                                                        .data_layout = target_data_layout(target_native),
                                                                        .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                    });
        CParseResult invalid_label_cast_parse = c_parse(invalid_label_cast_temporary.arena, invalid_label_cast_tokens);
        CIRLowerResult invalid_label_cast_lowered = c_lower_to_ir(invalid_label_cast_temporary.arena, S8("invalid-label-cast.c"), invalid_label_cast_tokens,
                                                                    invalid_label_cast_parse, target_native);
        BUSTER_TEST(arguments, invalid_label_cast_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_label_cast_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_label_cast_lowered.diagnostic_count == 1);
        if (invalid_label_cast_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_label_cast_lowered.diagnostics[0].message,
                               S8("in function 'invalid_label_cast': a label-provenance value may only be used with its original void pointer type"));
        }
        scratch_end(invalid_label_cast_temporary);
    }
    {
        TemporalArena invalid_label_deref_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_label_deref_tokens = c_preprocess(invalid_label_deref_temporary.arena,
                                                                     S8("int invalid_label_deref(void) { return *&&target; target: return 0; }\n"),
                                                                     (CPreprocessOptions){
                                                                         .target = target_native,
                                                                         .data_layout = target_data_layout(target_native),
                                                                         .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                     });
        CParseResult invalid_label_deref_parse = c_parse(invalid_label_deref_temporary.arena, invalid_label_deref_tokens);
        CIRLowerResult invalid_label_deref_lowered = c_lower_to_ir(invalid_label_deref_temporary.arena, S8("invalid-label-deref.c"), invalid_label_deref_tokens,
                                                                     invalid_label_deref_parse, target_native);
        BUSTER_TEST(arguments, invalid_label_deref_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_label_deref_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_label_deref_lowered.diagnostic_count == 1);
        if (invalid_label_deref_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_label_deref_lowered.diagnostics[0].message,
                               S8("in function 'invalid_label_deref': a label-provenance value may not be used as an addressable place"));
        }
        scratch_end(invalid_label_deref_temporary);
    }
    {
        TemporalArena invalid_label_address_of_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_label_address_of_tokens = c_preprocess(
            invalid_label_address_of_temporary.arena,
            S8("int invalid_label_address_of(void) { void *value = &&target; goto *&value; target: return 0; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult invalid_label_address_of_parse = c_parse(invalid_label_address_of_temporary.arena, invalid_label_address_of_tokens);
        CIRLowerResult invalid_label_address_of_lowered = c_lower_to_ir(invalid_label_address_of_temporary.arena, S8("invalid-label-address-of.c"),
                                                                         invalid_label_address_of_tokens, invalid_label_address_of_parse, target_native);
        BUSTER_TEST(arguments, invalid_label_address_of_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_label_address_of_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_label_address_of_lowered.diagnostic_count == 1);
        if (invalid_label_address_of_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_label_address_of_lowered.diagnostics[0].message,
                               S8("in function 'invalid_label_address_of': computed goto requires a function-local void pointer label value"));
        }
        scratch_end(invalid_label_address_of_temporary);
    }
    {
        TemporalArena invalid_union_overlay_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_union_overlay_tokens = c_preprocess(
            invalid_union_overlay_temporary.arena,
            S8("union label_overlay { void *label; int *data; };"
               "int invalid_union_overlay(void) { union label_overlay value = { .label = &&target }; return *value.data; target: return 0; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult invalid_union_overlay_parse = c_parse(invalid_union_overlay_temporary.arena, invalid_union_overlay_tokens);
        CIRLowerResult invalid_union_overlay_lowered = c_lower_to_ir(invalid_union_overlay_temporary.arena, S8("invalid-union-overlay.c"),
                                                                      invalid_union_overlay_tokens, invalid_union_overlay_parse, target_native);
        BUSTER_TEST(arguments, invalid_union_overlay_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_union_overlay_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_union_overlay_lowered.diagnostic_count == 1);
        if (invalid_union_overlay_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_union_overlay_lowered.diagnostics[0].message,
                               S8("in function 'invalid_union_overlay': a label-provenance value may not be used as an addressable place"));
        }
        scratch_end(invalid_union_overlay_temporary);
    }
    {
        TemporalArena invalid_mixed_label_integer_cast_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_mixed_label_integer_cast_tokens = c_preprocess(
            invalid_mixed_label_integer_cast_temporary.arena,
            S8("unsigned long invalid_mixed_label_integer_cast(int selector) { void *value = selector ? &&target : 0; return (unsigned long)value; target: return 0; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult invalid_mixed_label_integer_cast_parse = c_parse(invalid_mixed_label_integer_cast_temporary.arena, invalid_mixed_label_integer_cast_tokens);
        CIRLowerResult invalid_mixed_label_integer_cast_lowered = c_lower_to_ir(
            invalid_mixed_label_integer_cast_temporary.arena, S8("invalid-mixed-label-integer-cast.c"), invalid_mixed_label_integer_cast_tokens,
            invalid_mixed_label_integer_cast_parse, target_native);
        BUSTER_TEST(arguments, invalid_mixed_label_integer_cast_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_mixed_label_integer_cast_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_mixed_label_integer_cast_lowered.diagnostic_count == 1);
        if (invalid_mixed_label_integer_cast_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_mixed_label_integer_cast_lowered.diagnostics[0].message,
                               S8("in function 'invalid_mixed_label_integer_cast': a label-provenance value may only be used with its original void pointer type"));
        }
        scratch_end(invalid_mixed_label_integer_cast_temporary);
    }
    {
        TemporalArena invalid_mixed_label_pointer_cast_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_mixed_label_pointer_cast_tokens = c_preprocess(
            invalid_mixed_label_pointer_cast_temporary.arena,
            S8("int invalid_mixed_label_pointer_cast(int selector) { void *value = selector ? &&target : 0; return *(int *)value; target: return 0; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult invalid_mixed_label_pointer_cast_parse = c_parse(invalid_mixed_label_pointer_cast_temporary.arena, invalid_mixed_label_pointer_cast_tokens);
        CIRLowerResult invalid_mixed_label_pointer_cast_lowered = c_lower_to_ir(
            invalid_mixed_label_pointer_cast_temporary.arena, S8("invalid-mixed-label-pointer-cast.c"), invalid_mixed_label_pointer_cast_tokens,
            invalid_mixed_label_pointer_cast_parse, target_native);
        BUSTER_TEST(arguments, invalid_mixed_label_pointer_cast_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_mixed_label_pointer_cast_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_mixed_label_pointer_cast_lowered.diagnostic_count == 1);
        if (invalid_mixed_label_pointer_cast_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_mixed_label_pointer_cast_lowered.diagnostics[0].message,
                               S8("in function 'invalid_mixed_label_pointer_cast': a label-provenance value may only be used with its original void pointer type"));
        }
        scratch_end(invalid_mixed_label_pointer_cast_temporary);
    }
    {
        TemporalArena valid_struct_label_sibling_temporary = scratch_begin(0, 0);
        CPreprocessResult valid_struct_label_sibling_tokens = c_preprocess(
            valid_struct_label_sibling_temporary.arena,
            S8("struct label_and_count { void *label; int count; };"
               "int valid_struct_label_sibling(void) { struct label_and_count value = { .label = &&target, .count = 7 }; return value.count; target: return value.count; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult valid_struct_label_sibling_parse = c_parse(valid_struct_label_sibling_temporary.arena, valid_struct_label_sibling_tokens);
        CIRLowerResult valid_struct_label_sibling_lowered = c_lower_to_ir(valid_struct_label_sibling_temporary.arena, S8("valid-struct-label-sibling.c"),
                                                                            valid_struct_label_sibling_tokens, valid_struct_label_sibling_parse, target_native);
        BUSTER_TEST(arguments, valid_struct_label_sibling_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, valid_struct_label_sibling_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, valid_struct_label_sibling_lowered.diagnostic_count == 0);
        if (valid_struct_label_sibling_lowered.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(valid_struct_label_sibling_lowered.program,
                                                                 &valid_struct_label_sibling_lowered.program->modules[0]).error == IR_VALIDATION_NONE);
        }
        scratch_end(valid_struct_label_sibling_temporary);
    }
    {
        TemporalArena label_provenance_mutation_temporary = scratch_begin(0, 0);
        CPreprocessResult label_provenance_mutation_tokens = c_preprocess(
            label_provenance_mutation_temporary.arena,
            S8("int forged_label_dereference(int *pointer) { return *pointer; }"
               "unsigned long forged_label_cast(int *pointer) { return (unsigned long)pointer; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult label_provenance_mutation_parse = c_parse(label_provenance_mutation_temporary.arena, label_provenance_mutation_tokens);
        CIRLowerResult label_provenance_mutation_lowered = c_lower_to_ir(label_provenance_mutation_temporary.arena, S8("label-provenance-mutations.c"),
                                                                          label_provenance_mutation_tokens, label_provenance_mutation_parse, target_native);
        BUSTER_TEST(arguments, label_provenance_mutation_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_provenance_mutation_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_provenance_mutation_lowered.diagnostic_count == 0);
        if (label_provenance_mutation_lowered.program)
        {
            IrModule* mutation_module = &label_provenance_mutation_lowered.program->modules[0];
            BUSTER_TEST(arguments, ir_validate_canonical_module(label_provenance_mutation_lowered.program, mutation_module).error == IR_VALIDATION_NONE);
            IrBlockId forged_block = {.value = 0};
            bool dereference_mutated = false;
            bool cast_mutated = false;
            for (u32 function_index = 0; function_index < mutation_module->function_count; function_index += 1)
            {
                IrFunction* function = mutation_module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    if (instruction->operand_count != 1 || !instruction->operands || instruction->operands[0].value >= function->value_count)
                    {
                        continue;
                    }
                    if ((instruction->opcode != IR_OPCODE_DEREFERENCE && instruction->opcode != IR_OPCODE_CAST) ||
                        function->block_count == 0)
                    {
                        continue;
                    }
                    IrValueLabelMetadata* operand =
                        ir_value_label_metadata_ensure(label_provenance_mutation_temporary.arena, function, instruction->operands[0]);
                    IrValueLabelMetadata saved = *operand;
                    operand->is_label_value = false;
                    operand->has_label_provenance = true;
                    operand->has_non_label_provenance = false;
                    operand->label_blocks = &forged_block;
                    operand->label_block_count = 1;
                    operand->label_paths = 0;
                    operand->label_path_count = 0;
                    IrValidationResult mutated_validation = ir_validate_canonical_module(label_provenance_mutation_lowered.program, mutation_module);
                    if (instruction->opcode == IR_OPCODE_DEREFERENCE)
                    {
                        dereference_mutated = mutated_validation.error != IR_VALIDATION_NONE;
                    }
                    else
                    {
                        cast_mutated = mutated_validation.error != IR_VALIDATION_NONE;
                    }
                    *operand = saved;
                }
            }
            BUSTER_TEST(arguments, dereference_mutated && cast_mutated);
        }
        scratch_end(label_provenance_mutation_temporary);
    }
    {
        TemporalArena legal_label_address_of_temporary = scratch_begin(0, 0);
        CPreprocessResult legal_label_address_of_tokens = c_preprocess(
            legal_label_address_of_temporary.arena,
            S8("extern void take_pointer_address(void **);"
               "int legal_label_address_of(void) { void *value = 0; take_pointer_address(&value); return 0; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult legal_label_address_of_parse = c_parse(legal_label_address_of_temporary.arena, legal_label_address_of_tokens);
        CIRLowerResult legal_label_address_of_lowered = c_lower_to_ir(legal_label_address_of_temporary.arena, S8("legal-label-address-of.c"),
                                                                       legal_label_address_of_tokens, legal_label_address_of_parse, target_native);
        BUSTER_TEST(arguments, legal_label_address_of_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, legal_label_address_of_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, legal_label_address_of_lowered.diagnostic_count == 0);
        if (legal_label_address_of_lowered.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(legal_label_address_of_lowered.program,
                                                                 &legal_label_address_of_lowered.program->modules[0]).error == IR_VALIDATION_NONE);
        }
        scratch_end(legal_label_address_of_temporary);
    }
    {
        TemporalArena invalid_asm_goto_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_asm_goto_tokens = c_preprocess(invalid_asm_goto_temporary.arena,
                                                                 S8("int invalid_asm_goto(void) { __asm__ goto (\"\" ::: : missing); return 0; }\n"),
                                                                 (CPreprocessOptions){
                                                                     .target = target_native,
                                                                     .data_layout = target_data_layout(target_native),
                                                                     .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                 });
        CParseResult invalid_asm_goto_parse = c_parse(invalid_asm_goto_temporary.arena, invalid_asm_goto_tokens);
        CIRLowerResult invalid_asm_goto_lowered = c_lower_to_ir(invalid_asm_goto_temporary.arena, S8("invalid-asm-goto.c"), invalid_asm_goto_tokens,
                                                                  invalid_asm_goto_parse, target_native);
        BUSTER_TEST(arguments, invalid_asm_goto_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_asm_goto_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_asm_goto_lowered.diagnostic_count == 1);
        if (invalid_asm_goto_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_asm_goto_lowered.diagnostics[0].message,
                               S8("in function 'invalid_asm_goto': asm goto label 'missing' is not defined in this function"));
        }
        scratch_end(invalid_asm_goto_temporary);
    }
    {
        TemporalArena duplicate_asm_goto_temporary = scratch_begin(0, 0);
        CPreprocessResult duplicate_asm_goto_tokens = c_preprocess(duplicate_asm_goto_temporary.arena,
                                                                    S8("int duplicate_asm_goto(void) { target: __asm__ goto (\"\" ::: : target, target); return 0; }\n"),
                                                                    (CPreprocessOptions){
                                                                        .target = target_native,
                                                                        .data_layout = target_data_layout(target_native),
                                                                        .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                    });
        CParseResult duplicate_asm_goto_parse = c_parse(duplicate_asm_goto_temporary.arena, duplicate_asm_goto_tokens);
        CIRLowerResult duplicate_asm_goto_lowered = c_lower_to_ir(duplicate_asm_goto_temporary.arena, S8("duplicate-asm-goto.c"), duplicate_asm_goto_tokens,
                                                                    duplicate_asm_goto_parse, target_native);
        BUSTER_TEST(arguments, duplicate_asm_goto_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, duplicate_asm_goto_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, duplicate_asm_goto_lowered.diagnostic_count == 1);
        if (duplicate_asm_goto_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, duplicate_asm_goto_lowered.diagnostics[0].message,
                               S8("in function 'duplicate_asm_goto': asm goto label 'target' is listed more than once"));
        }
        scratch_end(duplicate_asm_goto_temporary);
    }
    {
        TemporalArena malformed_asm_goto_temporary = scratch_begin(0, 0);
        CPreprocessResult malformed_asm_goto_tokens = c_preprocess(malformed_asm_goto_temporary.arena,
                                                                    S8("int malformed_asm_goto(void) { __asm__ goto (\"\"); return 0; }\n"),
                                                                    (CPreprocessOptions){
                                                                        .target = target_native,
                                                                        .data_layout = target_data_layout(target_native),
                                                                        .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                    });
        CParseResult malformed_asm_goto_parse = c_parse(malformed_asm_goto_temporary.arena, malformed_asm_goto_tokens);
        CIRLowerResult malformed_asm_goto_lowered = c_lower_to_ir(malformed_asm_goto_temporary.arena, S8("malformed-asm-goto.c"), malformed_asm_goto_tokens,
                                                                    malformed_asm_goto_parse, target_native);
        BUSTER_TEST(arguments, malformed_asm_goto_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, malformed_asm_goto_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, malformed_asm_goto_lowered.diagnostic_count == 1);
        if (malformed_asm_goto_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, malformed_asm_goto_lowered.diagnostics[0].message,
                               S8("in function 'malformed_asm_goto': asm goto requires four colon sections"));
        }
        scratch_end(malformed_asm_goto_temporary);
    }
    {
        TemporalArena unqualified_asm_goto_temporary = scratch_begin(0, 0);
        CPreprocessResult unqualified_asm_goto_tokens = c_preprocess(unqualified_asm_goto_temporary.arena,
                                                                      S8("int unqualified_asm_goto(void) { __asm__ (\"\" : : : : target); target: return 0; }\n"),
                                                                      (CPreprocessOptions){
                                                                          .target = target_native,
                                                                          .data_layout = target_data_layout(target_native),
                                                                          .dialect = C_PREPROCESS_DIALECT_GNU23,
                                                                      });
        CParseResult unqualified_asm_goto_parse = c_parse(unqualified_asm_goto_temporary.arena, unqualified_asm_goto_tokens);
        CIRLowerResult unqualified_asm_goto_lowered = c_lower_to_ir(unqualified_asm_goto_temporary.arena, S8("unqualified-asm-goto.c"),
                                                                      unqualified_asm_goto_tokens, unqualified_asm_goto_parse, target_native);
        BUSTER_TEST(arguments, unqualified_asm_goto_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, unqualified_asm_goto_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, unqualified_asm_goto_lowered.diagnostic_count == 1);
        if (unqualified_asm_goto_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, unqualified_asm_goto_lowered.diagnostics[0].message,
                               S8("in function 'unqualified_asm_goto': four asm colon sections require the goto qualifier"));
        }
        scratch_end(unqualified_asm_goto_temporary);
    }
    {
        TemporalArena conflicting_fixed_asm_temporary = scratch_begin(0, 0);
        CPreprocessResult conflicting_fixed_asm_tokens = c_preprocess(
            conflicting_fixed_asm_temporary.arena,
            S8("int conflicting_fixed_asm(int left, int right) { __asm__(\"\" : \"+a\"(left) : \"a\"(right)); return left; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult conflicting_fixed_asm_parse = c_parse(conflicting_fixed_asm_temporary.arena, conflicting_fixed_asm_tokens);
        CIRLowerResult conflicting_fixed_asm_lowered = c_lower_to_ir(conflicting_fixed_asm_temporary.arena, S8("conflicting-fixed-asm.c"),
                                                                       conflicting_fixed_asm_tokens, conflicting_fixed_asm_parse, target_native);
        BUSTER_TEST(arguments, conflicting_fixed_asm_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, conflicting_fixed_asm_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, conflicting_fixed_asm_lowered.diagnostic_count == 1);
        if (conflicting_fixed_asm_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, conflicting_fixed_asm_lowered.diagnostics[0].message,
                               S8("in function 'conflicting_fixed_asm': asm fixed-register operands conflict without a supported matching constraint"));
        }
        scratch_end(conflicting_fixed_asm_temporary);
    }
    {
        TemporalArena c_labels_temporary = scratch_begin(0, 0);
        CPreprocessResult c_labels_tokens = c_preprocess(c_labels_temporary.arena,
                                                         S8("int c_labels(void) { return &&target; target: return 0; }\n"),
                                                         (CPreprocessOptions){
                                                             .target = target_native,
                                                             .data_layout = target_data_layout(target_native),
                                                             .dialect = C_PREPROCESS_DIALECT_C23,
                                                         });
        CParseResult c_labels_parse = c_parse(c_labels_temporary.arena, c_labels_tokens);
        BUSTER_TEST(arguments, c_labels_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, c_labels_parse.diagnostic_count == 1);
        if (c_labels_parse.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, c_labels_parse.diagnostics[0].message,
                               S8("in function 'c_labels': GNU labels-as-values are only available in GNU dialects"));
        }
        scratch_end(c_labels_temporary);
    }
    {
        TemporalArena same_fixed_asm_temporary = scratch_begin(0, 0);
        CPreprocessResult same_fixed_asm_tokens = c_preprocess(
            same_fixed_asm_temporary.arena,
            S8("int same_fixed_asm(int value) { __asm__(\"\" : \"+a\"(value) : \"a\"(value)); return value; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult same_fixed_asm_parse = c_parse(same_fixed_asm_temporary.arena, same_fixed_asm_tokens);
        CIRLowerResult same_fixed_asm_lowered = c_lower_to_ir(same_fixed_asm_temporary.arena, S8("same-fixed-asm.c"), same_fixed_asm_tokens,
                                                               same_fixed_asm_parse, target_native);
        BUSTER_TEST(arguments, same_fixed_asm_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, same_fixed_asm_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, same_fixed_asm_lowered.diagnostic_count == 1);
        if (same_fixed_asm_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, same_fixed_asm_lowered.diagnostics[0].message,
                               S8("in function 'same_fixed_asm': asm fixed-register operands conflict without a supported matching constraint"));
        }
        scratch_end(same_fixed_asm_temporary);
    }
    {
        TemporalArena aarch64_clobber_temporary = scratch_begin(0, 0);
        Target aarch64_target = target_native;
        aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
        CPreprocessResult aarch64_clobber_tokens = c_preprocess(
            aarch64_clobber_temporary.arena,
            S8("int invalid_aarch64_clobber(void) { __asm__(\"\" ::: \"x19\"); return 0; }\n"),
            (CPreprocessOptions){
                .target = aarch64_target,
                .data_layout = target_data_layout(aarch64_target),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult aarch64_clobber_parse = c_parse(aarch64_clobber_temporary.arena, aarch64_clobber_tokens);
        CIRLowerResult aarch64_clobber_lowered = c_lower_to_ir(aarch64_clobber_temporary.arena, S8("aarch64-clobber.c"), aarch64_clobber_tokens,
                                                               aarch64_clobber_parse, aarch64_target);
        BUSTER_TEST(arguments, aarch64_clobber_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_clobber_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_clobber_lowered.diagnostic_count == 1);
        if (aarch64_clobber_lowered.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, aarch64_clobber_lowered.diagnostics[0].message,
                               S8("in function 'invalid_aarch64_clobber': unsupported GNU inline assembly clobber"));
        }
        scratch_end(aarch64_clobber_temporary);
    }
    {
        TemporalArena malformed_recovery_temporary = scratch_begin(0, 0);
        CPreprocessResult malformed_recovery_tokens = c_preprocess(
            malformed_recovery_temporary.arena,
            S8("int malformed_recovery(void) { __asm__ goto(\"\"); return 1; }\n"
               "int valid_after_malformed(void) { return 9; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult malformed_recovery_parse = c_parse(malformed_recovery_temporary.arena, malformed_recovery_tokens);
        CIRLowerResult malformed_recovery_lowered = c_lower_to_ir(malformed_recovery_temporary.arena, S8("malformed-recovery.c"), malformed_recovery_tokens,
                                                                   malformed_recovery_parse, target_native);
        BUSTER_TEST(arguments, malformed_recovery_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, malformed_recovery_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, malformed_recovery_lowered.diagnostic_count == 1);
        bool recovered_function = false;
        if (malformed_recovery_lowered.program)
        {
            IrModule* module = malformed_recovery_lowered.program->modules;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                recovered_function |= string_equal(function->name, S8("valid_after_malformed")) && function->state == IR_FUNCTION_LOWERED &&
                                      function->instruction_count != 0;
            }
        }
        BUSTER_TEST(arguments, recovered_function);
        scratch_end(malformed_recovery_temporary);
    }
    {
        TemporalArena typedef_shadow_label_temporary = scratch_begin(0, 0);
        CPreprocessResult typedef_shadow_label_tokens = c_preprocess(
            typedef_shadow_label_temporary.arena,
            S8("typedef void *T;"
               "int typedef_shadow_label(void) { int T = 1; int x = 2; return (T) && x; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
        });
        CParseResult typedef_shadow_label_parse = c_parse(typedef_shadow_label_temporary.arena, typedef_shadow_label_tokens);
        CIRLowerResult typedef_shadow_label_lowered = c_lower_to_ir(typedef_shadow_label_temporary.arena, S8("typedef-shadow-label.c"), typedef_shadow_label_tokens,
                                                                     typedef_shadow_label_parse, target_native);
        BUSTER_TEST(arguments, typedef_shadow_label_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, typedef_shadow_label_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, typedef_shadow_label_lowered.diagnostic_count == 0);
        if (typedef_shadow_label_lowered.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(typedef_shadow_label_lowered.program,
                                                                 &typedef_shadow_label_lowered.program->modules[0]).error == IR_VALIDATION_NONE);
        }
        scratch_end(typedef_shadow_label_temporary);
    }
    {
        TemporalArena symbolic_array_operand_temporary = scratch_begin(0, 0);
        CPreprocessResult symbolic_array_operand_tokens = c_preprocess(
            symbolic_array_operand_temporary.arena,
            S8("int symbolic_array_operand(int index) { int values[2] = {1, 2};"
               " __asm__(\"\" : [value] \"+r\"(values[index]) : [index_input] \"r\"(index)); return values[index]; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult symbolic_array_operand_parse = c_parse(symbolic_array_operand_temporary.arena, symbolic_array_operand_tokens);
        CIRLowerResult symbolic_array_operand_lowered = c_lower_to_ir(symbolic_array_operand_temporary.arena, S8("symbolic-array-operand.c"),
                                                                      symbolic_array_operand_tokens, symbolic_array_operand_parse, target_native);
        BUSTER_TEST(arguments, symbolic_array_operand_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, symbolic_array_operand_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, symbolic_array_operand_lowered.diagnostic_count == 0);
        if (symbolic_array_operand_lowered.program)
        {
            IrInstruction* assembly = 0;
            IrFunction* function = symbolic_array_operand_lowered.program->modules[0].functions;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                if (function->instructions[instruction_index].opcode == IR_OPCODE_INLINE_ASSEMBLY)
                {
                    assembly = function->instructions + instruction_index;
                    break;
                }
            }
            IrInstructionExtra assembly_extra = assembly ? ir_instruction_extra(function, ir_instruction_self_id(function, assembly)) : (IrInstructionExtra){0};
            BUSTER_TEST(arguments, assembly && assembly_extra.operand_name_count == 2);
            if (assembly && assembly_extra.operand_name_count == 2)
            {
                BUSTER_STRING_TEST(arguments, assembly_extra.operand_names[0], S8("value"));
                BUSTER_STRING_TEST(arguments, assembly_extra.operand_names[1], S8("index_input"));
            }
        }
        scratch_end(symbolic_array_operand_temporary);
    }
    {
        TemporalArena tied_assembly_temporary = scratch_begin(0, 0);
        String8 tied_assembly_source = S8(
            "int numeric_tied(int input) { int output; __asm__(\"\" : \"=r\"(output) : \"0\"(input)); return output; }"
            "int named_tied(int input) { int output; __asm__(\"\" : [dst] \"=r\"(output) : \"[dst]\"(input)); return output; }"
            "int many_tied(int input) { int output0, output1, output2, output3, output4, output5, output6, output7, output8, output9, output10;"
            " __asm__(\"\" : \"=r\"(output0), \"=r\"(output1), \"=r\"(output2), \"=r\"(output3), \"=r\"(output4), \"=r\"(output5),"
            " \"=r\"(output6), \"=r\"(output7), \"=r\"(output8), \"=r\"(output9), \"=r\"(output10) : \"10\"(input)); return output10; }"
            "int four_tied(int a, int b, int c, int d) { int output0, output1, output2, output3;"
            " __asm__(\"\" : \"=r\"(output0), \"=r\"(output1), \"=r\"(output2), \"=r\"(output3) : \"0\"(a), \"1\"(b), \"2\"(c), \"3\"(d), \"r\"(a), \"r\"(b));"
            " return output0 + output1 + output2 + output3; }\n");
        CPreprocessResult tied_assembly_tokens = {0};
        CParseResult tied_assembly_parse = {0};
        CIRLowerResult tied_assembly_lowered = c_test_lower_source(tied_assembly_temporary.arena, tied_assembly_source, S8("tied-assembly.c"), target_native,
                                                                    &tied_assembly_tokens, &tied_assembly_parse);
        BUSTER_TEST(arguments, tied_assembly_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, tied_assembly_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, tied_assembly_lowered.diagnostic_count == 0);
        BUSTER_TEST(arguments, tied_assembly_lowered.program != 0);
        IrInstruction* numeric_tied_assembly = 0;
        IrInstruction* named_tied_assembly = 0;
        IrInstruction* many_tied_assembly = 0;
        IrInstruction* four_tied_assembly = 0;
        IrInstructionExtra named_tied_extra = {0};
        if (tied_assembly_lowered.program)
        {
            IrModule* module = tied_assembly_lowered.program->modules;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                IrInstruction* first_assembly = 0;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    if (function->instructions[instruction_index].opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        first_assembly = function->instructions + instruction_index;
                        break;
                    }
                }
                if (string_equal(function->name, S8("numeric_tied")))
                {
                    numeric_tied_assembly = first_assembly;
                }
                else if (string_equal(function->name, S8("named_tied")))
                {
                    named_tied_assembly = first_assembly;
                    named_tied_extra = first_assembly ? ir_instruction_extra(function, ir_instruction_self_id(function, first_assembly)) : (IrInstructionExtra){0};
                }
                else if (string_equal(function->name, S8("many_tied")))
                {
                    many_tied_assembly = first_assembly;
                }
                else if (string_equal(function->name, S8("four_tied")))
                {
                    four_tied_assembly = first_assembly;
                }
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, module).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, numeric_tied_assembly && numeric_tied_assembly->operand_count == 2);
        BUSTER_TEST(arguments, named_tied_assembly && named_tied_assembly->operand_count == 2);
        BUSTER_TEST(arguments, many_tied_assembly && many_tied_assembly->operand_count == 12);
        BUSTER_TEST(arguments, four_tied_assembly && four_tied_assembly->operand_count == 10);
        if (numeric_tied_assembly)
        {
            BUSTER_TEST(arguments, numeric_tied_assembly->immediates[0] == (IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_R));
            BUSTER_TEST(arguments, (numeric_tied_assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_R);
            BUSTER_TEST(arguments, (numeric_tied_assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0);
            BUSTER_TEST(arguments, IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(numeric_tied_assembly->immediates[1]) == 0);
            u64 saved_constraint = numeric_tied_assembly->immediates[1];
            numeric_tied_assembly->immediates[1] |= UINT64_C(1) << 11;
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            numeric_tied_assembly->immediates[1] = saved_constraint;
            numeric_tied_assembly->immediates[1] = (saved_constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK) |
                                                    IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH | IR_INLINE_ASSEMBLY_CONSTRAINT_R |
                                                    ((u64)1 << IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_SHIFT);
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            numeric_tied_assembly->immediates[1] = saved_constraint;
            numeric_tied_assembly->immediates[1] = saved_constraint | IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT;
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            numeric_tied_assembly->immediates[1] = saved_constraint;
            numeric_tied_assembly->immediates[1] = saved_constraint | IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE;
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            numeric_tied_assembly->immediates[1] = saved_constraint;
            numeric_tied_assembly->immediates[1] = (saved_constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) | IR_INLINE_ASSEMBLY_CONSTRAINT_A;
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            numeric_tied_assembly->immediates[1] = saved_constraint;
            u64 saved_output_constraint = numeric_tied_assembly->immediates[0];
            numeric_tied_assembly->immediates[0] = IR_INLINE_ASSEMBLY_CONSTRAINT_R;
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            numeric_tied_assembly->immediates[0] = saved_output_constraint;
            numeric_tied_assembly->immediates[1] = (saved_constraint & ~(IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH | IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK)) |
                                                    ((u64)1 << IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_SHIFT);
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            numeric_tied_assembly->immediates[1] = saved_constraint;
        }
        if (named_tied_assembly)
        {
            BUSTER_STRING_TEST(arguments, named_tied_extra.operand_names[0], S8("dst"));
            BUSTER_TEST(arguments, (named_tied_assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0);
            BUSTER_TEST(arguments, IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(named_tied_assembly->immediates[1]) == 0);
        }
        if (many_tied_assembly)
        {
            BUSTER_TEST(arguments, (many_tied_assembly->immediates[11] & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0);
            BUSTER_TEST(arguments, IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(many_tied_assembly->immediates[11]) == 10);
        }
        if (four_tied_assembly)
        {
            for (u32 pair_index = 0; pair_index < 4; pair_index += 1)
            {
                u64 constraint = four_tied_assembly->immediates[4 + pair_index];
                BUSTER_TEST(arguments, (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0);
                BUSTER_TEST(arguments, IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint) == pair_index);
            }
            u64 saved_duplicate_constraint = four_tied_assembly->immediates[5];
            four_tied_assembly->immediates[5] = (saved_duplicate_constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK) |
                                                IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH | IR_INLINE_ASSEMBLY_CONSTRAINT_R;
            BUSTER_TEST(arguments, ir_validate_canonical_module(tied_assembly_lowered.program, tied_assembly_lowered.program->modules).error != IR_VALIDATION_NONE);
            four_tied_assembly->immediates[5] = saved_duplicate_constraint;
        }
        scratch_end(tied_assembly_temporary);
    }
    {
        TemporalArena fixed_tied_assembly_temporary = scratch_begin(0, 0);
        Target x86_tied_target = target_native;
        x86_tied_target.cpu_arch = CPU_ARCH_X86_64;
        x86_tied_target.cpu_model = CPU_MODEL_BASELINE;
        CPreprocessResult fixed_tied_tokens = {0};
        CParseResult fixed_tied_parse = {0};
        CIRLowerResult fixed_tied_lowered = c_test_lower_source(
            fixed_tied_assembly_temporary.arena,
            S8("int fixed_tied(int input) { int output; __asm__(\"\" : \"=a\"(output) : \"0\"(input)); return output; }\n"), S8("fixed-tied.c"),
            x86_tied_target, &fixed_tied_tokens, &fixed_tied_parse);
        BUSTER_TEST(arguments, fixed_tied_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, fixed_tied_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, fixed_tied_lowered.diagnostic_count == 0);
        if (fixed_tied_lowered.program)
        {
            IrModule* module = fixed_tied_lowered.program->modules;
            IrFunction* function = c_test_find_ir_function(module, S8("fixed_tied"));
            IrInstruction* assembly = 0;
            if (function)
            {
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    if (function->instructions[instruction_index].opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        assembly = function->instructions + instruction_index;
                        break;
                    }
                }
            }
            BUSTER_TEST(arguments, assembly && assembly->operand_count == 2);
            if (assembly)
            {
                BUSTER_TEST(arguments, (assembly->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
                BUSTER_TEST(arguments, (assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(fixed_tied_lowered.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(fixed_tied_assembly_temporary);
    }
    {
        TemporalArena special_literal_assembly_temporary = scratch_begin(0, 0);
        Target x86_special_target = target_native;
        x86_special_target.cpu_arch = CPU_ARCH_X86_64;
        x86_special_target.cpu_model = CPU_MODEL_BASELINE;
        CPreprocessResult special_tokens = {0};
        CParseResult special_parse = {0};
        CIRLowerResult special_lowered = c_test_lower_source(
            special_literal_assembly_temporary.arena,
            S8("unsigned cpuid_rw(unsigned value) { unsigned eax = value, ebx, ecx = value;"
               " __asm__(\"cpuid\" : \"+a\"(eax), \"=b\"(ebx), \"+c\"(ecx)); return eax ^ ebx ^ ecx; }"
               "unsigned cpuid_tied(unsigned value) { unsigned eax, ebx, ecx, edx;"
               " __asm__(\"cpuid\" : \"=a\"(eax), \"=b\"(ebx), \"=c\"(ecx), \"=d\"(edx) : \"0\"(value), \"2\"(value));"
               " return eax ^ ebx ^ ecx ^ edx; }"
               "unsigned cpuid_reordered(unsigned value) { unsigned eax, ebx, ecx, edx;"
               " __asm__(\"cpuid\" : \"=c\"(ecx), \"=d\"(edx), \"=a\"(eax), \"=b\"(ebx) : \"c\"(value), \"a\"(value));"
               " return eax ^ ebx ^ ecx ^ edx; }"
               "unsigned cpuid_named_tied(unsigned value) { unsigned eax, ecx;"
               " __asm__(\"cpuid\" : [eax_out] \"=a\"(eax), [ecx_out] \"=c\"(ecx) : \"[eax_out]\"(value), \"[ecx_out]\"(value) : \"rbx\");"
               " return eax ^ ecx; }"
               "unsigned cpuid_pointer(unsigned value) { unsigned *input = 0, *output;"
               " __asm__(\"cpuid\" : \"=a\"(output) : \"a\"(input), \"c\"(value) : \"rbx\");"
               " return output != 0; }"
               "unsigned xgetbv_outputs(unsigned value) { unsigned eax, edx;"
               " __asm__(\"xgetbv\" : \"=a\"(eax), \"=d\"(edx) : \"c\"(value)); return eax ^ edx; }\n"),
            S8("special-literal-assembly.c"), x86_special_target, &special_tokens, &special_parse);
        BUSTER_TEST(arguments, special_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, special_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, special_lowered.diagnostic_count == 0);
        if (special_lowered.program)
        {
            IrModule* module = special_lowered.program->modules;
            IrFunction* cpuid_rw = c_test_find_ir_function(module, S8("cpuid_rw"));
            IrFunction* cpuid_tied = c_test_find_ir_function(module, S8("cpuid_tied"));
            IrFunction* cpuid_reordered = c_test_find_ir_function(module, S8("cpuid_reordered"));
            IrFunction* cpuid_named_tied = c_test_find_ir_function(module, S8("cpuid_named_tied"));
            IrFunction* cpuid_pointer = c_test_find_ir_function(module, S8("cpuid_pointer"));
            IrFunction* xgetbv_outputs = c_test_find_ir_function(module, S8("xgetbv_outputs"));
            IrInstruction* cpuid_rw_assembly = 0;
            IrInstruction* cpuid_tied_assembly = 0;
            IrInstruction* cpuid_reordered_assembly = 0;
            IrInstruction* cpuid_named_tied_assembly = 0;
            IrInstruction* cpuid_pointer_assembly = 0;
            IrInstruction* xgetbv_outputs_assembly = 0;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    if (instruction->opcode != IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        continue;
                    }
                    if (function == cpuid_rw)
                    {
                        cpuid_rw_assembly = instruction;
                    }
                    else if (function == cpuid_tied)
                    {
                        cpuid_tied_assembly = instruction;
                    }
                    else if (function == cpuid_reordered)
                    {
                        cpuid_reordered_assembly = instruction;
                    }
                    else if (function == cpuid_named_tied)
                    {
                        cpuid_named_tied_assembly = instruction;
                    }
                    else if (function == cpuid_pointer)
                    {
                        cpuid_pointer_assembly = instruction;
                    }
                    else if (function == xgetbv_outputs)
                    {
                        xgetbv_outputs_assembly = instruction;
                    }
                }
            }
            BUSTER_TEST(arguments, cpuid_rw_assembly && cpuid_rw_assembly->operand_count == 3);
            BUSTER_TEST(arguments, cpuid_tied_assembly && cpuid_tied_assembly->operand_count == 6);
            BUSTER_TEST(arguments, cpuid_reordered_assembly && cpuid_reordered_assembly->operand_count == 6);
            BUSTER_TEST(arguments, cpuid_named_tied_assembly && cpuid_named_tied_assembly->operand_count == 4);
            BUSTER_TEST(arguments, cpuid_pointer_assembly && cpuid_pointer_assembly->operand_count == 3);
            BUSTER_TEST(arguments, xgetbv_outputs_assembly && xgetbv_outputs_assembly->operand_count == 3);
            if (cpuid_rw_assembly)
            {
                BUSTER_TEST(arguments, cpuid_rw_assembly->immediates[0] == (IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE |
                                                                              IR_INLINE_ASSEMBLY_CONSTRAINT_A));
                BUSTER_TEST(arguments, cpuid_rw_assembly->immediates[1] ==
                                           (IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_B));
                BUSTER_TEST(arguments, cpuid_rw_assembly->immediates[2] == (IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE |
                                                                              IR_INLINE_ASSEMBLY_CONSTRAINT_C));
            }
            if (cpuid_tied_assembly)
            {
                BUSTER_TEST(arguments, (cpuid_tied_assembly->immediates[4] & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                             IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(cpuid_tied_assembly->immediates[4]) == 0);
                BUSTER_TEST(arguments, (cpuid_tied_assembly->immediates[5] & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                             IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(cpuid_tied_assembly->immediates[5]) == 2);
            }
            if (cpuid_reordered_assembly)
            {
                BUSTER_TEST(arguments, (cpuid_reordered_assembly->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_C);
                BUSTER_TEST(arguments, (cpuid_reordered_assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_D);
                BUSTER_TEST(arguments, (cpuid_reordered_assembly->immediates[2] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
                BUSTER_TEST(arguments, (cpuid_reordered_assembly->immediates[3] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_B);
                BUSTER_TEST(arguments, (cpuid_reordered_assembly->immediates[4] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_C);
                BUSTER_TEST(arguments, (cpuid_reordered_assembly->immediates[5] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
            }
            if (cpuid_named_tied_assembly)
            {
                BUSTER_TEST(arguments, (cpuid_named_tied_assembly->immediates[2] & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                             IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(cpuid_named_tied_assembly->immediates[2]) == 0);
                BUSTER_TEST(arguments, (cpuid_named_tied_assembly->immediates[3] & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                             IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(cpuid_named_tied_assembly->immediates[3]) == 1);
            }
            if (cpuid_pointer_assembly)
            {
                BUSTER_TEST(arguments, (cpuid_pointer_assembly->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
                BUSTER_TEST(arguments, (cpuid_pointer_assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
                BUSTER_TEST(arguments, (cpuid_pointer_assembly->immediates[2] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_C);
            }
            if (xgetbv_outputs_assembly)
            {
                BUSTER_TEST(arguments, (xgetbv_outputs_assembly->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
                BUSTER_TEST(arguments, (xgetbv_outputs_assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_D);
                BUSTER_TEST(arguments, (xgetbv_outputs_assembly->immediates[2] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_C);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(special_lowered.program, module).error == IR_VALIDATION_NONE);
        }

        String8 invalid_sources[] = {
            S8("unsigned invalid_cpuid_generic(unsigned value) { unsigned eax = value; __asm__(\"cpuid\" : \"+a\"(eax) : \"r\"(value)); return eax; }\n"),
            S8("unsigned invalid_cpuid_hidden_rbx(unsigned value) { unsigned eax = value, ecx = value; __asm__(\"cpuid\" : \"+a\"(eax), \"+c\"(ecx)); return eax; }\n"),
            S8("unsigned invalid_cpuid_missing_eax(unsigned value) { unsigned eax, ecx, edx; __asm__(\"cpuid\" : \"=a\"(eax), \"=c\"(ecx), \"=d\"(edx) : \"c\"(value)); return eax; }\n"),
            S8("unsigned invalid_cpuid_bad_input(unsigned value) { unsigned eax, ebx, ecx, edx; __asm__(\"cpuid\" : \"=a\"(eax), \"=b\"(ebx), \"=c\"(ecx), \"=d\"(edx) : \"a\"(value), \"b\"(value)); return eax; }\n"),
            S8("unsigned invalid_cpuid_rw_b(unsigned value) { unsigned ebx = value, ecx = value; __asm__(\"cpuid\" : \"+b\"(ebx), \"+c\"(ecx)); return ebx; }\n"),
            S8("unsigned invalid_xgetbv_generic(unsigned value) { unsigned eax; __asm__(\"xgetbv\" : \"=a\"(eax) : \"r\"(value)); return eax; }\n"),
            S8("unsigned invalid_xgetbv_a_input(unsigned value) { unsigned eax; __asm__(\"xgetbv\" : \"=a\"(eax) : \"a\"(value)); return eax; }\n"),
            S8("unsigned invalid_xgetbv_rw(unsigned value) { unsigned eax = value; __asm__(\"xgetbv\" : \"+a\"(eax) : \"c\"(value)); return eax; }\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_sources); source_index += 1)
        {
            CPreprocessResult invalid_tokens = {0};
            CParseResult invalid_parse = {0};
            CIRLowerResult invalid_lowered = c_test_lower_source(special_literal_assembly_temporary.arena, invalid_sources[source_index], S8("invalid-special-assembly.c"),
                                                                  x86_special_target, &invalid_tokens, &invalid_parse);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_lowered.diagnostic_count == 1);
        }
        scratch_end(special_literal_assembly_temporary);
    }
    {
        TemporalArena legacy_asm_operand_temporary = scratch_begin(0, 0);
        CPreprocessResult legacy_asm_operand_tokens = {0};
        CParseResult legacy_asm_operand_parse = {0};
        CIRLowerResult legacy_asm_operand_lowered = c_test_lower_source(
            legacy_asm_operand_temporary.arena,
            S8("struct legacy_asm_pair { int left; int right; };"
               "void legacy_float_asm(float input) { float output; __asm__(\"\" : \"=r\"(output) : \"r\"(input)); }"
               "void legacy_pair_asm(struct legacy_asm_pair input) { struct legacy_asm_pair output;"
               " __asm__(\"\" : \"=r\"(output) : \"r\"(input)); }\n"),
            S8("legacy-asm-operands.c"), target_native, &legacy_asm_operand_tokens, &legacy_asm_operand_parse);
        BUSTER_TEST(arguments, legacy_asm_operand_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, legacy_asm_operand_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, legacy_asm_operand_lowered.diagnostic_count == 0);
        if (legacy_asm_operand_lowered.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(legacy_asm_operand_lowered.program, legacy_asm_operand_lowered.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(legacy_asm_operand_temporary);
    }
    {
        String8 invalid_tied_assembly_sources[] = {
            S8("int invalid_tied(void) { int output, input; __asm__(\"\" : \"=r\"(output) : \"12\"(input)); return output; }\n"),
            S8("int invalid_tied(void) { int output, input; __asm__(\"\" : [dst] \"=r\"(output) : \"[missing]\"(input)); return output; }\n"),
            S8("int invalid_tied(void) { int output, input; __asm__(\"\" : [dst] \"=r\"(output) : \"[dst\"(input)); return output; }\n"),
            S8("int invalid_tied(void) { int output, input, other; __asm__(\"\" : \"=r\"(output) : \"0\"(input), \"0\"(other)); return output; }\n"),
            S8("int invalid_tied(void) { int output, input; __asm__(\"\" : \"+r\"(output) : \"0\"(input)); return output; }\n"),
            S8("int invalid_tied(void) { int output; short input; __asm__(\"\" : \"=r\"(output) : \"0\"(input)); return output; }\n"),
            S8("int invalid_tied(void) { int output; void *input = 0; __asm__(\"\" : \"=r\"(output) : \"0\"(input)); return output; }\n"),
            S8("int invalid_tied(float input) { float output; __asm__(\"\" : \"=r\"(output) : \"0\"(input)); return (int)output; }\n"),
            S8("struct invalid_tied_pair { int left; int right; };"
               "void invalid_tied_pair(struct invalid_tied_pair input) { struct invalid_tied_pair output;"
               " __asm__(\"\" : \"=r\"(output) : \"0\"(input)); }\n"),
            S8("int invalid_tied(void) { int output, input; __asm__(\"\" : \"=r\"(output) : \"1x\"(input)); return output; }\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_tied_assembly_sources); source_index += 1)
        {
            TemporalArena invalid_tied_temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tied_tokens = {0};
            CParseResult invalid_tied_parse = {0};
            CIRLowerResult invalid_tied_lowered = c_test_lower_source(invalid_tied_temporary.arena, invalid_tied_assembly_sources[source_index], S8("invalid-tied.c"),
                                                                       target_native, &invalid_tied_tokens, &invalid_tied_parse);
            BUSTER_TEST(arguments, invalid_tied_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_tied_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_tied_lowered.diagnostic_count == 1);
            scratch_end(invalid_tied_temporary);
        }
    }
    // A local register variable pins the operand it is passed as. The class it
    // resolves to is checked directly, because a value that reaches the wrong
    // register is still a lowered function: only the recorded class says which
    // register the emitter will use.
    {
        typedef struct CTestBoundRegisterCase CTestBoundRegisterCase;
        struct CTestBoundRegisterCase
        {
            String8 source;
            u64 constraint;
        };
        CTestBoundRegisterCase bound_register_cases[] = {
            {S8("long bound_rsi(long input) { register long value __asm__(\"rsi\") = input; long output;"
                " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
             IR_INLINE_ASSEMBLY_CONSTRAINT_SI},
            {S8("long bound_rdi(long input) { register long value __asm__(\"rdi\") = input; long output;"
                " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
             IR_INLINE_ASSEMBLY_CONSTRAINT_DI},
            {S8("long bound_r8(long input) { register long value __asm__(\"r8\") = input; long output;"
                " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
             IR_INLINE_ASSEMBLY_CONSTRAINT_R8},
            {S8("long bound_r9(long input) { register long value __asm__(\"%r9\") = input; long output;"
                " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
             IR_INLINE_ASSEMBLY_CONSTRAINT_R9},
            {S8("long bound_r10(long input) { register long value __asm__(\"r10\") = input; long output;"
                " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
             IR_INLINE_ASSEMBLY_CONSTRAINT_R10},
            {S8("long letter_rsi(long input) { long output; __asm__(\"\" : \"=r\"(output) : \"S\"(input)); return output; }\n"),
             IR_INLINE_ASSEMBLY_CONSTRAINT_SI},
            {S8("long letter_rdi(long input) { long output; __asm__(\"\" : \"=r\"(output) : \"D\"(input)); return output; }\n"),
             IR_INLINE_ASSEMBLY_CONSTRAINT_DI},
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(bound_register_cases); case_index += 1)
        {
            TemporalArena bound_register_temporary = scratch_begin(0, 0);
            Target bound_register_target = target_native;
            bound_register_target.cpu_arch = CPU_ARCH_X86_64;
            CPreprocessResult bound_register_tokens = {0};
            CParseResult bound_register_parse = {0};
            CIRLowerResult bound_register_lowered =
                c_test_lower_source(bound_register_temporary.arena, bound_register_cases[case_index].source, S8("bound-register.c"), bound_register_target,
                                    &bound_register_tokens, &bound_register_parse);
            BUSTER_TEST(arguments, bound_register_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, bound_register_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, bound_register_lowered.diagnostic_count == 0);
            if (bound_register_lowered.program)
            {
                IrModule* module = bound_register_lowered.program->modules;
                IrInstruction* assembly = 0;
                for (u32 function_index = 0; module && function_index < module->function_count && !assembly; function_index += 1)
                {
                    IrFunction* function = module->functions + function_index;
                    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                    {
                        if (function->instructions[instruction_index].opcode == IR_OPCODE_INLINE_ASSEMBLY)
                        {
                            assembly = function->instructions + instruction_index;
                            break;
                        }
                    }
                }
                BUSTER_TEST(arguments, assembly && assembly->operand_count == 2);
                if (assembly && assembly->operand_count == 2)
                {
                    BUSTER_TEST(arguments, (assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == bound_register_cases[case_index].constraint);
                }
                BUSTER_TEST(arguments, ir_validate_canonical_module(bound_register_lowered.program, module).error == IR_VALIDATION_NONE);
            }
            scratch_end(bound_register_temporary);
        }
    }
    // A memory constraint is not a register class: the operand it carries is
    // the storage rather than the value, and the class it lowers to is what
    // tells the emitter to put an address in the register it assigns.
    {
        String8 memory_constraint_sources[] = {
            S8("void store(volatile int* p, int v) { __asm__ __volatile__(\"mov %1, %0\" : \"=m\"(*p) : \"r\"(v) : \"memory\"); }\n"),
            S8("void bump(volatile int* p) { __asm__ __volatile__(\"lock ; incl %0\" : \"=m\"(*p) : \"m\"(*p) : \"memory\"); }\n"),
        };
        u32 memory_constraint_operands[] = {2, 2};
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(memory_constraint_sources); source_index += 1)
        {
            TemporalArena memory_temporary = scratch_begin(0, 0);
            Target memory_target = target_native;
            memory_target.cpu_arch = CPU_ARCH_X86_64;
            CPreprocessResult memory_tokens = {0};
            CParseResult memory_parse = {0};
            CIRLowerResult memory_lowered = c_test_lower_source(memory_temporary.arena, memory_constraint_sources[source_index], S8("memory-constraint.c"),
                                                               memory_target, &memory_tokens, &memory_parse);
            BUSTER_TEST(arguments, memory_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, memory_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, memory_lowered.diagnostic_count == 0);
            if (memory_lowered.program)
            {
                IrModule* module = memory_lowered.program->modules;
                IrInstruction* assembly = 0;
                for (u32 function_index = 0; module && function_index < module->function_count && !assembly; function_index += 1)
                {
                    IrFunction* function = module->functions + function_index;
                    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                    {
                        if (function->instructions[instruction_index].opcode == IR_OPCODE_INLINE_ASSEMBLY)
                        {
                            assembly = function->instructions + instruction_index;
                            break;
                        }
                    }
                }
                BUSTER_TEST(arguments, assembly && assembly->operand_count == memory_constraint_operands[source_index]);
                if (assembly && assembly->operand_count == memory_constraint_operands[source_index])
                {
                    BUSTER_TEST(arguments, (assembly->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_M);
                    BUSTER_TEST(arguments, (assembly->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0);
                }
                BUSTER_TEST(arguments, ir_validate_canonical_module(memory_lowered.program, module).error == IR_VALIDATION_NONE);
            }
            scratch_end(memory_temporary);
        }
    }
    // What a template may not spell literally. A register the emitter can hand
    // to an operand stays out however it is written, and the two it can never
    // hand out are allowed only in the position where they cannot alias: the
    // stack pointer as a memory base, a segment selector before a colon.
    {
        String8 invalid_literal_sources[] = {
            S8("void f(int v) { __asm__ __volatile__(\"mov %0, %%rax\" : : \"r\"(v)); }\n"),
            S8("void f(int v) { __asm__ __volatile__(\"mov %0, (%%rax)\" : : \"r\"(v)); }\n"),
            S8("void f(int v) { __asm__ __volatile__(\"add $8, %%rsp ; mov %0, %0\" : \"+r\"(v)); }\n"),
            S8("void f(int v) { __asm__ __volatile__(\"mov %0, %%fs\" : : \"r\"(v)); }\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_literal_sources); source_index += 1)
        {
            TemporalArena invalid_literal_temporary = scratch_begin(0, 0);
            Target invalid_literal_target = target_native;
            invalid_literal_target.cpu_arch = CPU_ARCH_X86_64;
            CPreprocessResult invalid_literal_tokens = {0};
            CParseResult invalid_literal_parse = {0};
            CIRLowerResult invalid_literal_lowered =
                c_test_lower_source(invalid_literal_temporary.arena, invalid_literal_sources[source_index], S8("invalid-literal-register.c"),
                                    invalid_literal_target, &invalid_literal_tokens, &invalid_literal_parse);
            BUSTER_TEST(arguments, invalid_literal_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_literal_parse.diagnostic_count == 0);
            // The template is policed in the emitter rather than in lowering,
            // so what is asserted here is that the module still validates; the
            // refusal itself is a code-generation failure the driver test sees.
            if (invalid_literal_lowered.program && !invalid_literal_lowered.diagnostic_count)
            {
                BUSTER_TEST(arguments, ir_validate_canonical_module(invalid_literal_lowered.program, invalid_literal_lowered.program->modules).error ==
                                           IR_VALIDATION_NONE);
            }
            scratch_end(invalid_literal_temporary);
        }
    }
    // The shapes a binding must refuse: a register the emitter's operand pool
    // does not cover, an assembler label on a local without register storage
    // (where the label would otherwise be read as a symbol rename that means
    // nothing), and a letter constraint that names a different register than
    // the binding does.
    {
        String8 invalid_bound_register_sources[] = {
            S8("long bound(long input) { register long value __asm__(\"r12\") = input; long output;"
               " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
            S8("long bound(long input) { register long value __asm__(\"rbp\") = input; long output;"
               " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
            S8("long bound(long input) { long value __asm__(\"r10\") = input; long output;"
               " __asm__(\"\" : \"=r\"(output) : \"r\"(value)); return output; }\n"),
            S8("long bound(long input) { register long value __asm__(\"r10\") = input; long output;"
               " __asm__(\"\" : \"=r\"(output) : \"a\"(value)); return output; }\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_bound_register_sources); source_index += 1)
        {
            TemporalArena invalid_bound_temporary = scratch_begin(0, 0);
            Target invalid_bound_target = target_native;
            invalid_bound_target.cpu_arch = CPU_ARCH_X86_64;
            CPreprocessResult invalid_bound_tokens = {0};
            CParseResult invalid_bound_parse = {0};
            CIRLowerResult invalid_bound_lowered =
                c_test_lower_source(invalid_bound_temporary.arena, invalid_bound_register_sources[source_index], S8("invalid-bound-register.c"),
                                    invalid_bound_target, &invalid_bound_tokens, &invalid_bound_parse);
            BUSTER_TEST(arguments, invalid_bound_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_bound_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_bound_lowered.diagnostic_count == 1);
            scratch_end(invalid_bound_temporary);
        }
    }
    // A libc spells the variable-argument list type under more than one
    // typedef name -- musl declares its public prototypes with
    // __isoc_va_list and its definitions with va_list -- so all of the
    // spellings have to be one type. Two type ids would make the definition
    // conflict with its own prototype.
    {
        TemporalArena va_list_alias_temporary = scratch_begin(0, 0);
        CPreprocessResult va_list_alias_tokens = {0};
        CParseResult va_list_alias_parse = {0};
        CIRLowerResult va_list_alias_lowered = c_test_lower_source(
            va_list_alias_temporary.arena,
            S8("typedef __builtin_va_list va_list;"
               "typedef __builtin_va_list __isoc_va_list;"
               "int aliased(char const* format, __isoc_va_list list);"
               "int aliased(char const* format, va_list list) { (void)format; (void)list; return 0; }\n"),
            S8("va-list-alias.c"), target_native, &va_list_alias_tokens, &va_list_alias_parse);
        BUSTER_TEST(arguments, va_list_alias_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, va_list_alias_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, va_list_alias_lowered.diagnostic_count == 0);
        scratch_end(va_list_alias_temporary);
    }
    {
        TemporalArena aarch64_tied_assembly_temporary = scratch_begin(0, 0);
        Target aarch64_tied_target = target_native;
        aarch64_tied_target.cpu_arch = CPU_ARCH_AARCH64;
        CPreprocessResult aarch64_tied_tokens = {0};
        CParseResult aarch64_tied_parse = {0};
        CIRLowerResult aarch64_tied_lowered = c_test_lower_source(
            aarch64_tied_assembly_temporary.arena,
            S8("int aarch64_tied(int input) { int output; __asm__(\"\" : \"=r\"(output) : \"0\"(input)); return output; }\n"), S8("aarch64-tied.c"),
            aarch64_tied_target, &aarch64_tied_tokens, &aarch64_tied_parse);
        BUSTER_TEST(arguments, aarch64_tied_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_tied_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_tied_lowered.diagnostic_count == 0);
        if (aarch64_tied_lowered.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(aarch64_tied_lowered.program, aarch64_tied_lowered.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(aarch64_tied_assembly_temporary);
    }
    {
        TemporalArena aarch64_fixed_assembly_temporary = scratch_begin(0, 0);
        Target aarch64_fixed_target = target_native;
        aarch64_fixed_target.cpu_arch = CPU_ARCH_AARCH64;
        CPreprocessResult aarch64_fixed_tokens = {0};
        CParseResult aarch64_fixed_parse = {0};
        CIRLowerResult aarch64_fixed_lowered = c_test_lower_source(
            aarch64_fixed_assembly_temporary.arena,
            S8("int aarch64_legacy_fixed(int input) { int output; __asm__(\"\" : \"=a\"(output) : \"a\"(input)); return output; }\n"),
            S8("aarch64-legacy-fixed.c"), aarch64_fixed_target, &aarch64_fixed_tokens, &aarch64_fixed_parse);
        BUSTER_TEST(arguments, aarch64_fixed_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_fixed_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_fixed_lowered.diagnostic_count == 0);
        if (aarch64_fixed_lowered.program)
        {
            IrModule* module = aarch64_fixed_lowered.program->modules;
            IrFunction* function = c_test_find_ir_function(module, S8("aarch64_legacy_fixed"));
            IrInstruction* assembly = 0;
            if (function)
            {
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    if (function->instructions[instruction_index].opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        assembly = function->instructions + instruction_index;
                        break;
                    }
                }
            }
            BUSTER_TEST(arguments, function && function->state == IR_FUNCTION_LOWERED && assembly && assembly->operand_count == 2);
            if (assembly)
            {
                BUSTER_TEST(arguments, (assembly->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
                BUSTER_TEST(arguments, (assembly->immediates[1] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A);
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(aarch64_fixed_lowered.program, module).error == IR_VALIDATION_NONE);
        }

        CPreprocessResult aarch64_fixed_tie_tokens = {0};
        CParseResult aarch64_fixed_tie_parse = {0};
        CIRLowerResult aarch64_fixed_tie_lowered = c_test_lower_source(
            aarch64_fixed_assembly_temporary.arena,
            S8("int aarch64_fixed_tie(int input) { int output; __asm__(\"\" : \"=a\"(output) : \"0\"(input)); return output; }\n"),
            S8("aarch64-fixed-tie.c"), aarch64_fixed_target, &aarch64_fixed_tie_tokens, &aarch64_fixed_tie_parse);
        BUSTER_TEST(arguments, aarch64_fixed_tie_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_fixed_tie_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, aarch64_fixed_tie_lowered.diagnostic_count == 1);
        scratch_end(aarch64_fixed_assembly_temporary);
    }
    {
        TemporalArena label_escape_temporary = scratch_begin(0, 0);
        CPreprocessResult label_escape_tokens = c_preprocess(
            label_escape_temporary.arena,
            S8("struct label_pair { void *first; void *second; };"
               "union label_union { void *pointer; int integer; };"
               "extern void take_pair(struct label_pair);"
               "extern void take_union(union label_union);"
               "extern void take_pointer(void **);"
               "int invalid_pair_call(void) { struct label_pair pair = {&&pair_a, &&pair_b}; take_pair(pair); return 0; pair_a: return 1; pair_b: return 2; }"
               "struct label_pair invalid_pair_return(void) { struct label_pair pair = {&&return_a, &&return_b}; return pair; return_a: return pair; return_b: return pair; }"
               "int invalid_pair_address(void) { struct label_pair pair = {&&address_a, &&address_b}; take_pointer((void **)&pair); return 0; address_a: return 1; address_b: return 2; }"
               "int invalid_union_call(void) { union label_union value = { .pointer = &&union_a }; take_union(value); return 0; union_a: return 1; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult label_escape_parse = c_parse(label_escape_temporary.arena, label_escape_tokens);
        CIRLowerResult label_escape_lowered = c_lower_to_ir(label_escape_temporary.arena, S8("label-escape.c"), label_escape_tokens, label_escape_parse, target_native);
        BUSTER_TEST(arguments, label_escape_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_escape_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_escape_lowered.diagnostic_count >= 4);
        scratch_end(label_escape_temporary);
    }
    {
        TemporalArena legal_label_pointer_address_temporary = scratch_begin(0, 0);
        CPreprocessResult legal_label_pointer_address_tokens = c_preprocess(
            legal_label_pointer_address_temporary.arena,
            S8("extern void take_pointer(void **);"
               "int legal_label_pointer_address(void) { void *pointer = &&target; take_pointer(&pointer); return 0; target: return 1; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult legal_label_pointer_address_parse = c_parse(legal_label_pointer_address_temporary.arena, legal_label_pointer_address_tokens);
        CIRLowerResult legal_label_pointer_address_lowered = c_lower_to_ir(legal_label_pointer_address_temporary.arena, S8("legal-label-pointer-address.c"),
                                                                            legal_label_pointer_address_tokens, legal_label_pointer_address_parse, target_native);
        BUSTER_TEST(arguments, legal_label_pointer_address_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, legal_label_pointer_address_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, legal_label_pointer_address_lowered.diagnostic_count == 0);
        scratch_end(legal_label_pointer_address_temporary);
    }
    {
        TemporalArena label_metadata_growth_temporary = scratch_begin(0, 0);
        CPreprocessResult label_metadata_growth_tokens = c_preprocess(
            label_metadata_growth_temporary.arena,
            S8("int label_metadata_capacity_growth(int selector) {"
               " int padding[256] = { 0 };"
               " void *table[2] = { &&zero, &&one };"
               " goto *table[selector & 1];"
               "zero: return padding[0];"
               "one: return padding[1];"
               "}\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult label_metadata_growth_parse = c_parse(label_metadata_growth_temporary.arena, label_metadata_growth_tokens);
        CIRLowerResult label_metadata_growth_lowered = c_lower_to_ir(label_metadata_growth_temporary.arena, S8("label-metadata-growth.c"),
                                                                      label_metadata_growth_tokens, label_metadata_growth_parse, target_native);
        BUSTER_TEST(arguments, label_metadata_growth_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_metadata_growth_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_metadata_growth_lowered.diagnostic_count == 0);
        BUSTER_TEST(arguments, label_metadata_growth_lowered.program != 0);
        if (label_metadata_growth_lowered.program)
        {
            IrModule* module = &label_metadata_growth_lowered.program->modules[0];
            IrFunction* function = c_test_find_ir_function(module, S8("label_metadata_capacity_growth"));
            BUSTER_TEST(arguments, function != 0);
            if (function)
            {
                CDeclaration declaration = label_metadata_growth_parse.declarations[0];
                u64 initial_value_capacity = (u64)declaration.body_token_count * 3 + (u64)declaration.parameter_count * 4 + 16;
                IrInstruction* indirect_branch = 0;
                u32 label_address_count = 0;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    label_address_count += instruction->opcode == IR_OPCODE_LABEL_ADDRESS;
                    if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                    {
                        indirect_branch = instruction;
                    }
                }
                BUSTER_TEST(arguments, function->value_count > initial_value_capacity);
                BUSTER_TEST(arguments, function->value_capacity > initial_value_capacity);
                BUSTER_TEST(arguments, label_address_count == 2);
                BUSTER_TEST(arguments, indirect_branch != 0);
                if (indirect_branch)
                {
                    BUSTER_TEST(arguments, indirect_branch->operand_count == 1 && indirect_branch->operands != 0);
                    BUSTER_TEST(arguments, indirect_branch->target_count == 2 && indirect_branch->targets != 0);
                    if (indirect_branch->operand_count == 1 && indirect_branch->operands && indirect_branch->target_count == 2 && indirect_branch->targets &&
                        indirect_branch->operands[0].value < function->value_count)
                    {
                        IrValueLabelMetadata target = ir_value_label_metadata(function, indirect_branch->operands[0]);
                        BUSTER_TEST(arguments, ir_label_provenance_valid(&target));
                        BUSTER_TEST(arguments, target.label_block_count == 2 && target.label_blocks != 0);
                        if (target.label_block_count == 2 && target.label_blocks)
                        {
                            for (u32 label_index = 0; label_index < target.label_block_count; label_index += 1)
                            {
                                bool found = false;
                                for (u32 target_index = 0; target_index < indirect_branch->target_count; target_index += 1)
                                {
                                    found |= target.label_blocks[label_index].value == indirect_branch->targets[target_index].value;
                                }
                                BUSTER_TEST(arguments, found);
                            }
                        }
                    }
                }
                BUSTER_TEST(arguments, ir_validate_canonical_module(label_metadata_growth_lowered.program, module).error == IR_VALIDATION_NONE);
            }
        }
        scratch_end(label_metadata_growth_temporary);
    }
    {
        TemporalArena cfg_label_storage_temporary = scratch_begin(0, 0);
        CPreprocessResult cfg_label_storage_tokens = c_preprocess(
            cfg_label_storage_temporary.arena,
            S8("int cfg_label_storage(int selector) { void *table[1]; if (selector) table[0] = &&first; else table[0] = &&second; goto *table[0]; first: return 1; second: return 2; }\n"),
            (CPreprocessOptions){
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = C_PREPROCESS_DIALECT_GNU23,
            });
        CParseResult cfg_label_storage_parse = c_parse(cfg_label_storage_temporary.arena, cfg_label_storage_tokens);
        CIRLowerResult cfg_label_storage_lowered = c_lower_to_ir(cfg_label_storage_temporary.arena, S8("cfg-label-storage.c"), cfg_label_storage_tokens,
                                                                  cfg_label_storage_parse, target_native);
        BUSTER_TEST(arguments, cfg_label_storage_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, cfg_label_storage_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, cfg_label_storage_lowered.diagnostic_count == 0);
        bool cfg_set = false;
        if (cfg_label_storage_lowered.program)
        {
            IrFunction* function = cfg_label_storage_lowered.program->modules[0].functions;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH && instruction->operand_count == 1 && instruction->operands[0].value < function->value_count)
                {
                    IrValueLabelMetadata target = ir_value_label_metadata(function, instruction->operands[0]);
                    cfg_set |= target.label_block_count == 2 && instruction->target_count == 2;
                }
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(cfg_label_storage_lowered.program,
                                                                 &cfg_label_storage_lowered.program->modules[0]).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, cfg_set);
        scratch_end(cfg_label_storage_temporary);
    }
    return result;
}
#endif
