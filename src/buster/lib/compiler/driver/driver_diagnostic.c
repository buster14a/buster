// Driver adapters for the shared diagnostic record. Grammar enums remain
// here; common publication and rendering contain no frontend dependencies.
// Included by driver.c in both unity and modular builds.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_c_diagnostic_code(CDiagnosticKind kind)
{
    static const String8 names[] = {
        [C_DIAGNOSTIC_INVALID_CHARACTER] = S8_INITIALIZER("c.invalid-character"),
        [C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT] = S8_INITIALIZER("c.unterminated-block-comment"),
        [C_DIAGNOSTIC_UNTERMINATED_CHARACTER_LITERAL] = S8_INITIALIZER("c.unterminated-character-literal"),
        [C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL] = S8_INITIALIZER("c.unterminated-string-literal"),
        [C_DIAGNOSTIC_EXPECTED_DIRECTIVE] = S8_INITIALIZER("c.expected-directive"),
        [C_DIAGNOSTIC_EXPECTED_MACRO_NAME] = S8_INITIALIZER("c.expected-macro-name"),
        [C_DIAGNOSTIC_UNSUPPORTED_DIRECTIVE] = S8_INITIALIZER("c.unsupported-directive"),
        [C_DIAGNOSTIC_INVALID_MACRO_DEFINITION] = S8_INITIALIZER("c.invalid-macro-definition"),
        [C_DIAGNOSTIC_INVALID_MACRO_INVOCATION] = S8_INITIALIZER("c.invalid-macro-invocation"),
        [C_DIAGNOSTIC_INVALID_TOKEN_PASTE] = S8_INITIALIZER("c.invalid-token-paste"),
        [C_DIAGNOSTIC_MACRO_EXPANSION_LIMIT] = S8_INITIALIZER("c.macro-expansion-limit"),
        [C_DIAGNOSTIC_INVALID_CONDITIONAL] = S8_INITIALIZER("c.invalid-conditional"),
        [C_DIAGNOSTIC_UNMATCHED_CONDITIONAL] = S8_INITIALIZER("c.unmatched-conditional"),
        [C_DIAGNOSTIC_INVALID_INCLUDE] = S8_INITIALIZER("c.invalid-include"),
        [C_DIAGNOSTIC_INCLUDE_NOT_FOUND] = S8_INITIALIZER("c.include-not-found"),
        [C_DIAGNOSTIC_INCLUDE_DEPTH] = S8_INITIALIZER("c.include-depth"),
        [C_DIAGNOSTIC_INVALID_LINE] = S8_INITIALIZER("c.invalid-line"),
        [C_DIAGNOSTIC_INVALID_ALIGNMENT] = S8_INITIALIZER("c.invalid-alignment"),
        [C_DIAGNOSTIC_INVALID_ATOMIC_TYPE] = S8_INITIALIZER("c.invalid-atomic-type"),
        [C_DIAGNOSTIC_INVALID_FLEXIBLE_ARRAY_MEMBER] = S8_INITIALIZER("c.invalid-flexible-array-member"),
        [C_DIAGNOSTIC_INVALID_BIT_FIELD_WIDTH] = S8_INITIALIZER("c.invalid-bit-field-width"),
        [C_DIAGNOSTIC_EXPECTED_DECLARATION] = S8_INITIALIZER("c.expected-declaration"),
        [C_DIAGNOSTIC_UNMATCHED_DELIMITER] = S8_INITIALIZER("c.unmatched-delimiter"),
        [C_DIAGNOSTIC_CONFLICTING_DECLARATION] = S8_INITIALIZER("c.conflicting-declaration"),
        [C_DIAGNOSTIC_REDEFINITION] = S8_INITIALIZER("c.redefinition"),
        [C_DIAGNOSTIC_UNDECLARED_IDENTIFIER] = S8_INITIALIZER("c.undeclared-identifier"),
        [C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT] = S8_INITIALIZER("c.static-assert-not-constant"),
        [C_DIAGNOSTIC_STATIC_ASSERT_FAILED] = S8_INITIALIZER("c.static-assert-failed"),
        [C_DIAGNOSTIC_INVALID_CONSTEXPR] = S8_INITIALIZER("c.invalid-constexpr"),
        [C_DIAGNOSTIC_INVALID_CLEANUP_ATTRIBUTE] = S8_INITIALIZER("c.invalid-cleanup-attribute"),
        [C_DIAGNOSTIC_INVALID_VOID_OBJECT] = S8_INITIALIZER("c.invalid-void-object"),
        [C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS] = S8_INITIALIZER("c.unsupported-semantics"),
        [C_DIAGNOSTIC_PREPROCESSOR_ERROR] = S8_INITIALIZER("c.preprocessor-error"),
        [C_DIAGNOSTIC_PREPROCESSOR_WARNING] = S8_INITIALIZER("c.preprocessor-warning"),
        [C_DIAGNOSTIC_TOKEN_TOO_LONG] = S8_INITIALIZER("c.token-too-long"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == C_DIAGNOSTIC_KIND_COUNT);
    return (u32)kind < (u32)BUSTER_ARRAY_LENGTH(names) ? names[kind] : S8("not-applicable");
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_assembly_diagnostic_code(AssemblyDiagnosticKind kind)
{
    static const String8 names[] = {
        [ASSEMBLY_DIAGNOSTIC_INVALID_TARGET] = S8_INITIALIZER("assembly.invalid-target"),
        [ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX] = S8_INITIALIZER("assembly.invalid-syntax"),
        [ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT] = S8_INITIALIZER("assembly.invalid-statement"),
        [ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION] = S8_INITIALIZER("assembly.invalid-expression"),
        [ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION] = S8_INITIALIZER("assembly.unknown-instruction"),
        [ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS] = S8_INITIALIZER("assembly.invalid-operands"),
        [ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE] = S8_INITIALIZER("assembly.unsupported-feature"),
        [ASSEMBLY_DIAGNOSTIC_DUPLICATE_SYMBOL] = S8_INITIALIZER("assembly.duplicate-symbol"),
        [ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE] = S8_INITIALIZER("assembly.branch-out-of-range"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == ASSEMBLY_DIAGNOSTIC_COUNT);
    return (u32)kind < (u32)BUSTER_ARRAY_LENGTH(names) ? names[kind] : S8("not-applicable");
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_opcode_name(IrOpcode kind)
{
    static const String8 names[] = {
        [IR_OPCODE_ARGUMENT] = S8_INITIALIZER("argument"),
        [IR_OPCODE_LOCAL] = S8_INITIALIZER("local"),
        [IR_OPCODE_STACK_ALLOCATE] = S8_INITIALIZER("stack-allocate"),
        [IR_OPCODE_STACK_SAVE] = S8_INITIALIZER("stack-save"),
        [IR_OPCODE_STACK_RESTORE] = S8_INITIALIZER("stack-restore"),
        [IR_OPCODE_GLOBAL] = S8_INITIALIZER("global"),
        [IR_OPCODE_LOAD] = S8_INITIALIZER("load"),
        [IR_OPCODE_STORE] = S8_INITIALIZER("store"),
        [IR_OPCODE_ATOMIC_LOAD] = S8_INITIALIZER("atomic-load"),
        [IR_OPCODE_ATOMIC_STORE] = S8_INITIALIZER("atomic-store"),
        [IR_OPCODE_ATOMIC_READ_MODIFY_WRITE] = S8_INITIALIZER("atomic-read-modify-write"),
        [IR_OPCODE_ATOMIC_COMPARE_EXCHANGE] = S8_INITIALIZER("atomic-compare-exchange"),
        [IR_OPCODE_ATOMIC_FENCE] = S8_INITIALIZER("atomic-fence"),
        [IR_OPCODE_CLEAR_INSTRUCTION_CACHE] = S8_INITIALIZER("clear-instruction-cache"),
        [IR_OPCODE_CONSTANT_INTEGER] = S8_INITIALIZER("constant-integer"),
        [IR_OPCODE_CONSTANT_FLOAT] = S8_INITIALIZER("constant-float"),
        [IR_OPCODE_CONSTANT_STRING] = S8_INITIALIZER("constant-string"),
        [IR_OPCODE_UNDEFINED] = S8_INITIALIZER("undefined"),
        [IR_OPCODE_FUNCTION] = S8_INITIALIZER("function"),
        [IR_OPCODE_ARRAY] = S8_INITIALIZER("array"),
        [IR_OPCODE_AGGREGATE] = S8_INITIALIZER("aggregate"),
        [IR_OPCODE_LENGTH] = S8_INITIALIZER("length"),
        [IR_OPCODE_INDEX] = S8_INITIALIZER("index"),
        [IR_OPCODE_SLICE] = S8_INITIALIZER("slice"),
        [IR_OPCODE_FIELD] = S8_INITIALIZER("field"),
        [IR_OPCODE_ENUM] = S8_INITIALIZER("enum"),
        [IR_OPCODE_CALL] = S8_INITIALIZER("call"),
        [IR_OPCODE_CAST] = S8_INITIALIZER("cast"),
        [IR_OPCODE_ADDRESS_OF] = S8_INITIALIZER("address-of"),
        [IR_OPCODE_DEREFERENCE] = S8_INITIALIZER("dereference"),
        [IR_OPCODE_UNARY] = S8_INITIALIZER("unary"),
        [IR_OPCODE_BINARY] = S8_INITIALIZER("binary"),
        [IR_OPCODE_REVERSE] = S8_INITIALIZER("reverse"),
        [IR_OPCODE_VA_START] = S8_INITIALIZER("va-start"),
        [IR_OPCODE_VA_COPY] = S8_INITIALIZER("va-copy"),
        [IR_OPCODE_VA_END] = S8_INITIALIZER("va-end"),
        [IR_OPCODE_VA_ARG] = S8_INITIALIZER("va-arg"),
        [IR_OPCODE_INLINE_ASSEMBLY] = S8_INITIALIZER("inline-assembly"),
        [IR_OPCODE_SIMD] = S8_INITIALIZER("simd"),
        [IR_OPCODE_LABEL_ADDRESS] = S8_INITIALIZER("label-address"),
        [IR_OPCODE_BRANCH] = S8_INITIALIZER("branch"),
        [IR_OPCODE_BRANCH_IF] = S8_INITIALIZER("branch-if"),
        [IR_OPCODE_SWITCH] = S8_INITIALIZER("switch"),
        [IR_OPCODE_INDIRECT_BRANCH] = S8_INITIALIZER("indirect-branch"),
        [IR_OPCODE_RETURN] = S8_INITIALIZER("return"),
        [IR_OPCODE_DEBUG_TRAP] = S8_INITIALIZER("debug-trap"),
        [IR_OPCODE_UNREACHABLE] = S8_INITIALIZER("unreachable"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == IR_OPCODE_COUNT);
    return (u32)kind < (u32)BUSTER_ARRAY_LENGTH(names) ? names[kind] : S8("not-applicable");
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_binary_name(IrBinaryOperation kind)
{
    static const String8 names[] = {
        [IR_BINARY_INTEGER_ADD] = S8_INITIALIZER("integer-add"),
        [IR_BINARY_INTEGER_SUBTRACT] = S8_INITIALIZER("integer-subtract"),
        [IR_BINARY_INTEGER_MULTIPLY] = S8_INITIALIZER("integer-multiply"),
        [IR_BINARY_SIGNED_DIVIDE] = S8_INITIALIZER("signed-divide"),
        [IR_BINARY_UNSIGNED_DIVIDE] = S8_INITIALIZER("unsigned-divide"),
        [IR_BINARY_FLOAT_ADD] = S8_INITIALIZER("float-add"),
        [IR_BINARY_FLOAT_SUBTRACT] = S8_INITIALIZER("float-subtract"),
        [IR_BINARY_FLOAT_MULTIPLY] = S8_INITIALIZER("float-multiply"),
        [IR_BINARY_FLOAT_DIVIDE] = S8_INITIALIZER("float-divide"),
        [IR_BINARY_SIGNED_REMAINDER] = S8_INITIALIZER("signed-remainder"),
        [IR_BINARY_UNSIGNED_REMAINDER] = S8_INITIALIZER("unsigned-remainder"),
        [IR_BINARY_SHIFT_LEFT] = S8_INITIALIZER("shift-left"),
        [IR_BINARY_SIGNED_SHIFT_RIGHT] = S8_INITIALIZER("signed-shift-right"),
        [IR_BINARY_UNSIGNED_SHIFT_RIGHT] = S8_INITIALIZER("unsigned-shift-right"),
        [IR_BINARY_INTEGER_BITWISE_AND] = S8_INITIALIZER("integer-bitwise-and"),
        [IR_BINARY_INTEGER_BITWISE_OR] = S8_INITIALIZER("integer-bitwise-or"),
        [IR_BINARY_INTEGER_BITWISE_XOR] = S8_INITIALIZER("integer-bitwise-xor"),
        [IR_BINARY_BOOLEAN_AND] = S8_INITIALIZER("boolean-and"),
        [IR_BINARY_BOOLEAN_OR] = S8_INITIALIZER("boolean-or"),
        [IR_BINARY_INTEGER_EQUAL] = S8_INITIALIZER("integer-equal"),
        [IR_BINARY_INTEGER_NOT_EQUAL] = S8_INITIALIZER("integer-not-equal"),
        [IR_BINARY_FLOAT_EQUAL] = S8_INITIALIZER("float-equal"),
        [IR_BINARY_FLOAT_NOT_EQUAL] = S8_INITIALIZER("float-not-equal"),
        [IR_BINARY_POINTER_EQUAL] = S8_INITIALIZER("pointer-equal"),
        [IR_BINARY_POINTER_NOT_EQUAL] = S8_INITIALIZER("pointer-not-equal"),
        [IR_BINARY_BOOLEAN_EQUAL] = S8_INITIALIZER("boolean-equal"),
        [IR_BINARY_BOOLEAN_NOT_EQUAL] = S8_INITIALIZER("boolean-not-equal"),
        [IR_BINARY_SIGNED_LESS] = S8_INITIALIZER("signed-less"),
        [IR_BINARY_SIGNED_LESS_EQUAL] = S8_INITIALIZER("signed-less-equal"),
        [IR_BINARY_SIGNED_GREATER] = S8_INITIALIZER("signed-greater"),
        [IR_BINARY_SIGNED_GREATER_EQUAL] = S8_INITIALIZER("signed-greater-equal"),
        [IR_BINARY_UNSIGNED_LESS] = S8_INITIALIZER("unsigned-less"),
        [IR_BINARY_UNSIGNED_LESS_EQUAL] = S8_INITIALIZER("unsigned-less-equal"),
        [IR_BINARY_UNSIGNED_GREATER] = S8_INITIALIZER("unsigned-greater"),
        [IR_BINARY_UNSIGNED_GREATER_EQUAL] = S8_INITIALIZER("unsigned-greater-equal"),
        [IR_BINARY_FLOAT_LESS] = S8_INITIALIZER("float-less"),
        [IR_BINARY_FLOAT_LESS_EQUAL] = S8_INITIALIZER("float-less-equal"),
        [IR_BINARY_FLOAT_GREATER] = S8_INITIALIZER("float-greater"),
        [IR_BINARY_FLOAT_GREATER_EQUAL] = S8_INITIALIZER("float-greater-equal"),
        [IR_BINARY_RANGE] = S8_INITIALIZER("range"),
        [IR_BINARY_VECTOR_INTEGER_ADD] = S8_INITIALIZER("vector-integer-add"),
        [IR_BINARY_VECTOR_INTEGER_SUBTRACT] = S8_INITIALIZER("vector-integer-subtract"),
        [IR_BINARY_VECTOR_INTEGER_MULTIPLY] = S8_INITIALIZER("vector-integer-multiply"),
        [IR_BINARY_VECTOR_SIGNED_DIVIDE] = S8_INITIALIZER("vector-signed-divide"),
        [IR_BINARY_VECTOR_UNSIGNED_DIVIDE] = S8_INITIALIZER("vector-unsigned-divide"),
        [IR_BINARY_VECTOR_FLOAT_ADD] = S8_INITIALIZER("vector-float-add"),
        [IR_BINARY_VECTOR_FLOAT_SUBTRACT] = S8_INITIALIZER("vector-float-subtract"),
        [IR_BINARY_VECTOR_FLOAT_MULTIPLY] = S8_INITIALIZER("vector-float-multiply"),
        [IR_BINARY_VECTOR_FLOAT_DIVIDE] = S8_INITIALIZER("vector-float-divide"),
        [IR_BINARY_VECTOR_SIGNED_REMAINDER] = S8_INITIALIZER("vector-signed-remainder"),
        [IR_BINARY_VECTOR_UNSIGNED_REMAINDER] = S8_INITIALIZER("vector-unsigned-remainder"),
        [IR_BINARY_VECTOR_SHIFT_LEFT] = S8_INITIALIZER("vector-shift-left"),
        [IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT] = S8_INITIALIZER("vector-signed-shift-right"),
        [IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT] = S8_INITIALIZER("vector-unsigned-shift-right"),
        [IR_BINARY_VECTOR_INTEGER_BITWISE_AND] = S8_INITIALIZER("vector-integer-bitwise-and"),
        [IR_BINARY_VECTOR_INTEGER_BITWISE_OR] = S8_INITIALIZER("vector-integer-bitwise-or"),
        [IR_BINARY_VECTOR_INTEGER_BITWISE_XOR] = S8_INITIALIZER("vector-integer-bitwise-xor"),
        [IR_BINARY_VECTOR_INTEGER_EQUAL] = S8_INITIALIZER("vector-integer-equal"),
        [IR_BINARY_VECTOR_INTEGER_NOT_EQUAL] = S8_INITIALIZER("vector-integer-not-equal"),
        [IR_BINARY_VECTOR_SIGNED_LESS] = S8_INITIALIZER("vector-signed-less"),
        [IR_BINARY_VECTOR_SIGNED_LESS_EQUAL] = S8_INITIALIZER("vector-signed-less-equal"),
        [IR_BINARY_VECTOR_SIGNED_GREATER] = S8_INITIALIZER("vector-signed-greater"),
        [IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL] = S8_INITIALIZER("vector-signed-greater-equal"),
        [IR_BINARY_VECTOR_UNSIGNED_LESS] = S8_INITIALIZER("vector-unsigned-less"),
        [IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL] = S8_INITIALIZER("vector-unsigned-less-equal"),
        [IR_BINARY_VECTOR_UNSIGNED_GREATER] = S8_INITIALIZER("vector-unsigned-greater"),
        [IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL] = S8_INITIALIZER("vector-unsigned-greater-equal"),
        [IR_BINARY_VECTOR_FLOAT_EQUAL] = S8_INITIALIZER("vector-float-equal"),
        [IR_BINARY_VECTOR_FLOAT_NOT_EQUAL] = S8_INITIALIZER("vector-float-not-equal"),
        [IR_BINARY_VECTOR_FLOAT_LESS] = S8_INITIALIZER("vector-float-less"),
        [IR_BINARY_VECTOR_FLOAT_LESS_EQUAL] = S8_INITIALIZER("vector-float-less-equal"),
        [IR_BINARY_VECTOR_FLOAT_GREATER] = S8_INITIALIZER("vector-float-greater"),
        [IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL] = S8_INITIALIZER("vector-float-greater-equal"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == IR_BINARY_COUNT);
    return (u32)kind < (u32)BUSTER_ARRAY_LENGTH(names) ? names[kind] : S8("not-applicable");
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_unary_name(IrUnaryOperation kind)
{
    static const String8 names[] = {
        [IR_UNARY_INTEGER_NEGATE] = S8_INITIALIZER("integer-negate"),
        [IR_UNARY_FLOAT_NEGATE] = S8_INITIALIZER("float-negate"),
        [IR_UNARY_INTEGER_BITWISE_NOT] = S8_INITIALIZER("integer-bitwise-not"),
        [IR_UNARY_INTEGER_COUNT_LEADING_ZEROS] = S8_INITIALIZER("integer-count-leading-zeros"),
        [IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS] = S8_INITIALIZER("integer-count-trailing-zeros"),
        [IR_UNARY_INTEGER_POPULATION_COUNT] = S8_INITIALIZER("integer-population-count"),
        [IR_UNARY_BOOLEAN_NOT] = S8_INITIALIZER("boolean-not"),
        [IR_UNARY_VECTOR_INTEGER_NEGATE] = S8_INITIALIZER("vector-integer-negate"),
        [IR_UNARY_VECTOR_FLOAT_NEGATE] = S8_INITIALIZER("vector-float-negate"),
        [IR_UNARY_VECTOR_INTEGER_BITWISE_NOT] = S8_INITIALIZER("vector-integer-bitwise-not"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == IR_UNARY_COUNT);
    return (u32)kind < (u32)BUSTER_ARRAY_LENGTH(names) ? names[kind] : S8("not-applicable");
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_codegen_error_name(CodegenError kind)
{
    static const String8 names[] = {
        [CODEGEN_ERROR_NONE] = S8_INITIALIZER("codegen.none"),
        [CODEGEN_ERROR_UNSUPPORTED_TARGET] = S8_INITIALIZER("codegen.unsupported-target"),
        [CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION] = S8_INITIALIZER("codegen.unsupported-instruction"),
        [CODEGEN_ERROR_UNSUPPORTED_ABI] = S8_INITIALIZER("codegen.unsupported-abi"),
        [CODEGEN_ERROR_INVALID_IR] = S8_INITIALIZER("codegen.invalid-ir"),
        [CODEGEN_ERROR_CAPACITY] = S8_INITIALIZER("codegen.capacity"),
        [CODEGEN_ERROR_EXECUTABLE_MEMORY] = S8_INITIALIZER("codegen.executable-memory"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == CODEGEN_ERROR_COUNT);
    return (u32)kind < (u32)BUSTER_ARRAY_LENGTH(names) ? names[kind] : S8("not-applicable");
}

BUSTER_GLOBAL_LOCAL void compiler_driver_collect_diagnostic(CompilerDriverDiagnosticCollector* collector, CompilerDiagnostic diagnostic)
{
    if (collector && collector->arena && !collector->suppress_records)
    {
        if (collector->record_count == collector->record_capacity)
        {
            u32 capacity = collector->record_capacity ? collector->record_capacity * 2 : 8;
            CompilerDiagnostic* records = arena_allocate(collector->arena, CompilerDiagnostic, capacity);
            if (collector->record_count)
            {
                memcpy(records, collector->records, sizeof(*records) * collector->record_count);
            }
            collector->records = records;
            collector->record_capacity = capacity;
        }
        collector->records[collector->record_count++] = compiler_diagnostic_copy(collector->arena, diagnostic);
    }
}

BUSTER_GLOBAL_LOCAL CompilerDiagnostic compiler_driver_c_diagnostic(CPreprocessResult const* preprocess, CDiagnostic diagnostic, String8 fallback_path)
{
    CompilerDiagnostic result = {
        .code = compiler_driver_c_diagnostic_code(diagnostic.kind),
        .message = diagnostic.message,
        .severity = diagnostic.severity == C_DIAGNOSTIC_WARNING ? COMPILER_DIAGNOSTIC_WARNING : COMPILER_DIAGNOSTIC_ERROR,
        .primary = {
            .path = diagnostic.location.file < preprocess->file_count ? preprocess->files[diagnostic.location.file] : fallback_path,
            .range = {.source = {.value = diagnostic.location.file}, .offset = diagnostic.location.map_offset},
            .position = {.source = diagnostic.location.file, .offset = diagnostic.location.offset,
                         .line = diagnostic.location.line, .column = diagnostic.location.column},
            .has_range = preprocess->recovery != 0 && diagnostic.location.line != 0,
        },
    };
    if (preprocess->recovery)
    {
        IrSourcePosition original = ir_source_map_original_position(&preprocess->recovery->map, diagnostic.location.map_offset);
        if (original.line && original.source < preprocess->file_count)
        {
            result.primary.original_position = original;
            result.primary.original_path = preprocess->files[original.source];
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDiagnostic compiler_driver_assembly_diagnostic(String8 source, String8 path, AssemblyDiagnostic diagnostic)
{
    CompilerDiagnostic result = {
        .code = compiler_driver_assembly_diagnostic_code(diagnostic.kind), .message = diagnostic.message,
        .severity = COMPILER_DIAGNOSTIC_ERROR,
        .primary = {.path = path, .range = {.source = IR_SOURCE_ID_INVALID},
                    .position = {.source = UINT32_MAX, .line = diagnostic.line, .column = diagnostic.column}},
    };
    u64 offset = 0;
    u32 line = 1;
    while (offset < source.length && line < diagnostic.line)
    {
        line += source.pointer[offset] == '\n';
        offset += 1;
    }
    u64 end = offset;
    while (end < source.length && source.pointer[end] != '\n') end += 1;
    u64 column = diagnostic.column ? (u64)diagnostic.column - 1 : 0;
    if (diagnostic.line && diagnostic.column && line == diagnostic.line && column <= end - offset && offset + column <= UINT32_MAX)
    {
        offset += column;
        u64 length = BUSTER_MIN((u64)diagnostic.length, end - offset);
        result.primary.range.offset = (u32)offset;
        result.primary.range.length = (u32)length;
        result.primary.position.offset = (u32)offset;
        result.primary.original_position = result.primary.position;
        result.primary.original_path = path;
        result.primary.has_range = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDiagnosticLocation compiler_driver_backend_location(IrProgram* program, IrModule* module,
                                                                                  IrFunctionId function, IrInstructionId instruction)
{
    CompilerDiagnosticLocation result = {.range = {.source = IR_SOURCE_ID_INVALID}};
    if (function.value < module->function_count)
    {
        IrFunction* owner = module->functions + function.value;
        result.range = instruction.value < owner->instruction_count ? owner->instruction_canonical_sources[instruction.value] : owner->source;
        result.position = ir_source_position(program, result.range);
        IrSource* source = ir_source_from_id(&program->sources, (IrSourceId){.value = result.position.source});
        if (source) result.path = source->path;
        result.has_range = result.position.line != 0;
        result.original_position = ir_source_map_original_position(&program->source_map, result.range.offset);
        IrSource* original = ir_source_from_id(&program->sources, (IrSourceId){.value = result.original_position.source});
        if (result.original_position.line && original) result.original_path = original->path;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDiagnosticBackend compiler_driver_backend_context(Arena* arena, CompilerDriverInvocation invocation,
                                                                                 IrProgram* program, IrModule* module, CodegenModule code)
{
    CompilerDiagnosticBackend result = {
        .target = string_format(arena, S8("{S8}-{S8}"), cpu_arch_to_string_os(invocation.target.cpu_arch), operating_system_to_string_os(invocation.target.os)),
        .allocator = codegen_register_allocator_mode_string((CodegenRegisterAllocatorMode)invocation.register_allocator),
        .function = S8("<none>"), .opcode = compiler_driver_opcode_name(code.failed_opcode), .operation = S8("not-applicable"),
        .reason = code.failure_reason, .error_id = (u32)code.error, .function_id = code.failed_function.value,
        .instruction_id = code.failed_instruction.value, .opcode_id = code.failed_opcode < IR_OPCODE_COUNT ? (u32)code.failed_opcode : UINT32_MAX,
        .operation_id = UINT32_MAX,
    };
    if (code.failed_function.value < module->function_count)
    {
        IrFunction* function = module->functions + code.failed_function.value;
        result.function = function->name;
        if (code.failed_instruction.value < function->instruction_count)
        {
            IrInstruction* instruction = function->instructions + code.failed_instruction.value;
            if (instruction->opcode == IR_OPCODE_BINARY)
            {
                result.operation = compiler_driver_binary_name(instruction->binary_operation);
                result.operation_id = (u32)instruction->binary_operation;
            }
            else if (instruction->opcode == IR_OPCODE_UNARY)
            {
                result.operation = compiler_driver_unary_name(instruction->unary_operation);
                result.operation_id = (u32)instruction->unary_operation;
            }
            if (instruction->opcode == IR_OPCODE_CALL && instruction->operand_count && instruction->operands[0].value < function->value_count)
            {
                IrInstructionId definition = function->values[instruction->operands[0].value].definition;
                if (definition.value < function->instruction_count)
                {
                    IrInstruction* reference = function->instructions + definition.value;
                    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, reference->symbol);
                    if (reference->opcode == IR_OPCODE_FUNCTION && symbol) result.referenced_symbol = symbol->link_name;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_error_code(CompilerDriverError error)
{
    static const String8 names[] = {
        [COMPILER_DRIVER_ERROR_NONE] = S8_INITIALIZER("driver.none"),
        [COMPILER_DRIVER_ERROR_ARGUMENT] = S8_INITIALIZER("driver.argument"),
        [COMPILER_DRIVER_ERROR_INVALID_INPUT] = S8_INITIALIZER("driver.invalid-input"),
        [COMPILER_DRIVER_ERROR_FILE_READ] = S8_INITIALIZER("driver.file-read"),
        [COMPILER_DRIVER_ERROR_TOKENIZE] = S8_INITIALIZER("driver.tokenize"),
        [COMPILER_DRIVER_ERROR_PARSE] = S8_INITIALIZER("driver.parse"),
        [COMPILER_DRIVER_ERROR_ANALYSIS] = S8_INITIALIZER("driver.analysis"),
        [COMPILER_DRIVER_ERROR_IR] = S8_INITIALIZER("driver.ir"),
        [COMPILER_DRIVER_ERROR_LLVM_BITCODE] = S8_INITIALIZER("driver.llvm-bitcode"),
        [COMPILER_DRIVER_ERROR_CODEGEN] = S8_INITIALIZER("driver.codegen"),
        [COMPILER_DRIVER_ERROR_WASM64] = S8_INITIALIZER("driver.wasm64"),
        [COMPILER_DRIVER_ERROR_GPU] = S8_INITIALIZER("driver.gpu"),
        [COMPILER_DRIVER_ERROR_EBPF] = S8_INITIALIZER("driver.ebpf"),
        [COMPILER_DRIVER_ERROR_OBJECT] = S8_INITIALIZER("driver.object"),
        [COMPILER_DRIVER_ERROR_LINK] = S8_INITIALIZER("driver.link"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == COMPILER_DRIVER_ERROR_COUNT);
    return (u32)error < (u32)BUSTER_ARRAY_LENGTH(names) ? names[error] : S8("driver.unknown");
}

BUSTER_GLOBAL_LOCAL CompilerDiagnosticLocation compiler_driver_assembly_unsplit_location(String8 split, String8 path, CompilerDiagnosticLocation location)
{
    if (string_equal(location.original_path, path) && location.original_position.line)
    {
        // The root .S lexer input inserts one space after each unquoted
        // dollar. Undo exactly those insertions in the resolved position;
        // include files enter the preprocessor unchanged.
        u32 removed = 0;
        u32 line_removed = 0;
        bool quoted = false;
        char8 quote = 0;
        u64 end = BUSTER_MIN((u64)location.original_position.offset, split.length);
        for (u64 index = 0; index < end; index += 1)
        {
            char8 byte = split.pointer[index];
            if (byte == '\n') line_removed = 0;
            if (quoted)
            {
                if (byte == '\\' && index + 1 < end) index += 1;
                else if (byte == quote) quoted = false;
            }
            else if (byte == '\'' || byte == '"')
            {
                quoted = true;
                quote = byte;
            }
            else if (byte == '$' && index + 1 < end)
            {
                index += 1;
                removed += 1;
                line_removed += 1;
            }
        }
        location.original_position.offset -= removed;
        location.original_position.column -= line_removed;
        location.position.offset -= removed;
        location.position.column -= line_removed;
    }
    return location;
}

// The .S printer emits generated byte offsets. Resolve an assembler error
// back to its token only on failure, retaining a point when expansion means
// that no contiguous physical range exists.
BUSTER_GLOBAL_LOCAL CompilerDiagnostic compiler_driver_preprocessed_assembly_diagnostic(Arena* arena, CPreprocessResult* preprocess,
                                                                                           String8 split, String8 path, CompilerDiagnostic diagnostic)
{
    CSourceLocation location = {0};
    if (diagnostic.primary.has_range)
    {
        compiler_driver_preprocess_text(arena, *preprocess, diagnostic.primary.range.offset, &location);
    }
    if (location.line)
    {
        CompilerDiagnostic mapped = compiler_driver_c_diagnostic(preprocess, (CDiagnostic){.location = location}, path);
        diagnostic.primary = mapped.primary;
        diagnostic.primary = compiler_driver_assembly_unsplit_location(split, path, diagnostic.primary);
    }
    else
    {
        // A synthesized separator has no originating token. Do not present a
        // generated position as a byte range in the user's original file.
        diagnostic.primary = (CompilerDiagnosticLocation){.path = path, .range = {.source = IR_SOURCE_ID_INVALID}};
    }
    return diagnostic;
}


BUSTER_GLOBAL_LOCAL String8 compiler_driver_link_code(LinkError error)
{
    static const String8 names[] = {
        [LINK_ERROR_NONE] = S8_INITIALIZER("link.none"),
        [LINK_ERROR_INVALID_INPUT] = S8_INITIALIZER("link.invalid-input"),
        [LINK_ERROR_TARGET_MISMATCH] = S8_INITIALIZER("link.target-mismatch"),
        [LINK_ERROR_DUPLICATE_SYMBOL] = S8_INITIALIZER("link.duplicate-symbol"),
        [LINK_ERROR_UNRESOLVED_SYMBOL] = S8_INITIALIZER("link.unresolved-symbol"),
        [LINK_ERROR_OBJECT_WRITE] = S8_INITIALIZER("link.object-write"),
        [LINK_ERROR_FILE_WRITE] = S8_INITIALIZER("link.file-write"),
        [LINK_ERROR_PROCESS_SPAWN] = S8_INITIALIZER("link.process-spawn"),
        [LINK_ERROR_PROCESS_FAILED] = S8_INITIALIZER("link.process-failed"),
        [LINK_ERROR_UNSUPPORTED_HOST] = S8_INITIALIZER("link.unsupported-host"),
        [LINK_ERROR_UNSUPPORTED_FEATURE] = S8_INITIALIZER("link.unsupported-feature"),
        [LINK_ERROR_ENTRY_SYMBOL] = S8_INITIALIZER("link.entry-symbol"),
        [LINK_ERROR_RELOCATION] = S8_INITIALIZER("link.relocation"),
        [LINK_ERROR_SYMBOL_VERSION] = S8_INITIALIZER("link.symbol-version"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == LINK_ERROR_COUNT);
    return (u32)error < (u32)BUSTER_ARRAY_LENGTH(names) ? names[error] : S8("link.unknown");
}
