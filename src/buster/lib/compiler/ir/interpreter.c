#include <buster/lib/compiler/ir/interpreter_internal.h>

#include <buster/lib/string.h>

typedef enum IrRuntimeValueKind
{
    IR_RUNTIME_VALUE_SCALAR,
    IR_RUNTIME_VALUE_FUNCTION,
    IR_RUNTIME_VALUE_PLACE,
    IR_RUNTIME_VALUE_AGGREGATE,
    IR_RUNTIME_VALUE_ADDRESS,
    IR_RUNTIME_VALUE_SLICE,
    IR_RUNTIME_VALUE_RANGE,
    IR_RUNTIME_VALUE_VA_LIST,
    IR_RUNTIME_VALUE_KIND_COUNT,
} IrRuntimeValueKind;

typedef struct IrRuntimeObject IrRuntimeObject;
typedef struct IrRuntimeStoredValue IrRuntimeStoredValue;
typedef struct IrRuntimeStoredValueIndex IrRuntimeStoredValueIndex;
typedef struct IrRuntimeValue IrRuntimeValue;
typedef struct IrRuntimeGlobal IrRuntimeGlobal;
typedef struct IrRuntimeContext IrRuntimeContext;
typedef struct IrExecutionFunctionCacheEntry IrExecutionFunctionCacheEntry;
typedef struct IrExecutionFunctionCache IrExecutionFunctionCache;

typedef enum IrExecutionTargetValidation
{
    IR_EXECUTION_TARGET_NOT_VALIDATED,
    IR_EXECUTION_TARGET_VALID,
    IR_EXECUTION_TARGET_INVALID,
} IrExecutionTargetValidation;

struct IrExecutionFunctionCacheEntry
{
    IrExecutionTarget target;
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    IrExecutionTargetValidation validation;
    bool used;
};

struct IrExecutionFunctionCache
{
    IrExecutionFunctionCacheEntry* entries;
    u32 capacity;
    u32 count;
};

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL IrInterpreterTestCounters ir_interpreter_test_counters;

void ir_interpreter_test_counters_reset(void)
{
    ir_interpreter_test_counters = (IrInterpreterTestCounters){0};
}

IrInterpreterTestCounters ir_interpreter_test_counters_read(void)
{
    return ir_interpreter_test_counters;
}
#endif

struct IrRuntimeValue
{
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    AnalysisModuleId type_module;
    AnalysisTypeId type;
    IrRuntimeObject* object;
    IrRuntimeValue* va_arguments;
    u64 bits;
    u64 offset;
    u64 length;
    u64 element_size;
    u32 va_index;
    u32 va_count;
    IrFunctionId label_function;
    IrRuntimeValueKind kind;
    bool initialized;
    bool reversed;
    bool va_ended;
    bool has_label_provenance;
};

struct IrRuntimeObject
{
    IrRuntimeStoredValue* first_stored_value;
    IrRuntimeStoredValueIndex* stored_value_index;
    u8* bytes;
    u8* initialized;
    u64 size;
};

struct IrRuntimeGlobal
{
    IrSymbolId symbol;
    IrRuntimeObject* object;
    u32 object_id;
    u32 reserved;
};

struct IrRuntimeContext
{
    IrRuntimeGlobal* globals;
    IrExecutionFunctionCache functions;
    u32 global_count;
};

struct IrRuntimeStoredValue
{
    IrRuntimeStoredValue* next;
    IrRuntimeValue value;
    u64 offset;
    u64 size;
};

struct IrRuntimeStoredValueIndex
{
    IrRuntimeStoredValue** values;
    u64 count;
    u64 capacity;
};

enum
{
    // A single pointer keeps the common one- and two-entry objects on the
    // linked path.  The flat sorted index starts paying for itself once a
    // clear would otherwise chase this many unrelated arena nodes.
    IR_INTERPRETER_STORED_VALUE_INDEX_THRESHOLD = 8,
};

typedef struct IrExecutionFrame IrExecutionFrame;
struct IrExecutionFrame
{
    AnalysisResult* analysis;
    IrModule* module;
    IrFunction* function;
    IrRuntimeValue* values;
    IrRuntimeValue* arguments;
    IrRuntimeValue* transition_values;
    IrValueId caller_result;
    IrRuntimeContext* runtime;
    IrBlockId block;
    IrInstructionId instruction;
    u32 value_capacity;
    u32 argument_capacity;
    u32 argument_count;
};

BUSTER_GLOBAL_LOCAL bool ir_interpreter_entity_equal(AnalysisEntityId left, AnalysisEntityId right)
{
    return left.module.value == right.module.value && left.index.value == right.index.value;
}

IrExecutionTarget ir_interpreter_function_find(AnalysisProgram* analysis, IrProgram* program, AnalysisEntityId entity,
                                                                  AnalysisInstantiationId instantiation)
{
#if BUSTER_INCLUDE_TESTS
    ir_interpreter_test_counters.function_lookup_count += 1;
#endif
    IrExecutionTarget target = {0};
    if (!analysis || !program || analysis->module_count != program->module_count)
    {
        return target;
    }
    for (u32 module_index = 0; module_index < analysis->module_count; module_index += 1)
    {
        AnalysisResult* candidate_analysis = analysis->module_results[module_index];
        if (!candidate_analysis || candidate_analysis->module.id.value != entity.module.value)
        {
            continue;
        }
        IrModule* candidate_module = program->modules + module_index;
        if (candidate_module->function_count && !candidate_module->functions)
        {
            return target;
        }
        for (u32 function_index = 0; function_index < candidate_module->function_count; function_index += 1)
        {
            IrFunction* candidate = candidate_module->functions + function_index;
            if (ir_interpreter_entity_equal(candidate->entity, entity) && candidate->instantiation.value == instantiation.value)
            {
                target = (IrExecutionTarget){
                    .analysis = candidate_analysis,
                    .program = program,
                    .module = candidate_module,
                    .function = candidate,
                };
                return target;
            }
        }
        return target;
    }
    return target;
}

BUSTER_GLOBAL_LOCAL u32 ir_interpreter_type_width(AnalysisResult* analysis, AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    switch (type->kind)
    {
    case ANALYSIS_TYPE_BOOL:
        return 1;
    case ANALYSIS_TYPE_INTEGER:
        return type->as.integer.bit_width;
    case ANALYSIS_TYPE_FLOAT:
        return type->as.float_bit_width;
    case ANALYSIS_TYPE_ENUM:
    {
        return type->layout.size ? (u32)(type->layout.size * 8) : 32;
    }
    default:
        return 64;
    }
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_mask(u32 width)
{
    return width >= 64 ? UINT64_MAX : width ? (((u64)1 << width) - 1) : 0;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_normalize_integer(AnalysisResult* analysis, AnalysisTypeId type, u64 bits)
{
    return bits & ir_interpreter_mask(ir_interpreter_type_width(analysis, type));
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_type_signed(AnalysisResult* analysis, AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    return type->kind == ANALYSIS_TYPE_INTEGER && type->as.integer.is_signed;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_integer_negative(u64 bits, u32 width)
{
    return width && ((bits >> (width - 1)) & 1);
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_integer_magnitude(u64 bits, u32 width)
{
    u64 mask = ir_interpreter_mask(width);
    bits &= mask;
    return ir_interpreter_integer_negative(bits, width) ? ((~bits) + 1) & mask : bits;
}

BUSTER_GLOBAL_LOCAL s32 ir_interpreter_signed_compare(u64 left, u64 right, u32 width)
{
    u64 mask = ir_interpreter_mask(width);
    left &= mask;
    right &= mask;
    bool left_negative = ir_interpreter_integer_negative(left, width);
    bool right_negative = ir_interpreter_integer_negative(right, width);
    if (left_negative != right_negative)
    {
        return left_negative ? -1 : 1;
    }
    return left < right ? -1 : left > right ? 1 : 0;
}

f64 ir_interpreter_float_read(u64 bits, u32 width)
{
    if (width == 32)
    {
        u32 bits32 = (u32)bits;
        f32 value = 0.0f;
        memcpy(&value, &bits32, sizeof(value));
        return (f64)value;
    }
    f64 value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_float_write(f64 value, u32 width)
{
    u64 bits = 0;
    if (width == 32)
    {
        f32 value32 = (f32)value;
        u32 bits32 = 0;
        memcpy(&bits32, &value32, sizeof(value32));
        bits = bits32;
    }
    else
    {
        memcpy(&bits, &value, sizeof(value));
    }
    return bits;
}

BUSTER_GLOBAL_LOCAL AnalysisResult* ir_interpreter_analysis_find(AnalysisResult* analysis, AnalysisModuleId module)
{
    if (!analysis)
    {
        return 0;
    }
    if (analysis->module.id.value == module.value)
    {
        return analysis;
    }
    if (analysis->program_module_count && !analysis->program_modules)
    {
        return 0;
    }
    for (u32 module_index = 0; module_index < analysis->program_module_count; module_index += 1)
    {
        AnalysisResult* candidate = analysis->program_modules[module_index];
        if (candidate && candidate->module.id.value == module.value)
        {
            return candidate;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_type_id_valid(AnalysisResult* analysis, AnalysisTypeId id)
{
    return analysis && id.value != ANALYSIS_ID_UNDERLYING_INVALID && id.value < analysis->types.count;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_u64_add(u64 left, u64 right, u64* result)
{
    if (left > UINT64_MAX - right)
    {
        return false;
    }
    *result = left + right;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_u64_multiply(u64 left, u64 right, u64* result)
{
    if (right && left > UINT64_MAX / right)
    {
        return false;
    }
    *result = left * right;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_arena_can_allocate(Arena* arena, u64 size)
{
    if (!arena || arena->position > arena->reserved_size || size > arena->reserved_size - arena->position)
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_arena_can_allocate_count(Arena* arena, u64 element_size, u64 count)
{
    u64 size = 0;
    return ir_interpreter_u64_multiply(element_size, count, &size) && ir_interpreter_arena_can_allocate(arena, size);
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_type_size(AnalysisResult* analysis, AnalysisTypeId type_id)
{
    if (!analysis || !ir_interpreter_type_id_valid(analysis, type_id))
    {
        return 0;
    }
    TargetDataLayout data_layout = target_data_layout_is_valid(analysis->data_layout) ? analysis->data_layout : target_data_layout(target_native);
    u64 multiplier = 1;
    AnalysisTypeId current = type_id;
    for (u32 depth = 0; depth < analysis->types.count; depth += 1)
    {
        if (!ir_interpreter_type_id_valid(analysis, current))
        {
            return 0;
        }
        AnalysisType* type = analysis_type_from_id(analysis, current);
        if (type->layout.state == ANALYSIS_LAYOUT_RESOLVED)
        {
            return type->layout.size && multiplier > UINT64_MAX / type->layout.size ? 0 : multiplier * type->layout.size;
        }
        u64 size = 0;
        switch (type->kind)
        {
        case ANALYSIS_TYPE_VOID:
            return 0;
        case ANALYSIS_TYPE_BOOL:
            size = data_layout.boolean.size;
            break;
        case ANALYSIS_TYPE_INTEGER:
            if (!type->as.integer.bit_width || type->as.integer.bit_width > UINT32_MAX - 7)
            {
                return 0;
            }
            size = (type->as.integer.bit_width + 7) / 8;
            break;
        case ANALYSIS_TYPE_FLOAT:
            size = type->as.float_bit_width == data_layout.long_double_type.bit_width ? data_layout.long_double_type.size
                  : type->as.float_bit_width == data_layout.double_type.bit_width ? data_layout.double_type.size
                  : type->as.float_bit_width == data_layout.float_type.bit_width  ? data_layout.float_type.size
                                                                                   : 0;
            break;
        case ANALYSIS_TYPE_VA_LIST:
            size = data_layout.va_list.size;
            break;
        case ANALYSIS_TYPE_ENUM:
            size = data_layout.integer.size;
            break;
        case ANALYSIS_TYPE_POINTER:
        case ANALYSIS_TYPE_FUNCTION:
            size = data_layout.pointer.size;
            break;
        case ANALYSIS_TYPE_SLICE:
        {
            u64 descriptor_size = (u64)data_layout.pointer.size * 2;
            if (!descriptor_size || multiplier > UINT64_MAX / descriptor_size)
            {
                return 0;
            }
            size = descriptor_size;
            break;
        }
        case ANALYSIS_TYPE_RANGE:
            if (multiplier > UINT64_MAX / 2)
            {
                return 0;
            }
            multiplier *= 2;
            current = type->as.element_type;
            continue;
        case ANALYSIS_TYPE_INFERRED_ARRAY:
        case ANALYSIS_TYPE_ARRAY:
            if (!type->as.array.count || multiplier > UINT64_MAX / type->as.array.count)
            {
                return 0;
            }
            multiplier *= type->as.array.count;
            current = type->as.array.element_type;
            continue;
        case ANALYSIS_TYPE_VECTOR:
            if (!type->as.vector.count || multiplier > UINT64_MAX / type->as.vector.count)
            {
                return 0;
            }
            multiplier *= type->as.vector.count;
            current = type->as.vector.element_type;
            continue;
        default:
            return 0;
        }
        return size && multiplier > UINT64_MAX / size ? 0 : multiplier * size;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrRuntimeObject* ir_interpreter_object_create(Arena* arena, u64 size)
{
    u64 storage_size = 0;
    if (!arena || !ir_interpreter_u64_multiply(size, 2, &storage_size) ||
        !ir_interpreter_u64_add(storage_size, sizeof(IrRuntimeObject) + BUSTER_ALIGN_OF(IrRuntimeObject), &storage_size) ||
        !ir_interpreter_arena_can_allocate(arena, storage_size))
    {
        return 0;
    }
    IrRuntimeObject* object = arena_allocate(arena, IrRuntimeObject, 1);
    *object = (IrRuntimeObject){.size = size};
    if (size)
    {
        object->bytes = arena_allocate(arena, u8, size);
        object->initialized = arena_allocate(arena, u8, size);
        for (u64 index = 0; index < size; index += 1)
        {
            object->bytes[index] = 0;
            object->initialized[index] = 0;
        }
    }
    return object;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_memory_write(Arena* arena, IrRuntimeObject* object, u64 offset, u64 size, IrRuntimeValue value);
BUSTER_GLOBAL_LOCAL bool ir_interpreter_object_range_valid(IrRuntimeObject* object, u64 offset, u64 size);
BUSTER_GLOBAL_LOCAL bool ir_interpreter_runtime_globals_initialize(Arena* arena, IrProgram* program, IrRuntimeContext* runtime);
BUSTER_GLOBAL_LOCAL IrRuntimeStoredValue* ir_interpreter_stored_value_find(IrRuntimeObject* object, u64 offset, u64 size);

BUSTER_GLOBAL_LOCAL IrRuntimeGlobal* ir_interpreter_global_find(IrRuntimeContext* runtime, IrSymbolId symbol)
{
    if (!runtime || symbol.value == IR_ID_UNDERLYING_INVALID)
    {
        return 0;
    }
    for (u32 index = 0; index < runtime->global_count; index += 1)
    {
        if (runtime->globals[index].symbol.value == symbol.value)
        {
            return runtime->globals + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void ir_interpreter_global_zero_initialize(IrRuntimeObject* object)
{
    if (!object)
    {
        return;
    }
    for (u64 index = 0; index < object->size; index += 1)
    {
        object->bytes[index] = 0;
        object->initialized[index] = 1;
    }
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_interpreter_label_function_for_symbol(IrProgram* program, IrSymbolId symbol)
{
    if (!program || symbol.value == IR_ID_UNDERLYING_INVALID || (program->module_count && !program->modules))
    {
        return 0;
    }
    IrFunction* result = 0;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        IrModule* module = program->modules + module_index;
        if (module->function_count && !module->functions)
        {
            return 0;
        }
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->symbol.value != symbol.value)
            {
                continue;
            }
            if (result)
            {
                return 0;
            }
            result = function;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_global_relocation_apply(Arena* arena, IrProgram* program, IrRuntimeContext* runtime, IrRuntimeObject* object,
                                                                 IrGlobalRelocation* relocation)
{
    if (!arena || !program || !runtime || !object || !relocation || !program->data_layout.pointer.size)
    {
        return false;
    }
    u64 pointer_size = program->data_layout.pointer.size;
    if (!ir_interpreter_object_range_valid(object, relocation->offset, pointer_size))
    {
        return false;
    }
    if (relocation->is_label_address)
    {
        IrFunction* owner = ir_interpreter_label_function_for_symbol(program, relocation->symbol);
        if (!owner || owner->state != IR_FUNCTION_LOWERED || relocation->addend != 0 || relocation->label_block.value >= owner->block_count)
        {
            return false;
        }
        IrRuntimeValue label = {
            .bits = relocation->label_block.value,
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
            .label_function = owner->id,
            .has_label_provenance = true,
        };
        return ir_interpreter_memory_write(arena, object, relocation->offset, pointer_size, label);
    }
    IrRuntimeGlobal* target_global = ir_interpreter_global_find(runtime, relocation->symbol);
    if (target_global)
    {
        if (!target_global->object || relocation->addend < 0 || (u64)relocation->addend > target_global->object->size)
        {
            return false;
        }
        IrRuntimeValue address = {
            .object = target_global->object,
            .offset = (u64)relocation->addend,
            .kind = IR_RUNTIME_VALUE_ADDRESS,
            .initialized = true,
        };
        return ir_interpreter_memory_write(arena, object, relocation->offset, pointer_size, address);
    }
    IrFunction* target_function = ir_interpreter_label_function_for_symbol(program, relocation->symbol);
    if (!target_function || relocation->addend != 0)
    {
        return false;
    }
    IrRuntimeValue function = {
        .entity = target_function->entity,
        .instantiation = target_function->instantiation,
        .kind = IR_RUNTIME_VALUE_FUNCTION,
        .initialized = true,
    };
    return ir_interpreter_memory_write(arena, object, relocation->offset, pointer_size, function);
}

BUSTER_F_DECL bool ir_interpreter_test_static_label_relocations(Arena* arena)
{
    if (!arena)
    {
        return false;
    }
    IrProgram program = ir_program_initialize(arena, 1, 3, 2, 0);
    program.data_layout.pointer.size = 8;
    IrTypeId void_type = ir_program_add_type(&program, (IrType){
                                                               .kind = IR_TYPE_VOID,
                                                               .layout = {.resolved = true},
                                                           });
    IrTypeId pointer_type = ir_program_add_type(&program, (IrType){
                                                                  .element_type = void_type,
                                                                  .kind = IR_TYPE_POINTER,
                                                                  .layout = {.size = 8, .alignment = 8, .resolved = true},
                                                              });
    IrTypeId table_type = ir_program_add_type(&program, (IrType){
                                                                .element_type = pointer_type,
                                                                .kind = IR_TYPE_ARRAY,
                                                                .element_count = 2,
                                                                .layout = {.size = 16, .alignment = 8, .resolved = true},
                                                            });
    IrSymbolId function_symbol = ir_program_add_symbol(&program, (IrSymbol){
                                                                                .kind = IR_SYMBOL_FUNCTION,
                                                                                .is_definition = true,
                                                                            });
    IrSymbolId global_symbol = ir_program_add_symbol(&program, (IrSymbol){
                                                                             .kind = IR_SYMBOL_DATA,
                                                                             .is_definition = true,
                                                                         });
    if (void_type.value == IR_ID_UNDERLYING_INVALID || pointer_type.value == IR_ID_UNDERLYING_INVALID || table_type.value == IR_ID_UNDERLYING_INVALID ||
        function_symbol.value == IR_ID_UNDERLYING_INVALID || global_symbol.value == IR_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    IrModule* module = program.modules;
    IrBlock* blocks = arena_allocate(arena, IrBlock, 2);
    blocks[0].id = (IrBlockId){.value = 0};
    blocks[1].id = (IrBlockId){.value = 1};
    IrFunction function = {
        .symbol = function_symbol,
        .canonical_type = IR_TYPE_ID_INVALID,
        .id = {.value = 17},
        .blocks = blocks,
        .block_count = 2,
        .state = IR_FUNCTION_LOWERED,
    };
    IrFunction* owner = ir_module_add_function(arena, module, function);
    if (!owner)
    {
        return false;
    }
    u8* bytes = arena_allocate(arena, u8, 16);
    memset(bytes, 0, 16);
    IrGlobalRelocation* relocations = arena_allocate(arena, IrGlobalRelocation, 2);
    relocations[0] = (IrGlobalRelocation){
        .symbol = function_symbol,
        .label_block = {.value = 0},
        .offset = 0,
        .is_label_address = true,
    };
    relocations[1] = (IrGlobalRelocation){
        .symbol = function_symbol,
        .label_block = {.value = 1},
        .offset = 8,
        .is_label_address = true,
    };
    if (!ir_module_add_global(arena, module,
                              (IrGlobal){
                                  .bytes = {.pointer = bytes, .length = 16},
                                  .relocations = relocations,
                                  .symbol = global_symbol,
                                  .type = table_type,
                                  .initializer_kind = IR_GLOBAL_INITIALIZER_BYTES,
                                  .relocation_count = 2,
                              }))
    {
        return false;
    }
    IrRuntimeContext runtime = {0};
    if (!ir_interpreter_runtime_globals_initialize(arena, &program, &runtime))
    {
        return false;
    }
    IrRuntimeGlobal* global = ir_interpreter_global_find(&runtime, global_symbol);
    IrRuntimeStoredValue* first = global ? ir_interpreter_stored_value_find(global->object, 0, 8) : 0;
    IrRuntimeStoredValue* second = global ? ir_interpreter_stored_value_find(global->object, 8, 8) : 0;
    bool labels_valid = first && second && first->value.initialized && second->value.initialized && first->value.has_label_provenance && second->value.has_label_provenance &&
                        first->value.label_function.value == owner->id.value && second->value.label_function.value == owner->id.value && first->value.bits == 0 &&
                        second->value.bits == 1;
    IrGlobalRelocation saved_relocation = relocations[0];
    relocations[0] = (IrGlobalRelocation){
        .symbol = global_symbol,
        .addend = -1,
        .offset = 0,
    };
    IrRuntimeContext negative_addend_runtime = {0};
    bool negative_addend_rejected = !ir_interpreter_runtime_globals_initialize(arena, &program, &negative_addend_runtime);
    relocations[0] = saved_relocation;
    IrGlobal saved_global = module->globals[0];
    module->globals[0].initializer_kind = IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS;
    module->globals[0].initializer_symbol = global_symbol;
    module->globals[0].initializer_addend = -1;
    module->globals[0].relocations = 0;
    module->globals[0].relocation_count = 0;
    IrRuntimeContext negative_initializer_runtime = {0};
    bool negative_initializer_rejected = !ir_interpreter_runtime_globals_initialize(arena, &program, &negative_initializer_runtime);
    module->globals[0] = saved_global;
    return labels_valid && negative_addend_rejected && negative_initializer_rejected;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_runtime_globals_initialize(Arena* arena, IrProgram* program, IrRuntimeContext* runtime)
{
    if (!arena || !program || !runtime)
    {
        return false;
    }
    u32 global_count = 0;
    if (program->module_count && !program->modules)
    {
        return false;
    }
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        IrModule* module = program->modules + module_index;
        if (module->global_count && !module->globals)
        {
            return false;
        }
        if (module->global_count > UINT32_MAX - global_count)
        {
            return false;
        }
        global_count += module->global_count;
    }
    if (!ir_interpreter_arena_can_allocate_count(arena, sizeof(IrRuntimeGlobal), global_count))
    {
        return false;
    }
    runtime->globals = arena_allocate(arena, IrRuntimeGlobal, global_count);
    runtime->global_count = global_count;
    u32 global_index = 0;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        IrModule* module = program->modules + module_index;
        for (u32 module_global_index = 0; module_global_index < module->global_count; module_global_index += 1)
        {
            IrGlobal* global = module->globals + module_global_index;
            IrType* type = ir_type_from_id(&program->types, global->type);
            if (!type || !type->layout.resolved)
            {
                return false;
            }
            IrRuntimeGlobal* runtime_global = runtime->globals + global_index;
            runtime_global->symbol = global->symbol;
            runtime_global->object_id = global_index + 1;
            runtime_global->object = ir_interpreter_object_create(arena, type->layout.size);
            if (!runtime_global->object)
            {
                return false;
            }
            ir_interpreter_global_zero_initialize(runtime_global->object);
            global_index += 1;
        }
    }
    global_index = 0;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        IrModule* module = program->modules + module_index;
        for (u32 module_global_index = 0; module_global_index < module->global_count; module_global_index += 1)
        {
            IrGlobal* global = module->globals + module_global_index;
            IrRuntimeGlobal* runtime_global = runtime->globals + global_index++;
            IrRuntimeObject* object = runtime_global->object;
            switch (global->initializer_kind)
            {
            case IR_GLOBAL_INITIALIZER_ZERO:
            case IR_GLOBAL_INITIALIZER_NONE:
                break;
            case IR_GLOBAL_INITIALIZER_BYTES:
            {
                if (global->bytes.length && !global->bytes.pointer)
                {
                    return false;
                }
                u64 count = BUSTER_MIN(object->size, global->bytes.length);
                if (count)
                {
                    memcpy(object->bytes, global->bytes.pointer, count);
                }
                for (u64 byte_index = 0; byte_index < object->size; byte_index += 1)
                {
                    object->initialized[byte_index] = 1;
                }
            }
            break;
            case IR_GLOBAL_INITIALIZER_INTEGER:
            case IR_GLOBAL_INITIALIZER_FLOAT:
            {
                u64 bits = global->initializer_bits;
                for (u64 byte_index = 0; byte_index < object->size; byte_index += 1)
                {
                    object->bytes[byte_index] = byte_index < sizeof(bits) ? (u8)(bits >> (u32)(byte_index * 8))
                                                                            : (global->initializer_is_negative ? 0xff : 0);
                    object->initialized[byte_index] = 1;
                }
            }
            break;
            case IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS:
            {
                IrRuntimeGlobal* target = ir_interpreter_global_find(runtime, global->initializer_symbol);
                IrType* type = ir_type_from_id(&program->types, global->type);
                if (!target || !target->object || global->initializer_addend < 0 || (u64)global->initializer_addend > target->object->size)
                {
                    return false;
                }
                IrRuntimeValue address = {
                    .object = target->object,
                    .offset = (u64)global->initializer_addend,
                    .kind = IR_RUNTIME_VALUE_ADDRESS,
                    .initialized = true,
                };
                if (!type || !ir_interpreter_memory_write(arena, object, 0, type->layout.size, address))
                {
                    return false;
                }
            }
            break;
            case IR_GLOBAL_INITIALIZER_COUNT:
                return false;
            }
            if (global->relocation_count && !global->relocations)
            {
                return false;
            }
            for (u32 relocation_index = 0; relocation_index < global->relocation_count; relocation_index += 1)
            {
                if (!ir_interpreter_global_relocation_apply(arena, program, runtime, object, global->relocations + relocation_index))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_object_range_valid(IrRuntimeObject* object, u64 offset, u64 size)
{
    return object && offset <= object->size && size <= object->size - offset;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_ranges_overlap(u64 left_offset, u64 left_size, u64 right_offset, u64 right_size)
{
    return left_size && right_size && left_offset <= UINT64_MAX - left_size && right_offset <= UINT64_MAX - right_size && left_offset < right_offset + right_size &&
           right_offset < left_offset + left_size;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_stored_value_index_offset_lower_bound(IrRuntimeStoredValueIndex* index, u64 offset)
{
    u64 low = 0;
    u64 high = index->count;
    while (low < high)
    {
        u64 middle = low + (high - low) / 2;
#if BUSTER_INCLUDE_TESTS
        ir_interpreter_test_counters.stored_value_index_probe_count += 1;
#endif
        if (index->values[middle]->offset < offset)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_stored_value_index_first_ending_after(IrRuntimeStoredValueIndex* index, u64 offset)
{
    u64 low = 0;
    u64 high = index->count;
    while (low < high)
    {
        u64 middle = low + (high - low) / 2;
        IrRuntimeStoredValue* stored = index->values[middle];
#if BUSTER_INCLUDE_TESTS
        ir_interpreter_test_counters.stored_value_index_probe_count += 1;
#endif
        if (stored->offset + stored->size <= offset)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

BUSTER_GLOBAL_LOCAL void ir_interpreter_stored_value_index_abandon(IrRuntimeObject* object)
{
    IrRuntimeStoredValueIndex* index = object->stored_value_index;
    object->first_stored_value = 0;
    for (u64 stored_index = index->count; stored_index; stored_index -= 1)
    {
        IrRuntimeStoredValue* stored = index->values[stored_index - 1];
        stored->next = object->first_stored_value;
        object->first_stored_value = stored;
    }
    object->stored_value_index = 0;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_stored_value_index_reserve(Arena* arena, IrRuntimeStoredValueIndex* index, u64 required_capacity)
{
    if (required_capacity <= index->capacity)
    {
        return true;
    }
    if (index->capacity > UINT64_MAX / 2)
    {
        return false;
    }
    u64 capacity = index->capacity ? index->capacity * 2 : IR_INTERPRETER_STORED_VALUE_INDEX_THRESHOLD * 2;
    if (capacity < required_capacity || !ir_interpreter_arena_can_allocate_count(arena, sizeof(*index->values), capacity))
    {
        return false;
    }
    IrRuntimeStoredValue** values = arena_allocate(arena, IrRuntimeStoredValue*, capacity);
    if (index->count)
    {
        memcpy(values, index->values, sizeof(*values) * index->count);
#if BUSTER_INCLUDE_TESTS
        ir_interpreter_test_counters.stored_value_index_moved_count += index->count;
#endif
    }
    index->values = values;
    index->capacity = capacity;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_stored_value_index_build(Arena* arena, IrRuntimeObject* object, u64 stored_value_count)
{
    u64 actual_count = 0;
    for (IrRuntimeStoredValue* stored = object->first_stored_value; stored; stored = stored->next)
    {
        // Zero-sized metadata never overlaps, so more than one newest-first
        // entry may legally share an offset on the list.  Keep that rare
        // representation on the list rather than changing first-match rules.
        if (!stored->size || actual_count == stored_value_count)
        {
            return false;
        }
        actual_count += 1;
    }
    u64 values_size = 0;
    u64 allocation_size = 0;
    u64 capacity = IR_INTERPRETER_STORED_VALUE_INDEX_THRESHOLD * 2;
    if (stored_value_count != IR_INTERPRETER_STORED_VALUE_INDEX_THRESHOLD || actual_count != stored_value_count ||
        !ir_interpreter_u64_multiply(sizeof(IrRuntimeStoredValue*), capacity, &values_size) ||
        !ir_interpreter_u64_add(values_size,
                                sizeof(IrRuntimeStoredValueIndex) + BUSTER_ALIGN_OF(IrRuntimeStoredValueIndex) + BUSTER_ALIGN_OF(IrRuntimeStoredValue*),
                                &allocation_size) ||
        !ir_interpreter_arena_can_allocate(arena, allocation_size))
    {
        return false;
    }
    IrRuntimeStoredValueIndex* index = arena_allocate(arena, IrRuntimeStoredValueIndex, 1);
    *index = (IrRuntimeStoredValueIndex){
        .values = arena_allocate(arena, IrRuntimeStoredValue*, capacity),
        .capacity = capacity,
    };
    for (IrRuntimeStoredValue* stored = object->first_stored_value; stored; stored = stored->next)
    {
        index->values[index->count] = stored;
        index->count += 1;
    }
    for (u64 stored_index = 1; stored_index < index->count; stored_index += 1)
    {
        IrRuntimeStoredValue* stored = index->values[stored_index];
        u64 insertion_index = stored_index;
        while (insertion_index && index->values[insertion_index - 1]->offset > stored->offset)
        {
            index->values[insertion_index] = index->values[insertion_index - 1];
            insertion_index -= 1;
        }
        index->values[insertion_index] = stored;
    }
    object->first_stored_value = 0;
    object->stored_value_index = index;
#if BUSTER_INCLUDE_TESTS
    ir_interpreter_test_counters.stored_value_index_build_count += 1;
#endif
    return true;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_stored_values_clear(IrRuntimeObject* object, u64 offset, u64 size)
{
    if (BUSTER_UNLIKELY(object->stored_value_index))
    {
        IrRuntimeStoredValueIndex* index = object->stored_value_index;
        if (!size || !index->count)
        {
            return index->count;
        }
        u64 first = ir_interpreter_stored_value_index_first_ending_after(index, offset);
        u64 end = offset + size;
        u64 after = first;
        while (after < index->count && index->values[after]->offset < end)
        {
#if BUSTER_INCLUDE_TESTS
            ir_interpreter_test_counters.stored_value_index_probe_count += 1;
#endif
            after += 1;
        }
        u64 removed = after - first;
        u64 tail = index->count - after;
        if (removed && tail)
        {
            memmove(index->values + first, index->values + after, sizeof(*index->values) * tail);
#if BUSTER_INCLUDE_TESTS
            ir_interpreter_test_counters.stored_value_index_moved_count += tail;
#endif
        }
        index->count -= removed;
        return index->count;
    }
    u64 stored_value_count = 0;
    IrRuntimeStoredValue** link = &object->first_stored_value;
    while (*link)
    {
        IrRuntimeStoredValue* stored = *link;
#if BUSTER_INCLUDE_TESTS
        ir_interpreter_test_counters.stored_value_linear_clear_probe_count += 1;
#endif
        if (ir_interpreter_ranges_overlap(stored->offset, stored->size, offset, size))
        {
            *link = stored->next;
        }
        else
        {
            link = &stored->next;
            stored_value_count += 1;
        }
    }
    return stored_value_count;
}

BUSTER_GLOBAL_LOCAL void ir_interpreter_stored_value_add(Arena* arena, IrRuntimeObject* object, u64 offset, u64 size, IrRuntimeValue value,
                                                          u64 existing_count)
{
    IrRuntimeStoredValue* stored = arena_allocate(arena, IrRuntimeStoredValue, 1);
    *stored = (IrRuntimeStoredValue){
        .value = value,
        .offset = offset,
        .size = size,
    };
    if (!size && BUSTER_UNLIKELY(object->stored_value_index))
    {
        ir_interpreter_stored_value_index_abandon(object);
    }
    if (BUSTER_UNLIKELY(object->stored_value_index))
    {
        IrRuntimeStoredValueIndex* index = object->stored_value_index;
        if (!ir_interpreter_stored_value_index_reserve(arena, index, index->count + 1))
        {
            ir_interpreter_stored_value_index_abandon(object);
        }
        else
        {
            u64 insertion_index = ir_interpreter_stored_value_index_offset_lower_bound(index, offset);
            u64 tail = index->count - insertion_index;
            if (tail)
            {
                memmove(index->values + insertion_index + 1, index->values + insertion_index, sizeof(*index->values) * tail);
#if BUSTER_INCLUDE_TESTS
                ir_interpreter_test_counters.stored_value_index_moved_count += tail;
#endif
            }
            index->values[insertion_index] = stored;
            index->count += 1;
            return;
        }
    }
    stored->next = object->first_stored_value;
    object->first_stored_value = stored;
    if (BUSTER_UNLIKELY(existing_count + 1 == IR_INTERPRETER_STORED_VALUE_INDEX_THRESHOLD))
    {
        ir_interpreter_stored_value_index_build(arena, object, existing_count + 1);
    }
}

BUSTER_GLOBAL_LOCAL IrRuntimeStoredValue* ir_interpreter_stored_value_find(IrRuntimeObject* object, u64 offset, u64 size)
{
    if (BUSTER_UNLIKELY(object->stored_value_index))
    {
        IrRuntimeStoredValueIndex* index = object->stored_value_index;
        u64 stored_index = ir_interpreter_stored_value_index_offset_lower_bound(index, offset);
        if (stored_index < index->count && index->values[stored_index]->offset == offset && index->values[stored_index]->size == size)
        {
            return index->values[stored_index];
        }
        return 0;
    }
    for (IrRuntimeStoredValue* stored = object->first_stored_value; stored; stored = stored->next)
    {
#if BUSTER_INCLUDE_TESTS
        ir_interpreter_test_counters.stored_value_linear_find_probe_count += 1;
#endif
        if (stored->offset == offset && stored->size == size)
        {
            return stored;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_object_region_copy(Arena* arena, IrRuntimeObject* destination, u64 destination_offset, IrRuntimeObject* source,
                                                           u64 source_offset, u64 size, u64 destination_stored_value_count)
{
    // memory_write snapshots aggregate self-copies before reaching this path,
    // and memory_read always copies into a fresh object.
    if (destination == source || !ir_interpreter_object_range_valid(destination, destination_offset, size) ||
        !ir_interpreter_object_range_valid(source, source_offset, size))
    {
        return false;
    }
    for (u64 index = 0; index < size; index += 1)
    {
        destination->bytes[destination_offset + index] = source->bytes[source_offset + index];
        destination->initialized[destination_offset + index] = source->initialized[source_offset + index];
    }
    u64 source_end = source_offset + size;
    if (BUSTER_UNLIKELY(source->stored_value_index))
    {
        IrRuntimeStoredValueIndex* index = source->stored_value_index;
        u64 first = ir_interpreter_stored_value_index_first_ending_after(index, source_offset);
        for (u64 stored_index = first; stored_index < index->count && index->values[stored_index]->offset < source_end; stored_index += 1)
        {
            IrRuntimeStoredValue* stored = index->values[stored_index];
            if (stored->offset >= source_offset && stored->size <= source_end - stored->offset)
            {
                ir_interpreter_stored_value_add(arena, destination, destination_offset + stored->offset - source_offset, stored->size, stored->value,
                                                destination_stored_value_count);
                destination_stored_value_count += 1;
            }
        }
        return true;
    }
    for (IrRuntimeStoredValue* stored = source->first_stored_value; stored; stored = stored->next)
    {
        if (ir_interpreter_object_range_valid(source, stored->offset, stored->size) && stored->offset >= source_offset && stored->offset <= source_end &&
            stored->size <= source_end - stored->offset)
        {
            ir_interpreter_stored_value_add(arena, destination, destination_offset + stored->offset - source_offset, stored->size, stored->value,
                                            destination_stored_value_count);
            destination_stored_value_count += 1;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_memory_write(Arena* arena, IrRuntimeObject* object, u64 offset, u64 size, IrRuntimeValue value)
{
    if (!ir_interpreter_object_range_valid(object, offset, size))
    {
        return false;
    }
    if (!size && value.initialized && value.kind == IR_RUNTIME_VALUE_AGGREGATE && value.object == object &&
        ir_interpreter_object_range_valid(value.object, value.offset, size))
    {
        return true;
    }
    if (value.initialized && size && value.kind == IR_RUNTIME_VALUE_AGGREGATE && value.object == object &&
        ir_interpreter_object_range_valid(value.object, value.offset, size))
    {
        IrRuntimeObject* snapshot = ir_interpreter_object_create(arena, size);
        if (!snapshot || !ir_interpreter_object_region_copy(arena, snapshot, 0, object, value.offset, size, 0))
        {
            return false;
        }
        value.object = snapshot;
        value.offset = 0;
    }
    u64 stored_value_count = ir_interpreter_stored_values_clear(object, offset, size);
    if (!value.initialized)
    {
        for (u64 index = 0; index < size; index += 1)
        {
            object->initialized[offset + index] = 0;
        }
        return true;
    }
    if (value.kind == IR_RUNTIME_VALUE_SCALAR)
    {
        for (u64 index = 0; index < size; index += 1)
        {
            object->bytes[offset + index] = (u8)(index < 8 ? value.bits >> (u32)(index * 8) : 0);
            object->initialized[offset + index] = 1;
        }
        if (value.has_label_provenance)
        {
            ir_interpreter_stored_value_add(arena, object, offset, size, value, stored_value_count);
        }
        return true;
    }
    if (value.kind == IR_RUNTIME_VALUE_AGGREGATE && ir_interpreter_object_range_valid(value.object, value.offset, size))
    {
        return ir_interpreter_object_region_copy(arena, object, offset, value.object, value.offset, size, stored_value_count);
    }
    for (u64 index = 0; index < size; index += 1)
    {
        object->bytes[offset + index] = 0;
        object->initialized[offset + index] = 1;
    }
    ir_interpreter_stored_value_add(arena, object, offset, size, value, stored_value_count);
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_memory_read(Arena* arena, AnalysisResult* analysis, AnalysisTypeId type_id, IrRuntimeObject* object, u64 offset,
                                                    IrRuntimeValue* value_out)
{
    u64 size = ir_interpreter_type_size(analysis, type_id);
    if (!ir_interpreter_object_range_valid(object, offset, size))
    {
        return false;
    }
    AnalysisType* type = analysis_type_from_id(analysis, type_id);
    bool scalar =
        type->kind == ANALYSIS_TYPE_BOOL || type->kind == ANALYSIS_TYPE_INTEGER || type->kind == ANALYSIS_TYPE_FLOAT || type->kind == ANALYSIS_TYPE_ENUM;
    if (scalar)
    {
        u64 bits = 0;
        for (u64 index = 0; index < size; index += 1)
        {
            if (!object->initialized[offset + index])
            {
                return false;
            }
            if (index < 8)
            {
                bits |= (u64)object->bytes[offset + index] << (u32)(index * 8);
            }
        }
        *value_out = (IrRuntimeValue){
            .bits = bits,
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
        };
        return true;
    }
    if (type_id.value == analysis->types.builtin.va_list_type.value)
    {
        IrRuntimeStoredValue* stored = ir_interpreter_stored_value_find(object, offset, size);
        if (!stored || !stored->value.initialized || stored->value.kind != IR_RUNTIME_VALUE_VA_LIST)
        {
            return false;
        }
        *value_out = stored->value;
        return true;
    }
    if (type->kind == ANALYSIS_TYPE_ARRAY || type->kind == ANALYSIS_TYPE_VECTOR || type->kind == ANALYSIS_TYPE_STRUCT || type->kind == ANALYSIS_TYPE_UNION)
    {
        IrRuntimeObject* copy = ir_interpreter_object_create(arena, size);
        if (!ir_interpreter_object_region_copy(arena, copy, 0, object, offset, size, 0))
        {
            return false;
        }
        *value_out = (IrRuntimeValue){
            .object = copy,
            .length = size,
            .kind = IR_RUNTIME_VALUE_AGGREGATE,
            .initialized = true,
        };
        return true;
    }
    IrRuntimeStoredValue* stored = ir_interpreter_stored_value_find(object, offset, size);
    if (!stored || !stored->value.initialized)
    {
        return false;
    }
    *value_out = stored->value;
    return true;
}

#if BUSTER_INCLUDE_TESTS
bool ir_interpreter_test_stored_value_stress(Arena* arena, u32 stored_value_count)
{
    const u64 stored_value_size = 8;
    u64 object_size = 0;
    if (!arena || stored_value_count < 2 || !ir_interpreter_u64_multiply(stored_value_count, stored_value_size, &object_size))
    {
        return false;
    }
    IrRuntimeObject* alias_target = ir_interpreter_object_create(arena, stored_value_count);
    IrRuntimeObject* source = ir_interpreter_object_create(arena, object_size);
    IrRuntimeObject* copy = ir_interpreter_object_create(arena, object_size);
    if (!alias_target || !source || !copy)
    {
        return false;
    }
    for (u32 stored_index = 0; stored_index < stored_value_count; stored_index += 1)
    {
        IrRuntimeValue address = {
            .object = alias_target,
            .offset = stored_index,
            .kind = IR_RUNTIME_VALUE_ADDRESS,
            .initialized = true,
        };
        if (!ir_interpreter_memory_write(arena, source, (u64)stored_index * stored_value_size, stored_value_size, address))
        {
            return false;
        }
    }
    for (u32 stored_index = 0; stored_index < stored_value_count; stored_index += 1)
    {
        IrRuntimeStoredValue* stored = ir_interpreter_stored_value_find(source, (u64)stored_index * stored_value_size, stored_value_size);
        if (!stored || stored->value.kind != IR_RUNTIME_VALUE_ADDRESS || stored->value.object != alias_target || stored->value.offset != stored_index)
        {
            return false;
        }
    }
    u64 zero_self_copy_position = arena->position;
    IrRuntimeValue zero_self_copy = {
        .object = source,
        .offset = object_size,
        .kind = IR_RUNTIME_VALUE_AGGREGATE,
        .initialized = true,
    };
    if (!ir_interpreter_memory_write(arena, source, object_size, 0, zero_self_copy) || arena->position != zero_self_copy_position)
    {
        return false;
    }
    if (stored_value_count >= IR_INTERPRETER_STORED_VALUE_INDEX_THRESHOLD)
    {
        IrRuntimeValue self_copy = {
            .object = source,
            .length = object_size,
            .kind = IR_RUNTIME_VALUE_AGGREGATE,
            .initialized = true,
        };
        if (!ir_interpreter_memory_write(arena, source, 0, object_size, self_copy))
        {
            return false;
        }
        for (u32 stored_index = 0; stored_index < stored_value_count; stored_index += 1)
        {
            IrRuntimeStoredValue* stored = ir_interpreter_stored_value_find(source, (u64)stored_index * stored_value_size, stored_value_size);
            if (!stored || stored->value.kind != IR_RUNTIME_VALUE_ADDRESS || stored->value.object != alias_target || stored->value.offset != stored_index)
            {
                return false;
            }
        }
    }
    IrRuntimeValue aggregate = {
        .object = source,
        .length = object_size,
        .kind = IR_RUNTIME_VALUE_AGGREGATE,
        .initialized = true,
    };
    if (!ir_interpreter_memory_write(arena, copy, 0, object_size, aggregate))
    {
        return false;
    }
    for (u32 stored_index = 0; stored_index < stored_value_count; stored_index += 1)
    {
        IrRuntimeStoredValue* stored = ir_interpreter_stored_value_find(copy, (u64)stored_index * stored_value_size, stored_value_size);
        if (!stored || stored->value.kind != IR_RUNTIME_VALUE_ADDRESS || stored->value.object != alias_target || stored->value.offset != stored_index)
        {
            return false;
        }
    }
    if (stored_value_count < 4)
    {
        IrRuntimeValue older_zero_sized = {
            .object = alias_target,
            .kind = IR_RUNTIME_VALUE_ADDRESS,
            .initialized = true,
        };
        IrRuntimeValue newer_zero_sized = older_zero_sized;
        newer_zero_sized.offset = 1;
        if (!ir_interpreter_memory_write(arena, copy, object_size, 0, older_zero_sized) ||
            !ir_interpreter_memory_write(arena, copy, object_size, 0, newer_zero_sized))
        {
            return false;
        }
        IrRuntimeStoredValue* zero_sized = ir_interpreter_stored_value_find(copy, object_size, 0);
        if (!zero_sized || zero_sized->value.object != alias_target || zero_sized->value.offset != 1)
        {
            return false;
        }
        u64 uninitialized_position = arena->position;
        IrRuntimeValue uninitialized_self_copy = {
            .object = copy,
            .kind = IR_RUNTIME_VALUE_AGGREGATE,
        };
        if (!ir_interpreter_memory_write(arena, copy, 0, object_size, uninitialized_self_copy) || arena->position != uninitialized_position ||
            ir_interpreter_stored_value_find(copy, 0, stored_value_size) ||
            ir_interpreter_stored_value_find(copy, stored_value_size, stored_value_size))
        {
            return false;
        }
        zero_sized = ir_interpreter_stored_value_find(copy, object_size, 0);
        if (!zero_sized || zero_sized->value.offset != 1)
        {
            return false;
        }
        for (u64 byte_index = 0; byte_index < object_size; byte_index += 1)
        {
            if (copy->initialized[byte_index])
            {
                return false;
            }
        }
        return true;
    }

    u64 overwrite_offset = stored_value_size + stored_value_size / 2;
    u64 overwrite_size = object_size - stored_value_size * 3;
    IrRuntimeValue scalar = {
        .bits = UINT64_C(0x123456789abcdef0),
        .kind = IR_RUNTIME_VALUE_SCALAR,
        .initialized = true,
    };
    if (!ir_interpreter_memory_write(arena, copy, overwrite_offset, overwrite_size, scalar))
    {
        return false;
    }
    IrRuntimeStoredValue* first = ir_interpreter_stored_value_find(copy, 0, stored_value_size);
    IrRuntimeStoredValue* last = ir_interpreter_stored_value_find(copy, object_size - stored_value_size, stored_value_size);
    IrRuntimeStoredValue* overwritten = ir_interpreter_stored_value_find(copy, stored_value_size, stored_value_size);
    if (!first || first->value.object != alias_target || first->value.offset != 0 || !last || last->value.object != alias_target ||
        last->value.offset != stored_value_count - 1 || overwritten)
    {
        return false;
    }

    u64 replacement_offset = (u64)(stored_value_count / 2) * stored_value_size;
    IrRuntimeValue replacement = {
        .object = alias_target,
        .offset = stored_value_count / 2,
        .kind = IR_RUNTIME_VALUE_ADDRESS,
        .initialized = true,
    };
    if (!ir_interpreter_memory_write(arena, copy, replacement_offset, stored_value_size, replacement))
    {
        return false;
    }
    IrRuntimeStoredValue* replaced = ir_interpreter_stored_value_find(copy, replacement_offset, stored_value_size);
    if (!replaced || replaced->value.object != alias_target || replaced->value.offset != stored_value_count / 2)
    {
        return false;
    }
    if (!ir_interpreter_memory_write(arena, copy, replacement_offset + 3, 1, (IrRuntimeValue){.kind = IR_RUNTIME_VALUE_SCALAR}) ||
        ir_interpreter_stored_value_find(copy, replacement_offset, stored_value_size) ||
        !ir_interpreter_stored_value_find(copy, 0, stored_value_size) ||
        !ir_interpreter_stored_value_find(copy, object_size - stored_value_size, stored_value_size))
    {
        return false;
    }
    u64 uninitialized_position = arena->position;
    IrRuntimeValue uninitialized_self_copy = {
        .object = copy,
        .kind = IR_RUNTIME_VALUE_AGGREGATE,
    };
    if (!ir_interpreter_memory_write(arena, copy, 0, object_size, uninitialized_self_copy) || arena->position != uninitialized_position ||
        ir_interpreter_stored_value_find(copy, 0, stored_value_size) ||
        ir_interpreter_stored_value_find(copy, object_size - stored_value_size, stored_value_size))
    {
        return false;
    }
    for (u64 byte_index = 0; byte_index < object_size; byte_index += 1)
    {
        if (copy->initialized[byte_index])
        {
            return false;
        }
    }
    return true;
}
#endif

BUSTER_GLOBAL_LOCAL bool ir_interpreter_field(IrExecutionFrame* frame, AnalysisTypeId aggregate_type_id, u32 field_index, AnalysisField** field_out)
{
    AnalysisType* aggregate_type = analysis_type_from_id(frame->analysis, aggregate_type_id);
    if (aggregate_type->kind != ANALYSIS_TYPE_STRUCT && aggregate_type->kind != ANALYSIS_TYPE_UNION)
    {
        return false;
    }
    AnalysisResult* declaration_analysis = ir_interpreter_analysis_find(frame->analysis, aggregate_type->as.declaration.module);
    if (!declaration_analysis || aggregate_type->as.declaration.index.value >= declaration_analysis->module.entity_count ||
        (declaration_analysis->module.entity_count && !declaration_analysis->module.semantics))
    {
        return false;
    }
    AnalysisEntitySemantic* semantic = declaration_analysis->module.semantics + aggregate_type->as.declaration.index.value;
    if (field_index >= semantic->field_count)
    {
        return false;
    }
    *field_out = semantic->fields + field_index;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_inline_assembly_jump_target(IrFunction* function, IrInstruction* instruction, String8 literal, String8 prefix,
                                                                    u32* target_index_out)
{
    return ir_inline_assembly_jump_target(function, instruction, literal, prefix, target_index_out);
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_instruction_shape_valid(IrExecutionFrame* frame, IrInstruction* instruction)
{
    if (!frame || !instruction || instruction->opcode >= IR_OPCODE_COUNT || !ir_interpreter_type_id_valid(frame->analysis, instruction->type) ||
        (instruction->operand_count && !instruction->operands) || (instruction->target_count && !instruction->targets) ||
        (instruction->immediate_count && !instruction->immediates))
    {
        return false;
    }
    if (instruction->operand_count > frame->function->value_count || instruction->target_count > frame->function->block_count)
    {
        return false;
    }
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value >= frame->function->value_count)
    {
        return false;
    }
    for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
    {
        if (instruction->operands[operand_index].value >= frame->function->value_count)
        {
            return false;
        }
    }
    for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
    {
        if (instruction->targets[target_index].value >= frame->function->block_count)
        {
            return false;
        }
    }
    switch (instruction->opcode)
    {
    case IR_OPCODE_ARGUMENT:
        return instruction->operand_count == 0 && instruction->target_count == 0 && instruction->immediate_count == 1;
    case IR_OPCODE_LOCAL:
    case IR_OPCODE_STACK_SAVE:
    case IR_OPCODE_GLOBAL:
    case IR_OPCODE_CONSTANT_STRING:
    case IR_OPCODE_UNDEFINED:
    case IR_OPCODE_FUNCTION:
    case IR_OPCODE_VA_START:
    case IR_OPCODE_DEBUG_TRAP:
    case IR_OPCODE_UNREACHABLE:
        return instruction->operand_count == 0 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_STACK_ALLOCATE:
    case IR_OPCODE_STACK_RESTORE:
    case IR_OPCODE_LOAD:
    case IR_OPCODE_ATOMIC_LOAD:
    case IR_OPCODE_LENGTH:
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
    case IR_OPCODE_REVERSE:
    case IR_OPCODE_VA_COPY:
    case IR_OPCODE_VA_END:
    case IR_OPCODE_VA_ARG:
        return instruction->operand_count == 1 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_STORE:
    case IR_OPCODE_ATOMIC_STORE:
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
        return instruction->operand_count == 2 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
        return instruction->operand_count == 3 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_ATOMIC_FENCE:
        return instruction->operand_count == 0 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
        return instruction->operand_count == 2 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_CONSTANT_FLOAT:
    case IR_OPCODE_ENUM:
        return instruction->operand_count == 0 && instruction->target_count == 0 && instruction->immediate_count == 1;
    case IR_OPCODE_ARRAY:
        return instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_AGGREGATE:
        return instruction->target_count == 0 && instruction->operand_count == instruction->immediate_count;
    case IR_OPCODE_INDEX:
        return instruction->operand_count == 2 && instruction->target_count == 0 && instruction->immediate_count == 0 &&
               instruction->result.value != IR_ID_UNDERLYING_INVALID;
    case IR_OPCODE_SLICE:
        return instruction->operand_count >= 1 && instruction->operand_count <= 3 && instruction->target_count == 0 && instruction->immediate_count == 2;
    case IR_OPCODE_FIELD:
        return instruction->operand_count == 1 && instruction->target_count == 0 && instruction->immediate_count == 1 &&
               instruction->result.value != IR_ID_UNDERLYING_INVALID;
    case IR_OPCODE_CALL:
        return instruction->operand_count >= 1 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_CAST:
    case IR_OPCODE_UNARY:
        return instruction->operand_count == 1 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_BINARY:
        return instruction->operand_count == 2 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_INLINE_ASSEMBLY:
    {
        IrInstructionExtra extra = ir_instruction_extra(frame->function, instruction->id);
        bool valid = instruction->operand_count == instruction->immediate_count && (instruction->target_count == 0 || instruction->target_count >= 2) &&
                     extra.label_name_count == (instruction->target_count ? instruction->target_count - 1 : 0) &&
                     (!extra.label_name_count || extra.label_names) && extra.operand_name_count == instruction->operand_count &&
                     (!extra.operand_name_count || extra.operand_names) && (!extra.clobber_count || extra.clobbers);
        for (u32 target_index = 0; valid && target_index < instruction->target_count; target_index += 1)
        {
            valid = instruction->targets[target_index].value < frame->function->block_count;
        }
        return valid;
    }
    case IR_OPCODE_SIMD:
    {
        // The interpreter never executes these — they only reach it through a
        // malformed program — but the shape check still has to agree with the
        // rest of the pipeline about what a well-formed one looks like.
        IrSimdShape shape = ir_simd_operation_shape((IrSimdOperation)instruction->simd_operation);
        return instruction->simd_operation < IR_SIMD_COUNT && instruction->operand_count == shape.operand_count && instruction->target_count == 0 &&
               instruction->immediate_count == shape.immediate_count &&
               (shape.has_result == (instruction->result.value != IR_ID_UNDERLYING_INVALID));
    }
    case IR_OPCODE_LABEL_ADDRESS:
    {
        AnalysisType* type = analysis_type_from_id(frame->analysis, instruction->type);
        AnalysisType* element = type && type->kind == ANALYSIS_TYPE_POINTER ? analysis_type_from_id(frame->analysis, type->as.element_type) : 0;
        bool result_in_range = instruction->result.value < frame->function->value_count;
        IrValueLabelMetadata result_metadata =
            result_in_range ? ir_value_label_metadata(frame->function, instruction->result) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata* result = result_in_range ? &result_metadata : 0;
        return instruction->operand_count == 0 && instruction->target_count == 1 && instruction->targets && instruction->targets[0].value < frame->function->block_count &&
               instruction->immediate_count == 0 && instruction->result.value != IR_ID_UNDERLYING_INVALID && result && element &&
               element->kind == ANALYSIS_TYPE_VOID && !result->has_non_label_provenance && !result->has_label_provenance && !result->label_paths &&
               !result->label_path_count && ir_label_provenance_valid(result) && result->label_block_count == 1 && result->label_blocks &&
               result->label_blocks[0].value == instruction->targets[0].value;
    }
    case IR_OPCODE_BRANCH:
        return instruction->operand_count == 0 && instruction->target_count == 1 && instruction->immediate_count == 0;
    case IR_OPCODE_BRANCH_IF:
        return instruction->operand_count == 1 && instruction->target_count == 2 && instruction->immediate_count == 0;
    case IR_OPCODE_SWITCH:
        return instruction->operand_count == 1 && instruction->immediate_count != UINT32_MAX && instruction->target_count == instruction->immediate_count + 1;
    case IR_OPCODE_INDIRECT_BRANCH:
    {
        IrValue* target_slot = instruction->operand_count == 1 && instruction->operands && instruction->operands[0].value < frame->function->value_count
                                   ? frame->function->values + instruction->operands[0].value
                                   : 0;
        IrValueLabelMetadata target_metadata =
            target_slot ? ir_value_label_metadata(frame->function, instruction->operands[0]) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata* target_value = target_slot ? &target_metadata : 0;
        AnalysisType* target_type = target_slot ? analysis_type_from_id(frame->analysis, target_slot->type) : 0;
        AnalysisType* target_element = target_type && target_type->kind == ANALYSIS_TYPE_POINTER
                                           ? analysis_type_from_id(frame->analysis, target_type->as.element_type)
                                           : 0;
        bool valid = instruction->operand_count == 1 && target_value && target_element && target_element->kind == ANALYSIS_TYPE_VOID &&
                     instruction->target_count == target_value->label_block_count && instruction->target_count != 0 && instruction->targets &&
                     instruction->immediate_count == 0 && instruction->result.value == IR_ID_UNDERLYING_INVALID &&
                     !target_value->has_non_label_provenance && ir_label_provenance_valid(target_value) &&
                     ir_block_id_array_unique(instruction->targets, instruction->target_count);
        bool label_targets = valid;
        for (u32 label_index = 0; label_targets && label_index < target_value->label_block_count; label_index += 1)
        {
            bool found = false;
            for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
            {
                found |= instruction->targets[target_index].value == target_value->label_blocks[label_index].value;
            }
            label_targets &= target_value->label_blocks[label_index].value < frame->function->block_count && found;
        }
        for (u32 target_index = 0; valid && target_index < instruction->target_count; target_index += 1)
        {
            bool found = false;
            for (u32 label_index = 0; label_index < target_value->label_block_count; label_index += 1)
            {
                found |= instruction->targets[target_index].value == target_value->label_blocks[label_index].value;
            }
            valid &= found;
        }
        return valid && label_targets;
    }
    case IR_OPCODE_RETURN:
        return instruction->operand_count <= 1 && instruction->target_count == 0 && instruction->immediate_count == 0;
    case IR_OPCODE_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_function_shape_valid(IrExecutionTarget target)
{
#if BUSTER_INCLUDE_TESTS
    ir_interpreter_test_counters.function_validation_count += 1;
#endif
    IrFunction* function = target.function;
    if (!target.analysis || !function || !function->blocks || !function->values || !function->instructions ||
        function->entry.value >= function->block_count || !ir_interpreter_type_id_valid(target.analysis, function->type))
    {
        return false;
    }
    AnalysisType* function_type = analysis_type_from_id(target.analysis, function->type);
    if (function_type->kind != ANALYSIS_TYPE_FUNCTION || !ir_interpreter_type_id_valid(target.analysis, function_type->as.function.return_type) ||
        (function_type->as.function.argument_count && !function_type->as.function.argument_types))
    {
        return false;
    }
    for (u32 argument_index = 0; argument_index < function_type->as.function.argument_count; argument_index += 1)
    {
        if (!ir_interpreter_type_id_valid(target.analysis, function_type->as.function.argument_types[argument_index]))
        {
            return false;
        }
    }
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        if (!ir_interpreter_type_id_valid(target.analysis, function->values[value_index].type))
        {
            return false;
        }
        // The interpreter also admits the legacy analysis-backed IR used by
        // its scalar tests; canonical layout checks belong to the canonical
        // module validator.  The transfer/shape checks remain active here so
        // forged label metadata is still rejected before execution.
        if (!ir_label_metadata_shape_valid(0, function, (IrValueId){.value = value_index}) ||
            !ir_label_metadata_transfer_valid(0, function, (IrValueId){.value = value_index}))
        {
            return false;
        }
    }
    IrExecutionFrame shape_frame = {
        .analysis = target.analysis,
        .function = function,
    };
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        if ((block->first_instruction.value != IR_ID_UNDERLYING_INVALID && block->first_instruction.value >= function->instruction_count) ||
            (block->last_instruction.value != IR_ID_UNDERLYING_INVALID && block->last_instruction.value >= function->instruction_count))
        {
            return false;
        }
        if (block->parameter_count > function->value_count)
        {
            return false;
        }
        u32 parameter_count = 0;
        for (IrBlockParameter* parameter = block->first_parameter; parameter && parameter_count < block->parameter_count; parameter = parameter->next)
        {
            if (parameter->value.value >= function->value_count || !ir_interpreter_type_id_valid(target.analysis, parameter->type))
            {
                return false;
            }
            if (!ir_label_block_parameter_provenance_valid(function, parameter))
            {
                return false;
            }
            parameter_count += 1;
        }
        if (parameter_count != block->parameter_count)
        {
            return false;
        }
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        if (!ir_interpreter_instruction_shape_valid(&shape_frame, function->instructions + instruction_index))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u64 ir_interpreter_function_cache_hash(AnalysisEntityId entity, AnalysisInstantiationId instantiation)
{
    u64 hash = ((u64)entity.module.value << 32) | entity.index.value;
    hash ^= (u64)instantiation.value * UINT64_C(0x9e3779b97f4a7c15);
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    hash *= UINT64_C(0x94d049bb133111eb);
    return hash ^ (hash >> 31);
}

BUSTER_GLOBAL_LOCAL IrExecutionFunctionCacheEntry* ir_interpreter_function_cache_slot(IrExecutionFunctionCache* cache, AnalysisEntityId entity,
                                                                                      AnalysisInstantiationId instantiation)
{
    u32 mask = cache->capacity - 1;
    u32 slot_index = (u32)ir_interpreter_function_cache_hash(entity, instantiation) & mask;
    IrExecutionFunctionCacheEntry* entry = cache->entries + slot_index;
    while (entry->used &&
           (!ir_interpreter_entity_equal(entry->entity, entity) || entry->instantiation.value != instantiation.value))
    {
        slot_index = (slot_index + 1) & mask;
        entry = cache->entries + slot_index;
    }
    return entry;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_function_cache_grow(Arena* scratch_arena, IrExecutionFunctionCache* cache)
{
    if (!scratch_arena || !cache || cache->capacity > UINT32_MAX / 2)
    {
        return false;
    }
    u32 capacity = cache->capacity ? cache->capacity * 2 : 16;
    if (!ir_interpreter_arena_can_allocate_count(scratch_arena, sizeof(IrExecutionFunctionCacheEntry), capacity))
    {
        return false;
    }
    IrExecutionFunctionCacheEntry* entries = arena_allocate(scratch_arena, IrExecutionFunctionCacheEntry, capacity);
    memset(entries, 0, sizeof(*entries) * capacity);
    IrExecutionFunctionCache previous = *cache;
    cache->entries = entries;
    cache->capacity = capacity;
    for (u32 entry_index = 0; entry_index < previous.capacity; entry_index += 1)
    {
        IrExecutionFunctionCacheEntry entry = previous.entries[entry_index];
        if (entry.used)
        {
            *ir_interpreter_function_cache_slot(cache, entry.entity, entry.instantiation) = entry;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL IrExecutionTarget ir_interpreter_function_resolve(Arena* scratch_arena, IrRuntimeContext* runtime, AnalysisProgram* analysis,
                                                                       IrProgram* program, AnalysisEntityId entity,
                                                                       AnalysisInstantiationId instantiation, IrExecutionTrap* trap_out)
{
    IrExecutionTarget target = {0};
    if (!scratch_arena || !runtime || !trap_out)
    {
        if (trap_out)
        {
            *trap_out = IR_EXECUTION_TRAP_INVALID_PROGRAM;
        }
        return target;
    }
    *trap_out = IR_EXECUTION_TRAP_NONE;
    IrExecutionFunctionCache* cache = &runtime->functions;
    if (!cache->capacity && !ir_interpreter_function_cache_grow(scratch_arena, cache))
    {
        *trap_out = IR_EXECUTION_TRAP_INVALID_PROGRAM;
        return target;
    }
    IrExecutionFunctionCacheEntry* entry = ir_interpreter_function_cache_slot(cache, entity, instantiation);
    if (!entry->used && cache->count >= cache->capacity / 2)
    {
        if (!ir_interpreter_function_cache_grow(scratch_arena, cache))
        {
            *trap_out = IR_EXECUTION_TRAP_INVALID_PROGRAM;
            return target;
        }
        entry = ir_interpreter_function_cache_slot(cache, entity, instantiation);
    }
    if (!entry->used)
    {
        entry->used = true;
        entry->entity = entity;
        entry->instantiation = instantiation;
        entry->target = ir_interpreter_function_find(analysis, program, entity, instantiation);
        cache->count += 1;
    }
    target = entry->target;
    if (!target.function)
    {
        *trap_out = IR_EXECUTION_TRAP_FUNCTION_NOT_FOUND;
    }
    else if (target.function->state != IR_FUNCTION_LOWERED)
    {
        *trap_out = IR_EXECUTION_TRAP_FUNCTION_NOT_LOWERED;
    }
    else
    {
        if (entry->validation == IR_EXECUTION_TARGET_NOT_VALIDATED)
        {
            entry->validation = ir_interpreter_function_shape_valid(target) ? IR_EXECUTION_TARGET_VALID : IR_EXECUTION_TARGET_INVALID;
        }
        if (entry->validation != IR_EXECUTION_TARGET_VALID)
        {
            *trap_out = IR_EXECUTION_TRAP_INVALID_PROGRAM;
        }
    }
    return target;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_frame_prepare(Arena* scratch_arena, IrExecutionFrame* frame, IrExecutionTarget target, IrRuntimeValue* arguments,
                                                      u32 argument_count, IrValueId caller_result)
{
    if (!scratch_arena || !frame || !target.analysis || !target.function || (argument_count && !arguments))
    {
        return false;
    }
    AnalysisType* function_type = analysis_type_from_id(target.analysis, target.function->type);
    if (function_type->kind != ANALYSIS_TYPE_FUNCTION ||
        (!function_type->as.function.is_variadic && function_type->as.function.argument_count != argument_count) ||
        (function_type->as.function.is_variadic && function_type->as.function.argument_count > argument_count) || target.function->state != IR_FUNCTION_LOWERED)
    {
        return false;
    }
    u32 required_value_capacity = target.function->value_count ? target.function->value_count : 1;
    if (!frame->values || !frame->transition_values || frame->value_capacity < required_value_capacity)
    {
        if (!ir_interpreter_arena_can_allocate_count(scratch_arena, sizeof(IrRuntimeValue), (u64)required_value_capacity * 2))
        {
            return false;
        }
        frame->values = arena_allocate(scratch_arena, IrRuntimeValue, required_value_capacity);
        frame->transition_values = arena_allocate(scratch_arena, IrRuntimeValue, required_value_capacity);
        if (!frame->values || !frame->transition_values)
        {
            return false;
        }
        frame->value_capacity = required_value_capacity;
    }
    if (argument_count && (!frame->arguments || frame->argument_capacity < argument_count))
    {
        if (!ir_interpreter_arena_can_allocate_count(scratch_arena, sizeof(IrRuntimeValue), argument_count))
        {
            return false;
        }
        frame->arguments = arena_allocate(scratch_arena, IrRuntimeValue, argument_count);
        if (!frame->arguments)
        {
            return false;
        }
        frame->argument_capacity = argument_count;
    }
    for (u32 value_index = 0; value_index < target.function->value_count; value_index += 1)
    {
        frame->values[value_index] = (IrRuntimeValue){0};
    }
    for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
    {
        frame->arguments[argument_index] = arguments[argument_index];
    }
    frame->analysis = target.analysis;
    frame->module = target.module;
    frame->function = target.function;
    frame->caller_result = caller_result;
    frame->block = target.function->entry;
    frame->instruction = target.function->blocks[target.function->entry.value].first_instruction;
    frame->argument_count = argument_count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_block_enter(IrExecutionFrame* frame, IrBlockId target, IrBlockId predecessor)
{
    if (target.value >= frame->function->block_count)
    {
        return false;
    }
    IrBlock* block = frame->function->blocks + target.value;
    u32 parameter_index = 0;
    for (IrBlockParameter* parameter = block->first_parameter; parameter && parameter_index < block->parameter_count; parameter = parameter->next)
    {
        IrIncoming* selected = 0;
        u32 incoming_count = 0;
        for (IrIncoming* incoming = parameter->first_incoming; incoming && incoming_count <= frame->function->value_count; incoming = incoming->next)
        {
            if (incoming->predecessor.value == predecessor.value)
            {
                selected = incoming;
                break;
            }
            incoming_count += 1;
        }
        if (!selected && incoming_count > frame->function->value_count)
        {
            return false;
        }
        if (!selected || parameter->value.value >= frame->function->value_count || selected->value.value >= frame->function->value_count ||
            !frame->values[selected->value.value].initialized)
        {
            return false;
        }
        frame->transition_values[parameter_index++] = frame->values[selected->value.value];
    }
    if (parameter_index != block->parameter_count)
    {
        return false;
    }
    parameter_index = 0;
    for (IrBlockParameter* parameter = block->first_parameter; parameter && parameter_index < block->parameter_count; parameter = parameter->next)
    {
        frame->values[parameter->value.value] = frame->transition_values[parameter_index++];
    }
    frame->block = target;
    frame->instruction = block->first_instruction;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_operands_ready(IrExecutionFrame* frame, IrInstruction* instruction)
{
    for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
    {
        IrValueId operand = instruction->operands[operand_index];
        if (operand.value >= frame->function->value_count ||
            (!frame->values[operand.value].initialized && !(instruction->opcode == IR_OPCODE_STORE && operand_index == 1)))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_integer_binary(IrExecutionFrame* frame, IrInstruction* instruction, u64* bits_out, IrExecutionTrap* trap_out)
{
    IrValueId left_id = instruction->operands[0];
    IrValueId right_id = instruction->operands[1];
    u64 left = frame->values[left_id.value].bits;
    u64 right = frame->values[right_id.value].bits;
    AnalysisTypeId operand_type = frame->function->values[left_id.value].type;
    u32 width = ir_interpreter_type_width(frame->analysis, operand_type);
    u64 mask = ir_interpreter_mask(width);
    left &= mask;
    right &= mask;
    u64 value = 0;
    switch (instruction->binary_operation)
    {
    case IR_BINARY_INTEGER_ADD:
        value = left + right;
        break;
    case IR_BINARY_INTEGER_SUBTRACT:
        value = left - right;
        break;
    case IR_BINARY_INTEGER_MULTIPLY:
        value = left * right;
        break;
    case IR_BINARY_SIGNED_DIVIDE:
    case IR_BINARY_UNSIGNED_DIVIDE:
    case IR_BINARY_SIGNED_REMAINDER:
    case IR_BINARY_UNSIGNED_REMAINDER:
    {
        if (!right)
        {
            *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
            return false;
        }
        bool divide = instruction->binary_operation == IR_BINARY_SIGNED_DIVIDE || instruction->binary_operation == IR_BINARY_UNSIGNED_DIVIDE;
        bool signed_operation = instruction->binary_operation == IR_BINARY_SIGNED_DIVIDE || instruction->binary_operation == IR_BINARY_SIGNED_REMAINDER;
        if (signed_operation)
        {
            bool left_negative = ir_interpreter_integer_negative(left, width);
            bool right_negative = ir_interpreter_integer_negative(right, width);
            u64 left_magnitude = ir_interpreter_integer_magnitude(left, width);
            u64 right_magnitude = ir_interpreter_integer_magnitude(right, width);
            if (divide)
            {
                value = left_magnitude / right_magnitude;
                if (left_negative != right_negative)
                {
                    value = (0 - value) & mask;
                }
            }
            else
            {
                value = left_magnitude % right_magnitude;
                if (left_negative)
                {
                    value = (0 - value) & mask;
                }
            }
        }
        else
        {
            value = divide ? left / right : left % right;
        }
    }
    break;
    case IR_BINARY_SHIFT_LEFT:
    case IR_BINARY_SIGNED_SHIFT_RIGHT:
    case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
    {
        if (right >= width)
        {
            *trap_out = IR_EXECUTION_TRAP_INVALID_SHIFT;
            return false;
        }
        if (instruction->binary_operation == IR_BINARY_SHIFT_LEFT)
        {
            value = left << (u32)right;
        }
        else if (instruction->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT && ir_interpreter_integer_negative(left, width) && right)
        {
            value = (left >> (u32)right) | (mask ^ (mask >> (u32)right));
        }
        else
        {
            value = left >> (u32)right;
        }
    }
    break;
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
        value = left == right;
        break;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
        value = left != right;
        break;
    case IR_BINARY_SIGNED_LESS:
    case IR_BINARY_SIGNED_LESS_EQUAL:
    case IR_BINARY_SIGNED_GREATER:
    case IR_BINARY_SIGNED_GREATER_EQUAL:
    case IR_BINARY_UNSIGNED_LESS:
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
    case IR_BINARY_UNSIGNED_GREATER:
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
    {
        bool signed_operation = instruction->binary_operation >= IR_BINARY_SIGNED_LESS && instruction->binary_operation <= IR_BINARY_SIGNED_GREATER_EQUAL;
        s32 order = signed_operation ? ir_interpreter_signed_compare(left, right, width) : left < right ? -1 : left > right ? 1 : 0;
        IrBinaryOperation relative = signed_operation ? (IrBinaryOperation)(instruction->binary_operation - IR_BINARY_SIGNED_LESS)
                                                      : (IrBinaryOperation)(instruction->binary_operation - IR_BINARY_UNSIGNED_LESS);
        value = relative == 0 ? order < 0 : relative == 1 ? order <= 0 : relative == 2 ? order > 0 : order >= 0;
    }
    break;
    case IR_BINARY_INTEGER_BITWISE_AND:
    case IR_BINARY_BOOLEAN_AND:
    {
        value = left & right;
    }
    break;
    case IR_BINARY_INTEGER_BITWISE_OR:
    case IR_BINARY_BOOLEAN_OR:
    {
        value = left | right;
    }
    break;
    case IR_BINARY_INTEGER_BITWISE_XOR:
        value = left ^ right;
        break;
    default:
    {
        *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
        return false;
    }
    }
    *bits_out = ir_interpreter_normalize_integer(frame->analysis, instruction->type, value);
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_float_binary(IrExecutionFrame* frame, IrInstruction* instruction, u64* bits_out, IrExecutionTrap* trap_out)
{
    IrValueId left_id = instruction->operands[0];
    IrValueId right_id = instruction->operands[1];
    AnalysisTypeId operand_type = frame->function->values[left_id.value].type;
    u32 width = ir_interpreter_type_width(frame->analysis, operand_type);
    f64 left = ir_interpreter_float_read(frame->values[left_id.value].bits, width);
    f64 right = ir_interpreter_float_read(frame->values[right_id.value].bits, width);
    f64 value = 0.0;
    bool comparison = false;
    bool comparison_value = false;
    switch (instruction->binary_operation)
    {
    case IR_BINARY_FLOAT_ADD:
        value = left + right;
        break;
    case IR_BINARY_FLOAT_SUBTRACT:
        value = left - right;
        break;
    case IR_BINARY_FLOAT_MULTIPLY:
        value = left * right;
        break;
    case IR_BINARY_FLOAT_DIVIDE:
    {
        if (right == 0.0)
        {
            *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
            return false;
        }
        value = left / right;
    }
    break;
    case IR_BINARY_FLOAT_EQUAL:
        comparison = true;
        comparison_value = left == right;
        break;
    case IR_BINARY_FLOAT_NOT_EQUAL:
        comparison = true;
        comparison_value = left != right;
        break;
    case IR_BINARY_FLOAT_LESS:
        comparison = true;
        comparison_value = left < right;
        break;
    case IR_BINARY_FLOAT_LESS_EQUAL:
        comparison = true;
        comparison_value = left <= right;
        break;
    case IR_BINARY_FLOAT_GREATER:
        comparison = true;
        comparison_value = left > right;
        break;
    case IR_BINARY_FLOAT_GREATER_EQUAL:
        comparison = true;
        comparison_value = left >= right;
        break;
    default:
    {
        *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
        return false;
    }
    }
    *bits_out = comparison ? comparison_value : ir_interpreter_float_write(value, width);
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_vector_unary(Arena* arena, IrExecutionFrame* frame, IrInstruction* instruction, IrRuntimeValue* value_out,
                                                     IrExecutionTrap* trap_out)
{
    AnalysisType* vector = analysis_type_from_id(frame->analysis, instruction->type);
    AnalysisTypeId element_type_id = vector->as.vector.element_type;
    AnalysisType* element = analysis_type_from_id(frame->analysis, element_type_id);
    u64 lane_size = ir_interpreter_type_size(frame->analysis, element_type_id);
    IrRuntimeValue operand = frame->values[instruction->operands[0].value];
    if (operand.kind != IR_RUNTIME_VALUE_AGGREGATE || !operand.object || !lane_size)
    {
        *trap_out = IR_EXECUTION_TRAP_INVALID_MEMORY;
        return false;
    }
    IrRuntimeObject* object = ir_interpreter_object_create(arena, vector->layout.size);
    if (!object)
    {
        *trap_out = IR_EXECUTION_TRAP_INVALID_MEMORY;
        return false;
    }
    for (u64 lane = 0; lane < vector->as.vector.count; lane += 1)
    {
        IrRuntimeValue source = {0};
        u64 lane_offset = 0;
        if (!ir_interpreter_u64_multiply(lane, lane_size, &lane_offset) || !ir_interpreter_u64_add(operand.offset, lane_offset, &lane_offset) ||
            !ir_interpreter_memory_read(arena, frame->analysis, element_type_id, operand.object, lane_offset, &source))
        {
            *trap_out = IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
            return false;
        }
        u64 bits = source.bits;
        if (instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE)
        {
            bits = ir_interpreter_float_write(-ir_interpreter_float_read(bits, element->as.float_bit_width), element->as.float_bit_width);
        }
        else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
        {
            bits = (0 - bits) & ir_interpreter_mask(element->as.integer.bit_width);
        }
        else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
        {
            bits = ~bits & ir_interpreter_mask(element->as.integer.bit_width);
        }
        else
        {
            *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
            return false;
        }
        if (!ir_interpreter_memory_write(arena, object, lane * lane_size, lane_size,
                                         (IrRuntimeValue){
                                             .bits = bits,
                                             .kind = IR_RUNTIME_VALUE_SCALAR,
                                             .initialized = true,
                                         }))
        {
            *trap_out = IR_EXECUTION_TRAP_INVALID_MEMORY;
            return false;
        }
    }
    *value_out = (IrRuntimeValue){
        .object = object,
        .length = vector->layout.size,
        .kind = IR_RUNTIME_VALUE_AGGREGATE,
        .initialized = true,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_vector_binary(Arena* arena, IrExecutionFrame* frame, IrInstruction* instruction, IrRuntimeValue* value_out,
                                                      IrExecutionTrap* trap_out)
{
    AnalysisTypeId operand_type_id = frame->function->values[instruction->operands[0].value].type;
    AnalysisType* vector = analysis_type_from_id(frame->analysis, operand_type_id);
    AnalysisTypeId element_type_id = vector->as.vector.element_type;
    AnalysisType* element = analysis_type_from_id(frame->analysis, element_type_id);
    AnalysisType* result_vector = analysis_type_from_id(frame->analysis, instruction->type);
    AnalysisTypeId result_element_type_id = result_vector->as.vector.element_type;
    u64 lane_size = ir_interpreter_type_size(frame->analysis, element_type_id);
    IrRuntimeValue left = frame->values[instruction->operands[0].value];
    IrRuntimeValue right = frame->values[instruction->operands[1].value];
    if (left.kind != IR_RUNTIME_VALUE_AGGREGATE || right.kind != IR_RUNTIME_VALUE_AGGREGATE || !left.object || !right.object || !lane_size)
    {
        *trap_out = IR_EXECUTION_TRAP_INVALID_MEMORY;
        return false;
    }
    IrRuntimeObject* object = ir_interpreter_object_create(arena, result_vector->layout.size);
    if (!object)
    {
        *trap_out = IR_EXECUTION_TRAP_INVALID_MEMORY;
        return false;
    }
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ? element->as.float_bit_width : element->as.integer.bit_width;
    u64 mask = ir_interpreter_mask(width);
    for (u64 lane = 0; lane < vector->as.vector.count; lane += 1)
    {
        IrRuntimeValue left_lane = {0};
        IrRuntimeValue right_lane = {0};
        u64 lane_offset = 0;
        u64 left_offset = 0;
        u64 right_offset = 0;
        if (!ir_interpreter_u64_multiply(lane, lane_size, &lane_offset) || !ir_interpreter_u64_add(left.offset, lane_offset, &left_offset) ||
            !ir_interpreter_u64_add(right.offset, lane_offset, &right_offset) ||
            !ir_interpreter_memory_read(arena, frame->analysis, element_type_id, left.object, left_offset, &left_lane) ||
            !ir_interpreter_memory_read(arena, frame->analysis, element_type_id, right.object, right_offset, &right_lane))
        {
            *trap_out = IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
            return false;
        }
        u64 result_bits = 0;
        bool comparison = false;
        bool comparison_value = false;
        IrBinaryOperation operation = instruction->binary_operation;
        if (element->kind == ANALYSIS_TYPE_FLOAT)
        {
            f64 left_value = ir_interpreter_float_read(left_lane.bits, width);
            f64 right_value = ir_interpreter_float_read(right_lane.bits, width);
            f64 result_value = 0.0;
            switch (operation)
            {
            case IR_BINARY_VECTOR_FLOAT_ADD:
                result_value = left_value + right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
                result_value = left_value - right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
                result_value = left_value * right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_DIVIDE:
                if (right_value == 0.0)
                {
                    *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
                    return false;
                }
                result_value = left_value / right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_EQUAL:
                comparison = true;
                comparison_value = left_value == right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
                comparison = true;
                comparison_value = left_value != right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_LESS:
                comparison = true;
                comparison_value = left_value < right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
                comparison = true;
                comparison_value = left_value <= right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_GREATER:
                comparison = true;
                comparison_value = left_value > right_value;
                break;
            case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
                comparison = true;
                comparison_value = left_value >= right_value;
                break;
            default:
                *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                return false;
            }
            result_bits = comparison ? (comparison_value ? mask : 0) : ir_interpreter_float_write(result_value, width);
        }
        else
        {
            u64 left_bits = left_lane.bits & mask;
            u64 right_bits = right_lane.bits & mask;
            switch (operation)
            {
            case IR_BINARY_VECTOR_INTEGER_ADD:
                result_bits = left_bits + right_bits;
                break;
            case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
                result_bits = left_bits - right_bits;
                break;
            case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
                result_bits = left_bits * right_bits;
                break;
            case IR_BINARY_VECTOR_SIGNED_DIVIDE:
            case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
            case IR_BINARY_VECTOR_SIGNED_REMAINDER:
            case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
            {
                if (!right_bits)
                {
                    *trap_out = IR_EXECUTION_TRAP_DIVISION_BY_ZERO;
                    return false;
                }
                bool divide = operation == IR_BINARY_VECTOR_SIGNED_DIVIDE || operation == IR_BINARY_VECTOR_UNSIGNED_DIVIDE;
                bool signed_operation = operation == IR_BINARY_VECTOR_SIGNED_DIVIDE || operation == IR_BINARY_VECTOR_SIGNED_REMAINDER;
                if (signed_operation)
                {
                    bool left_negative = ir_interpreter_integer_negative(left_bits, width);
                    bool right_negative = ir_interpreter_integer_negative(right_bits, width);
                    u64 left_magnitude = ir_interpreter_integer_magnitude(left_bits, width);
                    u64 right_magnitude = ir_interpreter_integer_magnitude(right_bits, width);
                    result_bits = divide ? left_magnitude / right_magnitude : left_magnitude % right_magnitude;
                    if ((divide && left_negative != right_negative) || (!divide && left_negative))
                    {
                        result_bits = 0 - result_bits;
                    }
                }
                else
                {
                    result_bits = divide ? left_bits / right_bits : left_bits % right_bits;
                }
            }
            break;
            case IR_BINARY_VECTOR_SHIFT_LEFT:
            case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
            case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
                if (right_bits >= width)
                {
                    *trap_out = IR_EXECUTION_TRAP_INVALID_SHIFT;
                    return false;
                }
                if (operation == IR_BINARY_VECTOR_SHIFT_LEFT)
                {
                    result_bits = left_bits << (u32)right_bits;
                }
                else if (operation == IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT && ir_interpreter_integer_negative(left_bits, width) && right_bits)
                {
                    result_bits = (left_bits >> (u32)right_bits) | (mask ^ (mask >> (u32)right_bits));
                }
                else
                {
                    result_bits = left_bits >> (u32)right_bits;
                }
                break;
            case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
                result_bits = left_bits & right_bits;
                break;
            case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
                result_bits = left_bits | right_bits;
                break;
            case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
                result_bits = left_bits ^ right_bits;
                break;
            case IR_BINARY_VECTOR_INTEGER_EQUAL:
                comparison = true;
                comparison_value = left_bits == right_bits;
                break;
            case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
                comparison = true;
                comparison_value = left_bits != right_bits;
                break;
            case IR_BINARY_VECTOR_SIGNED_LESS:
            case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
            case IR_BINARY_VECTOR_SIGNED_GREATER:
            case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
            case IR_BINARY_VECTOR_UNSIGNED_LESS:
            case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
            case IR_BINARY_VECTOR_UNSIGNED_GREATER:
            case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
            {
                comparison = true;
                bool signed_operation = operation >= IR_BINARY_VECTOR_SIGNED_LESS && operation <= IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL;
                s32 order = signed_operation         ? ir_interpreter_signed_compare(left_bits, right_bits, width)
                            : left_bits < right_bits ? -1
                            : left_bits > right_bits ? 1
                                                     : 0;
                IrBinaryOperation first = signed_operation ? IR_BINARY_VECTOR_SIGNED_LESS : IR_BINARY_VECTOR_UNSIGNED_LESS;
                u32 relative = (u32)(operation - first);
                comparison_value = relative == 0 ? order < 0 : relative == 1 ? order <= 0 : relative == 2 ? order > 0 : order >= 0;
            }
            break;
            default:
                *trap_out = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                return false;
            }
            if (comparison)
            {
                result_bits = comparison_value ? mask : 0;
            }
            result_bits &= mask;
        }
        if (!ir_interpreter_memory_write(arena, object, lane * lane_size, lane_size,
                                         (IrRuntimeValue){
                                             .bits = result_bits,
                                             .kind = IR_RUNTIME_VALUE_SCALAR,
                                             .initialized = true,
                                         }))
        {
            *trap_out = IR_EXECUTION_TRAP_INVALID_MEMORY;
            return false;
        }
    }
    BUSTER_UNUSED(result_element_type_id);
    *value_out = (IrRuntimeValue){
        .object = object,
        .length = result_vector->layout.size,
        .kind = IR_RUNTIME_VALUE_AGGREGATE,
        .initialized = true,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_cast(IrExecutionFrame* frame, IrInstruction* instruction, u64* bits_out)
{
    IrValueId operand_id = instruction->operands[0];
    AnalysisType* source = analysis_type_from_id(frame->analysis, frame->function->values[operand_id.value].type);
    AnalysisType* target = analysis_type_from_id(frame->analysis, instruction->type);
    u64 bits = frame->values[operand_id.value].bits;
    switch (instruction->conversion_operation)
    {
    case IR_CONVERSION_IDENTITY:
        *bits_out = bits;
        return true;
    case IR_CONVERSION_INTEGER_SIGN_EXTEND:
    {
        u32 source_width = ir_interpreter_type_width(frame->analysis, source->id);
        u32 target_width = ir_interpreter_type_width(frame->analysis, target->id);
        bits &= ir_interpreter_mask(source_width);
        if (ir_interpreter_integer_negative(bits, source_width))
        {
            bits |= ~ir_interpreter_mask(source_width);
        }
        *bits_out = bits & ir_interpreter_mask(target_width);
        return true;
    }
    case IR_CONVERSION_INTEGER_ZERO_EXTEND:
    case IR_CONVERSION_INTEGER_TRUNCATE:
    case IR_CONVERSION_INTEGER_REINTERPRET:
    {
        *bits_out = bits & ir_interpreter_mask(ir_interpreter_type_width(frame->analysis, target->id));
        return true;
    }
    case IR_CONVERSION_FLOAT_EXTEND:
    case IR_CONVERSION_FLOAT_TRUNCATE:
    {
        *bits_out = ir_interpreter_float_write(ir_interpreter_float_read(bits, source->as.float_bit_width), target->as.float_bit_width);
        return true;
    }
    case IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT:
    case IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT:
    {
        u32 source_width = ir_interpreter_type_width(frame->analysis, source->id);
        bits &= ir_interpreter_mask(source_width);
        f64 value = (f64)bits;
        if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT && ir_interpreter_integer_negative(bits, source_width))
        {
            value = -(f64)ir_interpreter_integer_magnitude(bits, source_width);
        }
        *bits_out = ir_interpreter_float_write(value, target->as.float_bit_width);
        return true;
    }
    case IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER:
    case IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER:
    {
        f64 value = ir_interpreter_float_read(bits, source->as.float_bit_width);
        u32 target_width = ir_interpreter_type_width(frame->analysis, target->id);
        if (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER && value < 0.0)
        {
            u64 magnitude = (u64)(-value);
            *bits_out = (0 - magnitude) & ir_interpreter_mask(target_width);
        }
        else
        {
            *bits_out = (u64)value & ir_interpreter_mask(target_width);
        }
        return true;
    }
    case IR_CONVERSION_POINTER_REINTERPRET:
    case IR_CONVERSION_POINTER_TO_INTEGER:
    case IR_CONVERSION_INTEGER_TO_POINTER:
    case IR_CONVERSION_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_collection(IrExecutionFrame* frame, AnalysisTypeId type_id, IrRuntimeValue value, IrRuntimeObject** object_out,
                                                   u64* offset_out, u64* count_out, u64* element_size_out)
{
    if (!frame || !object_out || !offset_out || !count_out || !element_size_out || !ir_interpreter_type_id_valid(frame->analysis, type_id))
    {
        return false;
    }
    AnalysisType* type = analysis_type_from_id(frame->analysis, type_id);
    if (type->kind == ANALYSIS_TYPE_SLICE && value.kind == IR_RUNTIME_VALUE_PLACE)
    {
        IrRuntimeStoredValue* stored = ir_interpreter_stored_value_find(value.object, value.offset, ir_interpreter_type_size(frame->analysis, type_id));
        if (!stored)
        {
            return false;
        }
        value = stored->value;
    }
    if (type->kind == ANALYSIS_TYPE_SLICE && value.kind == IR_RUNTIME_VALUE_SLICE)
    {
        u64 size = 0;
        if (!value.object || !value.element_size || !ir_interpreter_u64_multiply(value.length, value.element_size, &size) ||
            !ir_interpreter_object_range_valid(value.object, value.offset, size))
        {
            return false;
        }
        *object_out = value.object;
        *offset_out = value.offset;
        *count_out = value.length;
        *element_size_out = value.element_size;
        return true;
    }
    if (type->kind == ANALYSIS_TYPE_ARRAY || type->kind == ANALYSIS_TYPE_VECTOR || type->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
    {
        AnalysisTypeId element = type->kind == ANALYSIS_TYPE_ARRAY    ? type->as.array.element_type
                                 : type->kind == ANALYSIS_TYPE_VECTOR ? type->as.vector.element_type
                                                                      : type->as.element_type;
        u64 element_size = ir_interpreter_type_size(frame->analysis, element);
        u64 count = type->kind == ANALYSIS_TYPE_ARRAY ? type->as.array.count : type->kind == ANALYSIS_TYPE_VECTOR ? type->as.vector.count : value.length;
        if ((value.kind != IR_RUNTIME_VALUE_AGGREGATE && value.kind != IR_RUNTIME_VALUE_PLACE) || !value.object || !element_size)
        {
            return false;
        }
        u64 size = 0;
        if (!ir_interpreter_u64_multiply(count, element_size, &size) || !ir_interpreter_object_range_valid(value.object, value.offset, size))
        {
            return false;
        }
        *object_out = value.object;
        *offset_out = value.offset;
        *count_out = count;
        *element_size_out = element_size;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL IrExecutionResult ir_interpreter_trap(IrExecutionFrame* frame, IrExecutionTrap trap, u64 step_count)
{
    return (IrExecutionResult){
        .function = frame ? frame->function->entity : ANALYSIS_ENTITY_ID_INVALID,
        .instantiation = frame ? frame->function->instantiation : ANALYSIS_INSTANTIATION_ID_INVALID,
        .instruction = frame ? frame->instruction : IR_INSTRUCTION_ID_INVALID,
        .step_count = step_count,
        .trap = trap,
    };
}

BUSTER_GLOBAL_LOCAL IrExecutionValue ir_interpreter_public_value(Arena* arena, AnalysisProgram* analysis, IrRuntimeContext* runtime, IrRuntimeValue value,
                                                                 AnalysisTypeId type_id)
{
    IrExecutionValue result = {
        .bits = value.bits,
        .has_value = value.initialized,
    };
    if (!value.initialized)
    {
        return result;
    }
    switch (value.kind)
    {
    case IR_RUNTIME_VALUE_SCALAR:
        result.kind = IR_EXECUTION_VALUE_SCALAR;
        return result;
    case IR_RUNTIME_VALUE_FUNCTION:
        result.kind = IR_EXECUTION_VALUE_FUNCTION;
        return result;
    case IR_RUNTIME_VALUE_ADDRESS:
    {
        result.kind = IR_EXECUTION_VALUE_ADDRESS;
        result.address_offset = value.offset;
        for (u32 index = 0; runtime && index < runtime->global_count; index += 1)
        {
            if (runtime->globals[index].object == value.object)
            {
                result.address_object = runtime->globals[index].object_id;
                break;
            }
        }
        return result;
    }
    case IR_RUNTIME_VALUE_AGGREGATE:
    case IR_RUNTIME_VALUE_SLICE:
    case IR_RUNTIME_VALUE_RANGE:
    case IR_RUNTIME_VALUE_PLACE:
    {
        result.kind = IR_EXECUTION_VALUE_AGGREGATE;
        AnalysisResult* value_analysis = 0;
        for (u32 module_index = 0; analysis && module_index < analysis->module_count; module_index += 1)
        {
            AnalysisResult* candidate = analysis->module_results[module_index];
            if (candidate && candidate->module.id.value == value.type_module.value)
            {
                value_analysis = candidate;
                break;
            }
        }
        AnalysisType* type = value_analysis ? analysis_type_from_id(value_analysis, type_id) : 0;
        if (type && type->kind == ANALYSIS_TYPE_VECTOR)
        {
            result.kind = IR_EXECUTION_VALUE_VECTOR;
        }
        u64 size = value.length;
        if (!size && value_analysis)
        {
            size = ir_interpreter_type_size(value_analysis, type_id);
        }
        if (value.kind == IR_RUNTIME_VALUE_SLICE && value.element_size && value.length <= UINT64_MAX / value.element_size)
        {
            size = value.length * value.element_size;
        }
        if (arena && value.object && ir_interpreter_object_range_valid(value.object, value.offset, size))
        {
            if (!ir_interpreter_arena_can_allocate_count(arena, size, 2))
            {
                return result;
            }
            result.bytes = (ByteSlice){
                .pointer = arena_allocate(arena, u8, size),
                .length = size,
            };
            result.initialized = (ByteSlice){
                .pointer = arena_allocate(arena, u8, size),
                .length = size,
            };
            if (size)
            {
                memcpy(result.bytes.pointer, value.object->bytes + value.offset, size);
                memcpy(result.initialized.pointer, value.object->initialized + value.offset, size);
            }
        }
        return result;
    }
    case IR_RUNTIME_VALUE_VA_LIST:
    case IR_RUNTIME_VALUE_KIND_COUNT:
        result.kind = IR_EXECUTION_VALUE_NONE;
        return result;
    }
    result.kind = IR_EXECUTION_VALUE_NONE;
    return result;
}

IrExecutionResult ir_execute(Arena* execution_arena, AnalysisProgram* analysis, IrProgram* program, AnalysisEntityId entry,
                             AnalysisInstantiationId instantiation, IrExecutionArgument* arguments, u32 argument_count, IrExecutionOptions options)
{
    if (!execution_arena || !analysis || !program || !analysis->module_results || !program->modules || analysis->module_count != program->module_count ||
        (argument_count && !arguments))
    {
        return ir_interpreter_trap(0, IR_EXECUTION_TRAP_INVALID_PROGRAM, 0);
    }
    u64 max_steps = options.max_steps ? options.max_steps : UINT64_C(1000000);
    u32 max_call_depth = options.max_call_depth ? options.max_call_depth : 1024;
    Arena* scratch_conflicts[] = {execution_arena};
    TemporalArena scratch = scratch_begin(scratch_conflicts, BUSTER_ARRAY_LENGTH(scratch_conflicts));
    IrRuntimeContext runtime = {0};
    if (!ir_interpreter_runtime_globals_initialize(scratch.arena, program, &runtime))
    {
        scratch_end(scratch);
        return ir_interpreter_trap(0, IR_EXECUTION_TRAP_INVALID_PROGRAM, 0);
    }
    if (!ir_interpreter_arena_can_allocate_count(scratch.arena, sizeof(IrExecutionFrame), max_call_depth))
    {
        scratch_end(scratch);
        return ir_interpreter_trap(0, IR_EXECUTION_TRAP_INVALID_PROGRAM, 0);
    }
    IrExecutionFrame* frames = arena_allocate(scratch.arena, IrExecutionFrame, max_call_depth);
    for (u32 frame_index = 0; frame_index < max_call_depth; frame_index += 1)
    {
        frames[frame_index] = (IrExecutionFrame){0};
    }
    IrExecutionTrap entry_trap = IR_EXECUTION_TRAP_NONE;
    IrExecutionTarget entry_target = ir_interpreter_function_resolve(scratch.arena, &runtime, analysis, program, entry, instantiation, &entry_trap);
    if (entry_trap == IR_EXECUTION_TRAP_FUNCTION_NOT_FOUND)
    {
        arena_set_position(scratch.arena, scratch.position);
        return ir_interpreter_trap(0, IR_EXECUTION_TRAP_FUNCTION_NOT_FOUND, 0);
    }
    if (entry_trap != IR_EXECUTION_TRAP_NONE)
    {
        IrExecutionResult result = ir_interpreter_trap(0, entry_trap, 0);
        result.function = entry;
        result.instantiation = instantiation;
        arena_set_position(scratch.arena, scratch.position);
        return result;
    }
    AnalysisType* entry_type = analysis_type_from_id(entry_target.analysis, entry_target.function->type);
    if (entry_type->kind != ANALYSIS_TYPE_FUNCTION || (!entry_type->as.function.is_variadic && entry_type->as.function.argument_count != argument_count) ||
        (entry_type->as.function.is_variadic && entry_type->as.function.argument_count > argument_count))
    {
        IrExecutionResult result = ir_interpreter_trap(0, IR_EXECUTION_TRAP_ARGUMENT_COUNT, 0);
        result.function = entry;
        result.instantiation = instantiation;
        arena_set_position(scratch.arena, scratch.position);
        return result;
    }
    if (!ir_interpreter_arena_can_allocate_count(scratch.arena, sizeof(IrRuntimeValue), argument_count))
    {
        IrExecutionResult result = ir_interpreter_trap(0, IR_EXECUTION_TRAP_INVALID_PROGRAM, 0);
        result.function = entry;
        result.instantiation = instantiation;
        arena_set_position(scratch.arena, scratch.position);
        return result;
    }
    IrRuntimeValue* entry_arguments = arena_allocate(scratch.arena, IrRuntimeValue, argument_count);
    for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
    {
        entry_arguments[argument_index] = (IrRuntimeValue){
            .bits = arguments[argument_index].bits,
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
        };
    }
    if (!ir_interpreter_frame_prepare(scratch.arena, frames, entry_target, entry_arguments, argument_count, IR_VALUE_ID_INVALID))
    {
        IrExecutionResult result = ir_interpreter_trap(0, IR_EXECUTION_TRAP_ARGUMENT_COUNT, 0);
        result.function = entry;
        result.instantiation = instantiation;
        arena_set_position(scratch.arena, scratch.position);
        return result;
    }
    frames[0].runtime = &runtime;

    u32 depth = 1;
    u64 step_count = 0;
    while (depth)
    {
        IrExecutionFrame* frame = frames + depth - 1;
        if (step_count >= max_steps)
        {
            IrExecutionResult result = ir_interpreter_trap(frame, IR_EXECUTION_TRAP_STEP_LIMIT, step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }
        if (frame->instruction.value >= frame->function->instruction_count)
        {
            IrExecutionResult result = ir_interpreter_trap(frame, IR_EXECUTION_TRAP_INVALID_PROGRAM, step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }
        IrInstruction* instruction = frame->function->instructions + frame->instruction.value;
        step_count += 1;
        if (!ir_interpreter_operands_ready(frame, instruction))
        {
            IrExecutionResult result = ir_interpreter_trap(frame, IR_EXECUTION_TRAP_UNINITIALIZED_VALUE, step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }

        IrRuntimeValue produced = {
            .kind = IR_RUNTIME_VALUE_SCALAR,
            .initialized = true,
        };
        bool advance = true;
        IrExecutionTrap operation_trap = IR_EXECUTION_TRAP_NONE;
        switch (instruction->opcode)
        {
        case IR_OPCODE_ARGUMENT:
        {
            if (instruction->immediate_count != 1 || instruction->immediates[0] >= frame->argument_count)
            {
                operation_trap = IR_EXECUTION_TRAP_ARGUMENT_COUNT;
                break;
            }
            produced = frame->arguments[instruction->immediates[0]];
        }
        break;
        case IR_OPCODE_CONSTANT_INTEGER:
        case IR_OPCODE_ENUM:
        {
            if (instruction->immediate_count != 1)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            produced.bits = instruction->immediate_is_negative ? 0 - instruction->immediates[0] : instruction->immediates[0];
            produced.bits = ir_interpreter_normalize_integer(frame->analysis, instruction->type, produced.bits);
        }
        break;
        case IR_OPCODE_CONSTANT_FLOAT:
        {
            if (instruction->immediate_count != 1)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            produced.bits = instruction->immediates[0];
        }
        break;
        case IR_OPCODE_CONSTANT_STRING:
        {
            AnalysisType* string_type = analysis_type_from_id(frame->analysis, instruction->type);
            if (string_type->kind != ANALYSIS_TYPE_SLICE && string_type->kind != ANALYSIS_TYPE_ARRAY && string_type->kind != ANALYSIS_TYPE_INFERRED_ARRAY)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            String8 literal = ir_instruction_extra(frame->function, instruction->id).literal;
            IrRuntimeObject* object = ir_interpreter_object_create(scratch.arena, literal.length);
            if (!object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            for (u64 index = 0; index < literal.length; index += 1)
            {
                object->bytes[index] = literal.pointer[index];
                object->initialized[index] = 1;
            }
            produced = (IrRuntimeValue){
                .object = object,
                .length = literal.length,
                .element_size = 1,
                .kind = string_type->kind == ANALYSIS_TYPE_SLICE ? IR_RUNTIME_VALUE_SLICE : IR_RUNTIME_VALUE_AGGREGATE,
                .initialized = true,
            };
        }
        break;
        case IR_OPCODE_UNDEFINED:
        {
            produced.initialized = false;
        }
        break;
        case IR_OPCODE_LOCAL:
        {
            u64 size = ir_interpreter_type_size(frame->analysis, instruction->type);
            if (!size)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = (IrRuntimeValue){
                .object = ir_interpreter_object_create(scratch.arena, size),
                .length = size,
                .kind = IR_RUNTIME_VALUE_PLACE,
                .initialized = true,
            };
            if (!produced.object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_STACK_ALLOCATE:
        {
            IrRuntimeValue size = frame->values[instruction->operands[0].value];
            if (!size.initialized || size.kind != IR_RUNTIME_VALUE_SCALAR)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = (IrRuntimeValue){
                .object = ir_interpreter_object_create(scratch.arena, size.bits),
                .length = size.bits,
                .kind = IR_RUNTIME_VALUE_ADDRESS,
                .initialized = true,
            };
            if (!produced.object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_STACK_SAVE:
        {
            produced = (IrRuntimeValue){
                .object = ir_interpreter_object_create(scratch.arena, 1),
                .kind = IR_RUNTIME_VALUE_ADDRESS,
                .initialized = true,
            };
            if (!produced.object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_STACK_RESTORE:
        {
            IrRuntimeValue checkpoint = frame->values[instruction->operands[0].value];
            if (!checkpoint.initialized || checkpoint.kind != IR_RUNTIME_VALUE_ADDRESS)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_GLOBAL:
        {
            IrSymbolId symbol = instruction->symbol;
            if (symbol.value == IR_ID_UNDERLYING_INVALID && instruction->entity.module.value == frame->analysis->module.id.value &&
                instruction->entity.index.value < frame->module->frontend_symbol_count)
            {
                symbol = frame->module->frontend_symbol_map[instruction->entity.index.value];
            }
            IrRuntimeGlobal* global = ir_interpreter_global_find(frame->runtime, symbol);
            if (!global || !global->object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = (IrRuntimeValue){
                .object = global->object,
                .length = global->object->size,
                .kind = IR_RUNTIME_VALUE_PLACE,
                .initialized = true,
            };
        }
        break;
        case IR_OPCODE_LOAD:
        case IR_OPCODE_ATOMIC_LOAD:
        {
            IrRuntimeValue place = frame->values[instruction->operands[0].value];
            if (place.kind != IR_RUNTIME_VALUE_PLACE ||
                !ir_interpreter_memory_read(scratch.arena, frame->analysis, instruction->type, place.object, place.offset, &produced))
            {
                operation_trap = IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
            }
        }
        break;
        case IR_OPCODE_STORE:
        case IR_OPCODE_ATOMIC_STORE:
        {
            IrRuntimeValue place = frame->values[instruction->operands[0].value];
            IrRuntimeValue stored = frame->values[instruction->operands[1].value];
            AnalysisTypeId stored_type = frame->function->values[instruction->operands[1].value].type;
            u64 size = ir_interpreter_type_size(frame->analysis, stored_type);
            if (place.kind != IR_RUNTIME_VALUE_PLACE || !size || !ir_interpreter_memory_write(scratch.arena, place.object, place.offset, size, stored))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
        {
            IrRuntimeValue place = frame->values[instruction->operands[0].value];
            IrRuntimeValue operand = frame->values[instruction->operands[1].value];
            IrRuntimeValue updated = {0};
            AnalysisTypeId value_type = frame->function->values[instruction->operands[1].value].type;
            u64 size = ir_interpreter_type_size(frame->analysis, value_type);
            if (place.kind != IR_RUNTIME_VALUE_PLACE || !operand.initialized || !size ||
                !ir_interpreter_memory_read(scratch.arena, frame->analysis, value_type, place.object, place.offset, &produced) ||
                (instruction->atomic_operation != IR_ATOMIC_EXCHANGE &&
                 (operand.kind != IR_RUNTIME_VALUE_SCALAR ||
                  (produced.kind != IR_RUNTIME_VALUE_SCALAR &&
                   !((instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT) &&
                     produced.kind == IR_RUNTIME_VALUE_ADDRESS)))))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            updated = produced;
            switch (instruction->atomic_operation)
            {
            case IR_ATOMIC_ADD:
                if (updated.kind == IR_RUNTIME_VALUE_ADDRESS)
                {
                    if (!updated.object || updated.offset > updated.object->size || operand.bits > updated.object->size - updated.offset)
                    {
                        operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                    }
                    else
                    {
                        updated.offset += operand.bits;
                    }
                }
                else
                {
                    updated.bits += operand.bits;
                }
                break;
            case IR_ATOMIC_SUBTRACT:
                if (updated.kind == IR_RUNTIME_VALUE_ADDRESS)
                {
                    if (!updated.object || updated.offset < operand.bits)
                    {
                        operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                    }
                    else
                    {
                        updated.offset -= operand.bits;
                    }
                }
                else
                {
                    updated.bits -= operand.bits;
                }
                break;
            case IR_ATOMIC_BITWISE_AND:
                updated.bits &= operand.bits;
                break;
            case IR_ATOMIC_BITWISE_OR:
                updated.bits |= operand.bits;
                break;
            case IR_ATOMIC_BITWISE_XOR:
                updated.bits ^= operand.bits;
                break;
            case IR_ATOMIC_EXCHANGE:
                updated = operand;
                break;
            case IR_ATOMIC_OPERATION_COUNT:
            {
                operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
            }
            break;
            }
            if (updated.kind == IR_RUNTIME_VALUE_SCALAR)
            {
                updated.bits = ir_interpreter_normalize_integer(frame->analysis, value_type, updated.bits);
            }
            if (operation_trap == IR_EXECUTION_TRAP_NONE && !ir_interpreter_memory_write(scratch.arena, place.object, place.offset, size, updated))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
        {
            IrRuntimeValue place = frame->values[instruction->operands[0].value];
            IrRuntimeValue expected = frame->values[instruction->operands[1].value];
            IrRuntimeValue desired = frame->values[instruction->operands[2].value];
            AnalysisTypeId value_type = frame->function->values[instruction->operands[1].value].type;
            u64 size = ir_interpreter_type_size(frame->analysis, value_type);
            if (place.kind != IR_RUNTIME_VALUE_PLACE || !expected.initialized || !desired.initialized || !size ||
                !ir_interpreter_memory_read(scratch.arena, frame->analysis, value_type, place.object, place.offset, &produced) || !produced.initialized)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            bool equal = produced.kind == expected.kind &&
                         ((produced.kind == IR_RUNTIME_VALUE_SCALAR && produced.bits == expected.bits) ||
                          (produced.kind == IR_RUNTIME_VALUE_ADDRESS && produced.object == expected.object && produced.offset == expected.offset));
            if (equal && !ir_interpreter_memory_write(scratch.arena, place.object, place.offset, size, desired))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_ATOMIC_FENCE:
            break;
        case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
            break;
        case IR_OPCODE_ARRAY:
        {
            AnalysisType* array_type = analysis_type_from_id(frame->analysis, instruction->type);
            AnalysisTypeId element_type = array_type->kind == ANALYSIS_TYPE_ARRAY    ? array_type->as.array.element_type
                                          : array_type->kind == ANALYSIS_TYPE_VECTOR ? array_type->as.vector.element_type
                                                                                     : array_type->as.element_type;
            u64 element_size = ir_interpreter_type_size(frame->analysis, element_type);
            u64 count = instruction->operand_count;
            u64 size = 0;
            if ((count && !element_size) || !ir_interpreter_u64_multiply(element_size, count, &size))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            IrRuntimeObject* object = ir_interpreter_object_create(scratch.arena, size);
            if (!object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            for (u32 element_index = 0; element_index < instruction->operand_count; element_index += 1)
            {
                if (!element_size || !ir_interpreter_memory_write(scratch.arena, object, (u64)element_index * element_size, element_size,
                                                                  frame->values[instruction->operands[element_index].value]))
                {
                    operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
            }
            produced = (IrRuntimeValue){
                .object = object,
                .length = count,
                .element_size = element_size,
                .kind = IR_RUNTIME_VALUE_AGGREGATE,
                .initialized = operation_trap == IR_EXECUTION_TRAP_NONE,
            };
        }
        break;
        case IR_OPCODE_AGGREGATE:
        {
            u64 size = ir_interpreter_type_size(frame->analysis, instruction->type);
            if (!size)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            IrRuntimeObject* object = ir_interpreter_object_create(scratch.arena, size);
            if (!object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
            {
                AnalysisField* field = 0;
                if (operand_index >= instruction->immediate_count ||
                    !ir_interpreter_field(frame, instruction->type, (u32)instruction->immediates[operand_index], &field))
                {
                    operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                u64 field_size = ir_interpreter_type_size(frame->analysis, field->type);
                if (!field_size || !ir_interpreter_memory_write(scratch.arena, object, field->offset, field_size,
                                                                 frame->values[instruction->operands[operand_index].value]))
                {
                    operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
            }
            produced = (IrRuntimeValue){
                .object = object,
                .length = size,
                .kind = IR_RUNTIME_VALUE_AGGREGATE,
                .initialized = operation_trap == IR_EXECUTION_TRAP_NONE,
            };
        }
        break;
        case IR_OPCODE_LENGTH:
        {
            IrValueId base_id = instruction->operands[0];
            IrRuntimeValue base = frame->values[base_id.value];
            AnalysisType* base_type = analysis_type_from_id(frame->analysis, frame->function->values[base_id.value].type);
            if (base_type->kind == ANALYSIS_TYPE_RANGE && base.kind == IR_RUNTIME_VALUE_RANGE)
            {
                AnalysisTypeId element_type = base_type->as.element_type;
                u32 width = ir_interpreter_type_width(frame->analysis, element_type);
                u64 mask = ir_interpreter_mask(width);
                u64 start = base.bits & mask;
                u64 end = base.length & mask;
                bool is_signed = ir_interpreter_type_signed(frame->analysis, element_type);
                s32 order = is_signed ? ir_interpreter_signed_compare(start, end, width) : start < end ? -1 : start > end ? 1 : 0;
                if (order > 0)
                {
                    operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                produced.bits = (end - start) & mask;
                break;
            }
            IrRuntimeObject* object = 0;
            u64 offset = 0;
            u64 count = 0;
            u64 element_size = 0;
            if (!ir_interpreter_collection(frame, frame->function->values[base_id.value].type, base, &object, &offset, &count, &element_size))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            BUSTER_UNUSED(object);
            BUSTER_UNUSED(offset);
            BUSTER_UNUSED(element_size);
            produced.bits = count;
        }
        break;
        case IR_OPCODE_INDEX:
        {
            IrValueId base_id = instruction->operands[0];
            IrRuntimeValue base = frame->values[base_id.value];
            u64 index = frame->values[instruction->operands[1].value].bits;
            AnalysisType* base_type = analysis_type_from_id(frame->analysis, frame->function->values[base_id.value].type);
            if (base_type->kind == ANALYSIS_TYPE_RANGE && base.kind == IR_RUNTIME_VALUE_RANGE)
            {
                AnalysisTypeId element_type = base_type->as.element_type;
                u32 width = ir_interpreter_type_width(frame->analysis, element_type);
                u64 mask = ir_interpreter_mask(width);
                u64 start = base.bits & mask;
                u64 end = base.length & mask;
                u64 count = (end - start) & mask;
                if (index >= count)
                {
                    operation_trap = IR_EXECUTION_TRAP_OUT_OF_BOUNDS;
                    break;
                }
                u64 element_index = base.reversed ? count - index - 1 : index;
                produced.bits = (start + element_index) & mask;
                break;
            }
            IrRuntimeObject* object = 0;
            u64 offset = 0;
            u64 count = 0;
            u64 element_size = 0;
            if (!ir_interpreter_collection(frame, frame->function->values[base_id.value].type, base, &object, &offset, &count, &element_size) || index >= count)
            {
                operation_trap = IR_EXECUTION_TRAP_OUT_OF_BOUNDS;
                break;
            }
            if (base.reversed)
            {
                index = count - index - 1;
            }
            u64 element_offset = 0;
            if (!ir_interpreter_u64_multiply(index, element_size, &element_offset) || !ir_interpreter_u64_add(offset, element_offset, &element_offset) ||
                !ir_interpreter_object_range_valid(object, element_offset, element_size))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = (IrRuntimeValue){
                .object = object,
                .offset = element_offset,
                .length = element_size,
                .kind = IR_RUNTIME_VALUE_PLACE,
                .initialized = true,
            };
            if (frame->function->values[instruction->result.value].category == IR_VALUE_VALUE)
            {
                if (!ir_interpreter_memory_read(scratch.arena, frame->analysis, instruction->type, produced.object, produced.offset, &produced))
                {
                    operation_trap = IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
                }
            }
        }
        break;
        case IR_OPCODE_SLICE:
        {
            IrValueId base_id = instruction->operands[0];
            IrRuntimeObject* object = 0;
            u64 offset = 0;
            u64 count = 0;
            u64 element_size = 0;
            if (instruction->immediate_count != 2 || !ir_interpreter_collection(frame, frame->function->values[base_id.value].type,
                                                                                frame->values[base_id.value], &object, &offset, &count, &element_size))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            bool has_start = instruction->immediates[0] != 0;
            bool has_end = instruction->immediates[1] != 0;
            u32 operand_index = 1;
            u64 start = has_start ? frame->values[instruction->operands[operand_index++].value].bits : 0;
            u64 end = has_end ? frame->values[instruction->operands[operand_index].value].bits : count;
            if (start > end || end > count)
            {
                operation_trap = IR_EXECUTION_TRAP_OUT_OF_BOUNDS;
                break;
            }
            u64 start_offset = 0;
            u64 slice_size = 0;
            if (!ir_interpreter_u64_multiply(start, element_size, &start_offset) || !ir_interpreter_u64_add(offset, start_offset, &start_offset) ||
                !ir_interpreter_u64_multiply(end - start, element_size, &slice_size) ||
                !ir_interpreter_object_range_valid(object, start_offset, slice_size))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = (IrRuntimeValue){
                .object = object,
                .offset = start_offset,
                .length = end - start,
                .element_size = element_size,
                .kind = IR_RUNTIME_VALUE_SLICE,
                .initialized = true,
            };
        }
        break;
        case IR_OPCODE_FIELD:
        {
            IrValueId base_id = instruction->operands[0];
            IrRuntimeValue base = frame->values[base_id.value];
            AnalysisField* field = 0;
            if (instruction->immediate_count != 1 ||
                !ir_interpreter_field(frame, frame->function->values[base_id.value].type, (u32)instruction->immediates[0], &field) ||
                (base.kind != IR_RUNTIME_VALUE_PLACE && base.kind != IR_RUNTIME_VALUE_AGGREGATE))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            u64 field_offset = 0;
            u64 field_size = ir_interpreter_type_size(frame->analysis, field->type);
            if (!field_size || !ir_interpreter_u64_add(base.offset, field->offset, &field_offset) ||
                !ir_interpreter_object_range_valid(base.object, field_offset, field_size))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = (IrRuntimeValue){
                .object = base.object,
                .offset = field_offset,
                .length = field_size,
                .kind = IR_RUNTIME_VALUE_PLACE,
                .initialized = true,
            };
            if (frame->function->values[instruction->result.value].category == IR_VALUE_VALUE)
            {
                if (!ir_interpreter_memory_read(scratch.arena, frame->analysis, instruction->type, produced.object, produced.offset, &produced))
                {
                    operation_trap = IR_EXECUTION_TRAP_UNINITIALIZED_VALUE;
                }
            }
        }
        break;
        case IR_OPCODE_FUNCTION:
        {
            produced.kind = IR_RUNTIME_VALUE_FUNCTION;
            produced.entity = instruction->entity;
            produced.instantiation = instruction->instantiation;
        }
        break;
        case IR_OPCODE_LABEL_ADDRESS:
        {
            if (instruction->target_count != 1)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            produced.bits = instruction->targets[0].value;
            produced.kind = IR_RUNTIME_VALUE_SCALAR;
            produced.label_function = frame->function->id;
            produced.has_label_provenance = true;
        }
        break;
        case IR_OPCODE_CAST:
        {
            IrRuntimeValue operand = frame->values[instruction->operands[0].value];
            AnalysisType* target_type = analysis_type_from_id(frame->analysis, instruction->type);
            if (operand.kind == IR_RUNTIME_VALUE_ADDRESS && target_type->kind == ANALYSIS_TYPE_POINTER &&
                instruction->conversion_operation == IR_CONVERSION_POINTER_REINTERPRET)
            {
                produced = operand;
            }
            else if (!ir_interpreter_cast(frame, instruction, &produced.bits))
            {
                operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
            }
        }
        break;
        case IR_OPCODE_UNARY:
        {
            if (instruction->unary_operation >= IR_UNARY_VECTOR_INTEGER_NEGATE && instruction->unary_operation <= IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
            {
                ir_interpreter_vector_unary(scratch.arena, frame, instruction, &produced, &operation_trap);
                break;
            }
            IrValueId operand_id = instruction->operands[0];
            u64 operand = frame->values[operand_id.value].bits;
            switch (instruction->unary_operation)
            {
            case IR_UNARY_FLOAT_NEGATE:
            {
                AnalysisType* type = analysis_type_from_id(frame->analysis, instruction->type);
                f64 value = ir_interpreter_float_read(operand, type->as.float_bit_width);
                produced.bits = ir_interpreter_float_write(-value, type->as.float_bit_width);
            }
            break;
            case IR_UNARY_INTEGER_NEGATE:
                produced.bits = 0 - operand;
                break;
            case IR_UNARY_BOOLEAN_NOT:
                produced.bits = !operand;
                break;
            case IR_UNARY_INTEGER_BITWISE_NOT:
                produced.bits = ~operand;
                break;
            case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS:
            case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS:
            {
                AnalysisType* type = analysis_type_from_id(frame->analysis, instruction->type);
                u32 width = type->as.integer.bit_width;
                u32 count = 0;
                if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS)
                {
                    u64 bit = (u64)1 << (width - 1);
                    while (count < width && !(operand & bit))
                    {
                        count += 1;
                        bit >>= 1;
                    }
                }
                else
                {
                    u64 bit = 1;
                    while (count < width && !(operand & bit))
                    {
                        count += 1;
                        bit <<= 1;
                    }
                }
                produced.bits = count;
            }
            break;
            case IR_UNARY_INTEGER_POPULATION_COUNT:
            {
                AnalysisType* type = analysis_type_from_id(frame->analysis, instruction->type);
                u32 width = type->as.integer.bit_width;
                u32 count = 0;
                for (u32 bit_index = 0; bit_index < width; bit_index += 1)
                {
                    count += (operand >> bit_index) & 1;
                }
                produced.bits = count;
            }
            break;
            case IR_UNARY_VECTOR_INTEGER_NEGATE:
            case IR_UNARY_VECTOR_FLOAT_NEGATE:
            case IR_UNARY_VECTOR_INTEGER_BITWISE_NOT:
            case IR_UNARY_COUNT:
            {
                operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
            }
            break;
            }
            if (instruction->unary_operation != IR_UNARY_FLOAT_NEGATE)
            {
                produced.bits = ir_interpreter_normalize_integer(frame->analysis, instruction->type, produced.bits);
            }
        }
        break;
        case IR_OPCODE_BINARY:
        {
            IrRuntimeValue left_value = frame->values[instruction->operands[0].value];
            IrRuntimeValue right_value = frame->values[instruction->operands[1].value];
            AnalysisType* operand_type = analysis_type_from_id(frame->analysis, frame->function->values[instruction->operands[0].value].type);
            if (operand_type->kind == ANALYSIS_TYPE_VECTOR)
            {
                ir_interpreter_vector_binary(scratch.arena, frame, instruction, &produced, &operation_trap);
                break;
            }
            if (left_value.kind == IR_RUNTIME_VALUE_ADDRESS || right_value.kind == IR_RUNTIME_VALUE_ADDRESS)
            {
                if (left_value.kind != IR_RUNTIME_VALUE_ADDRESS || right_value.kind != IR_RUNTIME_VALUE_ADDRESS)
                {
                    operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                    break;
                }
                bool same_object = left_value.object == right_value.object;
                switch (instruction->binary_operation)
                {
                case IR_BINARY_POINTER_EQUAL:
                    produced.bits = same_object && left_value.offset == right_value.offset;
                    break;
                case IR_BINARY_POINTER_NOT_EQUAL:
                    produced.bits = !same_object || left_value.offset != right_value.offset;
                    break;
                default:
                {
                    operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                }
                break;
                }
                break;
            }
            if (instruction->binary_operation == IR_BINARY_RANGE)
            {
                produced = (IrRuntimeValue){
                    .bits = frame->values[instruction->operands[0].value].bits,
                    .length = frame->values[instruction->operands[1].value].bits,
                    .element_size = ir_interpreter_type_size(frame->analysis, operand_type->id),
                    .kind = IR_RUNTIME_VALUE_RANGE,
                    .initialized = true,
                };
                break;
            }
            bool success = operand_type->kind == ANALYSIS_TYPE_FLOAT ? ir_interpreter_float_binary(frame, instruction, &produced.bits, &operation_trap)
                                                                     : ir_interpreter_integer_binary(frame, instruction, &produced.bits, &operation_trap);
            BUSTER_UNUSED(success);
        }
        break;
        case IR_OPCODE_CALL:
        {
            if (depth >= max_call_depth)
            {
                operation_trap = IR_EXECUTION_TRAP_CALL_DEPTH_LIMIT;
                break;
            }
            AnalysisEntityId callee_entity = instruction->entity;
            AnalysisInstantiationId callee_instantiation = instruction->instantiation;
            if (instruction->operand_count)
            {
                IrRuntimeValue reference = frame->values[instruction->operands[0].value];
                if (reference.kind == IR_RUNTIME_VALUE_FUNCTION)
                {
                    callee_entity = reference.entity;
                    callee_instantiation = reference.instantiation;
                }
            }
            IrExecutionTrap callee_trap = IR_EXECUTION_TRAP_NONE;
            IrExecutionTarget callee =
                ir_interpreter_function_resolve(scratch.arena, &runtime, analysis, program, callee_entity, callee_instantiation, &callee_trap);
            if (callee_trap != IR_EXECUTION_TRAP_NONE)
            {
                operation_trap = callee_trap;
                break;
            }
            u32 callee_argument_count = instruction->operand_count - 1;
            for (u32 argument_index = 0; argument_index < callee_argument_count; argument_index += 1)
            {
                frame->transition_values[argument_index] = frame->values[instruction->operands[argument_index + 1].value];
            }
            IrExecutionFrame* callee_frame = frames + depth;
            if (!ir_interpreter_frame_prepare(scratch.arena, callee_frame, callee, frame->transition_values, callee_argument_count, instruction->result))
            {
                operation_trap = IR_EXECUTION_TRAP_ARGUMENT_COUNT;
                break;
            }
            callee_frame->runtime = frame->runtime;
            frame->instruction = instruction->next;
            depth += 1;
            advance = false;
        }
        break;
        case IR_OPCODE_ADDRESS_OF:
        {
            IrRuntimeValue place = frame->values[instruction->operands[0].value];
            if (place.kind != IR_RUNTIME_VALUE_PLACE)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = place;
            produced.kind = IR_RUNTIME_VALUE_ADDRESS;
        }
        break;
        case IR_OPCODE_DEREFERENCE:
        {
            IrRuntimeValue address = frame->values[instruction->operands[0].value];
            if (address.kind != IR_RUNTIME_VALUE_ADDRESS || !address.object)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                break;
            }
            produced = address;
            produced.kind = IR_RUNTIME_VALUE_PLACE;
            produced.length = ir_interpreter_type_size(frame->analysis, instruction->type);
            if (!produced.length || !ir_interpreter_object_range_valid(produced.object, produced.offset, produced.length))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
            }
        }
        break;
        case IR_OPCODE_REVERSE:
        {
            produced = frame->values[instruction->operands[0].value];
            if (produced.kind != IR_RUNTIME_VALUE_RANGE && produced.kind != IR_RUNTIME_VALUE_SLICE && produced.kind != IR_RUNTIME_VALUE_AGGREGATE &&
                produced.kind != IR_RUNTIME_VALUE_PLACE)
            {
                operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                break;
            }
            produced.reversed = !produced.reversed;
        }
        break;
        case IR_OPCODE_VA_START:
        {
            AnalysisType* function_type = analysis_type_from_id(frame->analysis, frame->function->type);
            produced = (IrRuntimeValue){
                .va_arguments = frame->arguments,
                .va_index = function_type->as.function.argument_count,
                .va_count = frame->argument_count,
                .kind = IR_RUNTIME_VALUE_VA_LIST,
                .initialized = true,
            };
        }
        break;
        case IR_OPCODE_VA_COPY:
        case IR_OPCODE_VA_END:
        case IR_OPCODE_VA_ARG:
        {
            IrRuntimeValue address = frame->values[instruction->operands[0].value];
            IrRuntimeValue list = {0};
            if (address.kind != IR_RUNTIME_VALUE_ADDRESS ||
                !ir_interpreter_memory_read(scratch.arena, frame->analysis, frame->analysis->types.builtin.va_list_type, address.object, address.offset,
                                            &list) ||
                list.kind != IR_RUNTIME_VALUE_VA_LIST || list.va_ended)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            if (instruction->opcode == IR_OPCODE_VA_COPY)
            {
                produced = list;
            }
            else if (instruction->opcode == IR_OPCODE_VA_END)
            {
                list.va_ended = true;
                if (!ir_interpreter_memory_write(scratch.arena, address.object, address.offset,
                                                 ir_interpreter_type_size(frame->analysis, frame->analysis->types.builtin.va_list_type), list))
                {
                    operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                }
            }
            else
            {
                if (list.va_index >= list.va_count)
                {
                    operation_trap = IR_EXECUTION_TRAP_ARGUMENT_COUNT;
                    break;
                }
                produced = list.va_arguments[list.va_index];
                list.va_index += 1;
                if (!ir_interpreter_memory_write(scratch.arena, address.object, address.offset,
                                                 ir_interpreter_type_size(frame->analysis, frame->analysis->types.builtin.va_list_type), list))
                {
                    operation_trap = IR_EXECUTION_TRAP_INVALID_MEMORY;
                }
            }
        }
        break;
        case IR_OPCODE_INLINE_ASSEMBLY:
        {
            if (!instruction->target_count)
            {
                operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                break;
            }
            IrBlockId target = instruction->targets[0];
            u32 jump_target_index = 0;
            String8 assembly_literal = ir_instruction_extra(frame->function, instruction->id).literal;
            bool jump_label = ir_interpreter_inline_assembly_jump_target(frame->function, instruction, assembly_literal, S8("jmp %l"), &jump_target_index) ||
                              ir_interpreter_inline_assembly_jump_target(frame->function, instruction, assembly_literal, S8("b %l"), &jump_target_index);
            if (jump_label)
            {
                target = instruction->targets[jump_target_index];
            }
            else if (assembly_literal.length)
            {
                operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
                break;
            }
            if (!ir_interpreter_block_enter(frame, target, frame->block))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
            }
            advance = false;
        }
        break;
        case IR_OPCODE_BRANCH:
        {
            IrBlockId predecessor = frame->block;
            if (instruction->target_count != 1 || !ir_interpreter_block_enter(frame, instruction->targets[0], predecessor))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
            }
            advance = false;
        }
        break;
        case IR_OPCODE_BRANCH_IF:
        {
            if (instruction->target_count != 2)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            IrBlockId predecessor = frame->block;
            u64 condition = frame->values[instruction->operands[0].value].bits;
            IrBlockId target = instruction->targets[condition ? 0 : 1];
            if (!ir_interpreter_block_enter(frame, target, predecessor))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
            }
            advance = false;
        }
        break;
        case IR_OPCODE_SWITCH:
        {
            if (!instruction->target_count || instruction->target_count != instruction->immediate_count + 1)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            u64 switched = frame->values[instruction->operands[0].value].bits;
            u32 target_index = instruction->immediate_count;
            for (u32 value_index = 0; value_index < instruction->immediate_count; value_index += 1)
            {
                if (instruction->immediates[value_index] == switched)
                {
                    target_index = value_index;
                    break;
                }
            }
            IrBlockId predecessor = frame->block;
            if (!ir_interpreter_block_enter(frame, instruction->targets[target_index], predecessor))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
            }
            advance = false;
        }
        break;
        case IR_OPCODE_INDIRECT_BRANCH:
        {
            IrRuntimeValue target_value = instruction->operand_count == 1 ? frame->values[instruction->operands[0].value] : (IrRuntimeValue){0};
            if (instruction->operand_count != 1 || !instruction->target_count || !target_value.has_label_provenance ||
                target_value.label_function.value != frame->function->id.value)
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
                break;
            }
            u64 label = target_value.bits;
            IrBlockId target = IR_BLOCK_ID_INVALID;
            for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
            {
                if (instruction->targets[target_index].value == label)
                {
                    target = instruction->targets[target_index];
                    break;
                }
            }
            if (target.value == IR_ID_UNDERLYING_INVALID || !ir_interpreter_block_enter(frame, target, frame->block))
            {
                operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
            }
            advance = false;
        }
        break;
        case IR_OPCODE_RETURN:
        {
            IrRuntimeValue returned = {0};
            bool has_value = instruction->operand_count == 1;
            if (has_value)
            {
                returned = frame->values[instruction->operands[0].value];
            }
            IrValueId caller_result = frame->caller_result;
            AnalysisType* function_type = analysis_type_from_id(frame->analysis, frame->function->type);
            AnalysisModuleId type_module = frame->analysis->module.id;
            AnalysisTypeId return_type = function_type->as.function.return_type;
            depth -= 1;
            if (!depth)
            {
                IrExecutionResult result = {
                    .function = entry,
                    .instantiation = instantiation,
                    .type_module = type_module,
                    .type = return_type,
                    .instruction = instruction->id,
                    .bits = returned.bits,
                    .step_count = step_count,
                    .trap = IR_EXECUTION_TRAP_NONE,
                    .has_value = has_value,
                };
                if (has_value)
                {
                    result.value = ir_interpreter_public_value(execution_arena, analysis, frame->runtime, returned, return_type);
                }
                arena_set_position(scratch.arena, scratch.position);
                return result;
            }
            IrExecutionFrame* caller = frames + depth - 1;
            if (caller_result.value != IR_ID_UNDERLYING_INVALID)
            {
                returned.type_module = caller->analysis->module.id;
                returned.type = caller->function->values[caller_result.value].type;
                caller->values[caller_result.value] = returned;
            }
            advance = false;
        }
        break;
        case IR_OPCODE_SIMD:
        {
            // The target-fixed 512-bit vocabulary is a native-codegen
            // construct; the interpreter is an oracle for the Buster language
            // and gains nothing from a second, differently rounded software
            // model of it.
            operation_trap = IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION;
        }
        break;
        case IR_OPCODE_DEBUG_TRAP:
        {
            operation_trap = IR_EXECUTION_TRAP_DEBUG;
        }
        break;
        case IR_OPCODE_UNREACHABLE:
        {
            operation_trap = IR_EXECUTION_TRAP_UNREACHABLE;
        }
        break;
        case IR_OPCODE_COUNT:
        {
            operation_trap = IR_EXECUTION_TRAP_INVALID_PROGRAM;
        }
        break;
        }
        if (operation_trap != IR_EXECUTION_TRAP_NONE)
        {
            IrExecutionResult result = ir_interpreter_trap(frame, operation_trap, step_count);
            arena_set_position(scratch.arena, scratch.position);
            return result;
        }
        if (instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->opcode != IR_OPCODE_CALL)
        {
            produced.type_module = frame->analysis->module.id;
            produced.type = instruction->type;
            frame->values[instruction->result.value] = produced;
        }
        if (advance)
        {
            frame->instruction = instruction->next;
        }
    }
    arena_set_position(scratch.arena, scratch.position);
    return ir_interpreter_trap(0, IR_EXECUTION_TRAP_INVALID_PROGRAM, step_count);
}
