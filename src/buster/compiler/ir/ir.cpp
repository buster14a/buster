#pragma once

#include <buster/compiler/ir/ir.h>
#include <buster/string.h>
#include <buster/os.h>
#include <buster/arena.h>
#include <buster/integer.h>

#define XXH_IMPLEMENTATION
#define XXH_STATIC_LINKING_ONLY
#define XXH_INLINE_ALL
#include <xxhash/xxhash.h>

BUSTER_GLOBAL_LOCAL constexpr u64 first_arena_multiplier = sizeof(u64);
BUSTER_GLOBAL_LOCAL constexpr u64 second_arena_multiplier = sizeof(u32);
BUSTER_GLOBAL_LOCAL constexpr u64 divisor = first_arena_multiplier + second_arena_multiplier;

template<typename T>
BUSTER_GLOBAL_LOCAL T ref(u32 value) { BUSTER_CHECK(value != UINT32_MAX); return { value + 1 }; }

template <typename T>
BUSTER_GLOBAL_LOCAL T* pointer_from_ref(Arena* arena, TypedIrRef<T> reference)
{
    T* result = 0;

    if (reference.is_valid())
    {
        result = arena_get_pointer_at_index(arena, T, reference.get());
    }

    return result;
}

BUSTER_F_IMPL IrDebugType* ir_debug_type_get(IrModule* module, IrDebugTypeRef reference)
{
    return pointer_from_ref(module->types.arenas.debug, reference);
}

BUSTER_GLOBAL_LOCAL IrBlock* ir_block_get(IrModule* module, IrBlockRef reference)
{
    return pointer_from_ref(module->block_arena, reference);
}

BUSTER_INLINE u64 get_hash_table_allocation_size(InternTable* table)
{
    let byte_size = table->hash_table_arena->position - arena_minimum_position;
    return byte_size;
}

BUSTER_INLINE u32 get_hash_table_capacity(InternTable* table)
{
    let byte_size = get_hash_table_allocation_size(table);
    BUSTER_CHECK(byte_size % divisor == 0);
    return (u32)(byte_size / divisor);
}

BUSTER_INLINE u32* get_slot_index(InternTable* table, u32 index)
{
    let capacity = get_hash_table_capacity(table);
    BUSTER_CHECK(index < capacity);
    let result = (u32*)((u8*)table->hash_table_arena + arena_minimum_position + first_arena_multiplier * capacity);
    return result;
}

BUSTER_INLINE u32 get_slot_count(InternTable* table)
{
    let byte_size = table->slot_arena->position - arena_minimum_position;
    BUSTER_CHECK(byte_size % sizeof(InternSlot) == 0);
    return (u32)(byte_size / sizeof(InternSlot));
}

BUSTER_GLOBAL_LOCAL constexpr u64 intern_table_start_element_count = 1 << 10;

BUSTER_GLOBAL_LOCAL void table_allocate(InternTable* table, u32 count)
{
    BUSTER_CHECK(table->hash_table_arena->position == arena_minimum_position);
    u64 allocation_size = (u64)count * divisor;
    let allocation = arena_allocate_bytes(table->hash_table_arena, allocation_size, alignof(u64));
    memset(allocation, 0, allocation_size);
}
 
BUSTER_GLOBAL_LOCAL void table_grow(InternTable* table)
{
    let current_capacity = get_hash_table_capacity(table);
    BUSTER_CHECK(get_hash_table_allocation_size(table) % divisor == 0);
    BUSTER_CHECK(current_capacity == get_hash_table_allocation_size(table) / 12);
    let target_capacity = current_capacity << 1;

    if (current_capacity)
    {
        let scratch = scratch_begin(0, 0);

        let allocation = arena_allocate_bytes(scratch.arena, get_hash_table_allocation_size(table), alignof(u64));
        memcpy(allocation, (u8*)table->hash_table_arena + arena_minimum_position, table->hash_table_arena->position - arena_minimum_position);
        table_allocate(table, target_capacity);

        scratch_end(scratch);
    }
    else
    {
        table_allocate(table, target_capacity);
    }
    // TODO, this is wrong
    BUSTER_TRAP();
}

BUSTER_GLOBAL_LOCAL u64 hash_string(String8 string)
{
    return XXH3_64bits(string.pointer, string.length);
}

BUSTER_GLOBAL_LOCAL IrInternRef table_intern(InternTable* restrict table, String8 string)
{
    let candidate_hash = hash_string(string);
    BUSTER_CHECK(candidate_hash != 0);
    let current_capacity = get_hash_table_capacity(table);

    if ((table->hash_table_item_count + 1) * 100 / current_capacity > 70)
    {
        table_grow(table);
    }

    BUSTER_CHECK(is_power_of_two(current_capacity));
    let mask = current_capacity - 1;
    u64* restrict hash_pointer = arena_get_pointer_at_position(table->hash_table_arena, u64, arena_minimum_position);
    InternSlot* slot_pointer = arena_get_pointer_at_position(table->slot_arena, InternSlot, arena_minimum_position);

    u32 index;
    for (index = (u32)candidate_hash & mask;; index = (index + 1) & mask)
    {
        let hash = hash_pointer[index];

        if ((hash == 0) | (hash == candidate_hash))
        {
            break;
        }
    }

    let hash = hash_pointer[index];

    IrInternRef result;

    if (hash == 0)
    {
        table->hash_table_item_count += 1;
        hash_pointer[index] = candidate_hash;
        *get_slot_index(table, index) = get_slot_count(table);
        let interned_string = string8_duplicate_arena(table->string_arena, string, false);
        let slot = arena_allocate(table->slot_arena, InternSlot, 1);
        *slot = {
            .name = interned_string,
            .hash = candidate_hash,
        };
        result = ref<IrInternRef>(index);
    }
    else if (hash == candidate_hash)
    {
        let candidate_slot = slot_pointer[*get_slot_index(table, index)];
        if (string8_equal(string, candidate_slot.name))
        {
            BUSTER_TRAP();
        }
        else
        {
            BUSTER_TRAP();
            // for (u32 collision_index = index;; collision_index = (collision_index + 1) & mask)
            // {
            //     let hash = hash_pointer[index];
            //
            //     if ((hash == 0) | (hash == candidate_hash))
            //     {
            //         if (a) BUSTER_TRAP();
            //     }
            // }
        }
    }
    else
    {
        BUSTER_UNREACHABLE();
    }

    return result;
}

BUSTER_F_IMPL IrInternRef ir_module_intern(IrModule* module, String8 string)
{
    return table_intern(&module->intern_table, string);
}

BUSTER_F_IMPL IrInternRef table_prepend(InternTable* table, IrInternRef after, String8 before)
{
    if (table && after.v == UINT32_MAX && before.pointer) BUSTER_TRAP();
    return {};
}

BUSTER_GLOBAL_LOCAL IrType* allocate_ir_types(IrModule* module, u64 count)
{
    let result = arena_allocate(module->types.arenas.ir, IrType, count);
    return result;
}

BUSTER_GLOBAL_LOCAL IrDebugType* allocate_ir_debug_types(IrModule* module, u64 count)
{
    let result = arena_allocate(module->types.arenas.debug, IrDebugType, count);
    return result;
}

BUSTER_GLOBAL_LOCAL u32 arena_get_index(Arena* arena, void* pointer, u64 element_size)
{
    let arena_start = (u8*)arena + arena_minimum_position;
    let arena_end = (u8*)arena + arena->position;
    let p = (u8*)pointer;
    BUSTER_CHECK(p >= arena_start && p < arena_end);
    let byte_offset = (u64)(p - arena_start);
    BUSTER_CHECK(byte_offset % element_size == 0);
    let result = (u32)(byte_offset / element_size);
    return result;
}

template <typename T>
BUSTER_GLOBAL_LOCAL TypedIrRef<T> ref_from_pointer(Arena* arena, T* pointer)
{
    return ref<TypedIrRef<T>>(arena_get_index(arena, pointer, sizeof(*pointer)));
}

BUSTER_GLOBAL_LOCAL IrTypeRef ir_type_get_ref(IrModule* module, IrType* pointer)
{
    return ref_from_pointer(module->types.arenas.ir, pointer);
}

BUSTER_GLOBAL_LOCAL IrDebugTypeRef ir_debug_type_get_ref(IrModule* module, IrDebugType* pointer)
{
    return ref_from_pointer(module->types.arenas.debug, pointer);
}

BUSTER_GLOBAL_LOCAL IrValueRef ir_value_get_ref(IrModule* module, IrValue* pointer)
{
    return ref_from_pointer(module->value_arena, pointer);
}

BUSTER_GLOBAL_LOCAL IrBlockRef ir_block_get_ref(IrModule* module, IrBlock* pointer)
{
    return ref_from_pointer(module->block_arena, pointer);
}

BUSTER_GLOBAL_LOCAL InternTable intern_table_create()
{
    InternTable result = {
        .string_arena = arena_create({}),
        .slot_arena = arena_create({}),
        .hash_table_arena = arena_create({}),
    };

    table_allocate(&result, intern_table_start_element_count);
    return result;
}

template <typename T>
BUSTER_GLOBAL_LOCAL Slice<T> ir_module_allocate_array(IrModule* module, u64 count)
{
    Slice<T> result = {
        .pointer = arena_allocate(module->untyped_arena, T, count),
        .length = count,
    };

    return result;
}

BUSTER_F_IMPL Slice<IrInternRef> ir_module_allocate_name_array(IrModule* module, u64 count)
{
    return ir_module_allocate_array<IrInternRef>(module, count);
}

BUSTER_F_IMPL Slice<IrDebugTypeRef> ir_module_allocate_debug_type_array(IrModule* module, u64 count)
{
    return ir_module_allocate_array<IrDebugTypeRef>(module, count);
}

BUSTER_F_IMPL Slice<IrTypeRef> ir_module_allocate_type_array(IrModule* module, u64 count)
{
    return ir_module_allocate_array<IrTypeRef>(module, count);
}

BUSTER_F_IMPL IrModule* ir_module_create(Arena* arena, Target* target, String8 name)
{
    let module = arena_allocate(arena, IrModule, 1);
    *module = (IrModule){
        .untyped_arena = arena,
        .default_target = target ? target : &target_native,
        .function_arena = arena_create({}),
        .value_arena = arena_create({}),
        .global_variable_arena = arena_create({}),
        .block_arena = arena_create({}),
        .statement_arena = arena_create({}),
        .intern_table = intern_table_create(),
        .types = {
            .arenas = {
                .ir = arena_create({}),
                .debug = arena_create({}),
            },
        },
        .name = name,
    };

    for (u64 i = 0; i < (u64)IrConstantDataId::Count; i += 1)
    {
        module->constant_arenas[i] = arena_create({});
    }

    char8 buffer[3];
    String8 slice = BUSTER_ARRAY_TO_SLICE(buffer);
    bool is_signed_values[] = { false, true };

    let builtin_debug_types = allocate_ir_debug_types(module, ir_debug_type_builtin_count);

    constexpr u32 contiguous_integer_type_count = sizeof(u64) * 8;

    for (bool is_signed : is_signed_values)
    {
        char8 signed_char = is_signed ? 's' : 'u';

        for (u32 i = 0; i < contiguous_integer_type_count; i += 1)
        {
            let bit_count = i + 1;
            buffer[0] = signed_char;
            buffer[1] = '0' + BUSTER_SELECT(bit_count > 9, (char8)(bit_count / 10), (char8)bit_count);
            buffer[2] = '0' + (char8)(bit_count % 10);
            let string = slice;
            string.length = 2 + (bit_count > 9);

            let type_name = table_intern(&module->intern_table, string);
            let index = is_signed * contiguous_integer_type_count + i;
            builtin_debug_types[index] = {
                .name = type_name,
                .id = (IrDebugTypeId)index,
            };
            module->types.builtin.debug[index] = ref<IrDebugTypeRef>(index);
        }
    }

    // TODO: fill extra debug types

    let builtin_ir_types = arena_allocate(module->types.arenas.ir, IrType, ir_type_builtin_count);

    for (u32 i = 0; i < contiguous_integer_type_count; i += 1)
    {
        let bit_count = i + 1;

        buffer[0] = 'i';
        buffer[1] = '0' + BUSTER_SELECT(bit_count > 9, (char8)(bit_count / 10), (char8)bit_count);
        buffer[2] = '0' + (char8)(bit_count % 10);
        let string = slice;
        string.length = 2 + (bit_count > 9);

        let type_name = table_intern(&module->intern_table, string);
        let index = i;
        builtin_ir_types[index] = {
            .name = type_name,
            .id = (IrTypeId)index,
        };
        module->types.builtin.ir[index] = ref<IrTypeRef>(index);
    }

    return module;
}

BUSTER_GLOBAL_LOCAL IrTypeRef ir_function_type_get(IrModule* module, IrFunctionType create)
{
    BUSTER_UNUSED(module);
    BUSTER_UNUSED(create);
    return {};
    // if (!create.target)
    // {
    //     create.target = module->default_target;
    // }
    //
    // let function_type = arena_allocate(module->ir_type_arena, IrType, 1);
    // // *function_type = create;
    // return function_type;
}

BUSTER_GLOBAL_LOCAL IrCallingConvention resolve_calling_convention(IrCallingConvention calling_convention, Target* target)
{
    switch (calling_convention)
    {
        break; case IrCallingConvention::C:
        {
            switch (target->cpu_arch)
            {
                break; case CpuArch::CPU_ARCH_X86_64:
                {
                    switch (target->os)
                    {
                        break; case OperatingSystem::OPERATING_SYSTEM_LINUX: calling_convention = IrCallingConvention::SystemV;
                        break; case OperatingSystem::OPERATING_SYSTEM_MACOS: calling_convention = IrCallingConvention::SystemV;
                        break; case OperatingSystem::OPERATING_SYSTEM_WINDOWS: calling_convention = IrCallingConvention::Win64;
                        break; case OperatingSystem::OPERATING_SYSTEM_UEFI: calling_convention = IrCallingConvention::Win64;
                        break; case OperatingSystem::OPERATING_SYSTEM_ANDROID: os_fail();
                        break; case OperatingSystem::OPERATING_SYSTEM_IOS: os_fail();
                        break; case OperatingSystem::OPERATING_SYSTEM_FREESTANDING: calling_convention = IrCallingConvention::SystemV;
                        break; case OperatingSystem::Count: BUSTER_UNREACHABLE();
                    }
                }
                break; case CpuArch::CPU_ARCH_AARCH64:
                {
                    os_fail();
                }
                break; case CpuArch::Count: BUSTER_UNREACHABLE();
            }
        }
        break; default: break;
    }

    return calling_convention;
}

BUSTER_GLOBAL_LOCAL bool calling_convention_is_resolved(IrCallingConvention calling_convention)
{
    return calling_convention != IrCallingConvention::C;
}

// ENUM(ClassSystemV,
//     None,
//     Integer,
//     Sse,
//     SseUp,
//     X87,
//     X87Up,
//     ComplexX87,
//     Memory);
//
// STRUCT(ClassificationSystemV)
// {
//     ClassSystemV classes[2];
// };
//
// BUSTER_GLOBAL_LOCAL ClassificationSystemV classify_type(IrModule* module, IrDebugTypeRef type_ref)
// {
//     let type = ir_debug_type_get(module, type_ref);
//
//     ClassificationSystemV result = {};
//
//     switch (type->id)
//     {
//         break;
//         case IrDebugTypeId::u1:
//         case IrDebugTypeId::u2:
//         case IrDebugTypeId::u3:
//         case IrDebugTypeId::u4:
//         case IrDebugTypeId::u5:
//         case IrDebugTypeId::u6:
//         case IrDebugTypeId::u7:
//         case IrDebugTypeId::u8:
//         case IrDebugTypeId::u9:
//         case IrDebugTypeId::u10:
//         case IrDebugTypeId::u11:
//         case IrDebugTypeId::u12:
//         case IrDebugTypeId::u13:
//         case IrDebugTypeId::u14:
//         case IrDebugTypeId::u15:
//         case IrDebugTypeId::u16:
//         case IrDebugTypeId::u17:
//         case IrDebugTypeId::u18:
//         case IrDebugTypeId::u19:
//         case IrDebugTypeId::u20:
//         case IrDebugTypeId::u21:
//         case IrDebugTypeId::u22:
//         case IrDebugTypeId::u23:
//         case IrDebugTypeId::u24:
//         case IrDebugTypeId::u25:
//         case IrDebugTypeId::u26:
//         case IrDebugTypeId::u27:
//         case IrDebugTypeId::u28:
//         case IrDebugTypeId::u29:
//         case IrDebugTypeId::u30:
//         case IrDebugTypeId::u31:
//         case IrDebugTypeId::u32:
//         case IrDebugTypeId::u33:
//         case IrDebugTypeId::u34:
//         case IrDebugTypeId::u35:
//         case IrDebugTypeId::u36:
//         case IrDebugTypeId::u37:
//         case IrDebugTypeId::u38:
//         case IrDebugTypeId::u39:
//         case IrDebugTypeId::u40:
//         case IrDebugTypeId::u41:
//         case IrDebugTypeId::u42:
//         case IrDebugTypeId::u43:
//         case IrDebugTypeId::u44:
//         case IrDebugTypeId::u45:
//         case IrDebugTypeId::u46:
//         case IrDebugTypeId::u47:
//         case IrDebugTypeId::u48:
//         case IrDebugTypeId::u49:
//         case IrDebugTypeId::u50:
//         case IrDebugTypeId::u51:
//         case IrDebugTypeId::u52:
//         case IrDebugTypeId::u53:
//         case IrDebugTypeId::u54:
//         case IrDebugTypeId::u55:
//         case IrDebugTypeId::u56:
//         case IrDebugTypeId::u57:
//         case IrDebugTypeId::u58:
//         case IrDebugTypeId::u59:
//         case IrDebugTypeId::u60:
//         case IrDebugTypeId::u61:
//         case IrDebugTypeId::u62:
//         case IrDebugTypeId::u63:
//         case IrDebugTypeId::u64:
//         case IrDebugTypeId::s1:
//         case IrDebugTypeId::s2:
//         case IrDebugTypeId::s3:
//         case IrDebugTypeId::s4:
//         case IrDebugTypeId::s5:
//         case IrDebugTypeId::s6:
//         case IrDebugTypeId::s7:
//         case IrDebugTypeId::s8:
//         case IrDebugTypeId::s9:
//         case IrDebugTypeId::s10:
//         case IrDebugTypeId::s11:
//         case IrDebugTypeId::s12:
//         case IrDebugTypeId::s13:
//         case IrDebugTypeId::s14:
//         case IrDebugTypeId::s15:
//         case IrDebugTypeId::s16:
//         case IrDebugTypeId::s17:
//         case IrDebugTypeId::s18:
//         case IrDebugTypeId::s19:
//         case IrDebugTypeId::s20:
//         case IrDebugTypeId::s21:
//         case IrDebugTypeId::s22:
//         case IrDebugTypeId::s23:
//         case IrDebugTypeId::s24:
//         case IrDebugTypeId::s25:
//         case IrDebugTypeId::s26:
//         case IrDebugTypeId::s27:
//         case IrDebugTypeId::s28:
//         case IrDebugTypeId::s29:
//         case IrDebugTypeId::s30:
//         case IrDebugTypeId::s31:
//         case IrDebugTypeId::s32:
//         case IrDebugTypeId::s33:
//         case IrDebugTypeId::s34:
//         case IrDebugTypeId::s35:
//         case IrDebugTypeId::s36:
//         case IrDebugTypeId::s37:
//         case IrDebugTypeId::s38:
//         case IrDebugTypeId::s39:
//         case IrDebugTypeId::s40:
//         case IrDebugTypeId::s41:
//         case IrDebugTypeId::s42:
//         case IrDebugTypeId::s43:
//         case IrDebugTypeId::s44:
//         case IrDebugTypeId::s45:
//         case IrDebugTypeId::s46:
//         case IrDebugTypeId::s47:
//         case IrDebugTypeId::s48:
//         case IrDebugTypeId::s49:
//         case IrDebugTypeId::s50:
//         case IrDebugTypeId::s51:
//         case IrDebugTypeId::s52:
//         case IrDebugTypeId::s53:
//         case IrDebugTypeId::s54:
//         case IrDebugTypeId::s55:
//         case IrDebugTypeId::s56:
//         case IrDebugTypeId::s57:
//         case IrDebugTypeId::s58:
//         case IrDebugTypeId::s59:
//         case IrDebugTypeId::s60:
//         case IrDebugTypeId::s61:
//         case IrDebugTypeId::s62:
//         case IrDebugTypeId::s63:
//         case IrDebugTypeId::s64:
//         case IrDebugTypeId::pointer:
//         {
//             result.classes[0] = ClassSystemV::Integer;
//         }
//         break;
//         case IrDebugTypeId::u128:
//         case IrDebugTypeId::s128:
//         {
//             result.classes[0] = ClassSystemV::Integer;
//             result.classes[1] = ClassSystemV::Integer;
//         }
//         break;
//         case IrDebugTypeId::Void: case IrDebugTypeId::NoReturn: { }
//         break; case IrDebugTypeId::hf16: BUSTER_UNREACHABLE();
//         break;
//         case IrDebugTypeId::f16:
//         case IrDebugTypeId::f32:
//         case IrDebugTypeId::f64:
//         {
//             result.classes[0] = ClassSystemV::Sse;
//         }
//         break; case IrDebugTypeId::f128:
//         {
//             result.classes[0] = ClassSystemV::Sse;
//             result.classes[1] = ClassSystemV::SseUp;
//         }
//         break; case IrDebugTypeId::vector: BUSTER_UNREACHABLE();
//         break; case IrDebugTypeId::aggregate: BUSTER_UNREACHABLE();
//         break; case IrDebugTypeId::function: BUSTER_UNREACHABLE();
//         break; case IrDebugTypeId::Count: BUSTER_UNREACHABLE();
//     }
//
//     return result;
// }

// BUSTER_GLOBAL_LOCAL void classify_function_systemv(IrModule* module, IrDebugTypeRef return_type, Slice<IrDebugTypeRef> argument_types)
// {
//     BUSTER_UNUSED(module);
//     BUSTER_UNUSED(return_type);
//     BUSTER_UNUSED(argument_types);
//
//     u32 gpr_argument_count = 0;
//     constexpr u32 max_gpr_argument_count = 6;
//
//     for (auto argument_type_ref : argument_types)
//     {
//         let classification = classify_type(module, argument_type_ref);
//         if (classification.classes[0] == ClassSystemV::Memory)
//         {
//             BUSTER_TRAP();
//         }
//         else if (classification.classes[0] == ClassSystemV::Integer)
//         {
//             bool is_big_integer = classification.classes[1] == ClassSystemV::Integer;
//             let required_registers = (u32)1 + is_big_integer;
//             if (gpr_argument_count + required_registers <= max_gpr_argument_count)
//             {
//                 gpr_argument_count += required_registers;
//             }
//             else
//             {
//                 BUSTER_TRAP();
//             }
//         }
//         else
//         {
//             BUSTER_TRAP();
//         }
//     }
//
//     BUSTER_TRAP();
// }

// BUSTER_GLOBAL_LOCAL void classify_function(IrModule* module, IrDebugTypeRef return_type, Slice<IrDebugTypeRef> argument_types, IrCallingConvention calling_convention)
// {
//     switch (calling_convention)
//     {
//         break; case IrCallingConvention::C: BUSTER_UNREACHABLE();
//         break; case IrCallingConvention::SystemV:
//         {
//             classify_function_systemv(module, return_type, argument_types);
//         }
//         break; case IrCallingConvention::Win64:
//         {
//             BUSTER_TRAP();
//         }
//         break; case IrCallingConvention::Count: BUSTER_UNREACHABLE();
//     }
// }

BUSTER_F_IMPL IrType* ir_type_get(IrModule* module, IrTypeRef reference)
{
    IrType* result = 0;

    if (reference.is_valid())
    {
        result = arena_get_pointer_at_index(module->types.arenas.ir, IrType, reference.get());
    }

    return result;
}

BUSTER_F_IMPL IrTypeRef ir_type_get_builtin(IrModule* module, IrTypeId id)
{
    IrTypeRef result{};

    if (id < (IrTypeId)ir_type_builtin_count)
    {
        result = module->types.builtin.ir[(u64)id];
    }

    return result;
}

BUSTER_F_IMPL IrDebugTypeRef ir_debug_type_get_builtin(IrModule* module, IrDebugTypeId id)
{
    IrDebugTypeRef result{};

    if (id < (IrDebugTypeId)ir_debug_type_builtin_count)
    {
        result = module->types.builtin.debug[(u64)id];
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrTypeRef ir_type_get_or_create(IrModule* module, IrTypeRef* first_pointer, void* context, bool (*compare)(IrType*, void*), void (*fill)(IrModule*, IrType*, void* context))
{
    IrTypeRef result{};

    IrTypeRef previous_valid_ref = *first_pointer;
    IrTypeRef it = previous_valid_ref;

    while (it.is_valid())
    {
        let i = ir_type_get(module, it);

        if (compare(i, context))
        {
            break;
        }

        previous_valid_ref = it;
        it = i->kind_next;
    }

    if (it.is_valid())
    {
        result = it;
    }
    else
    {
        let new_type = allocate_ir_types(module, 1);

        if (previous_valid_ref.is_valid())
        {
            BUSTER_CHECK(first_pointer->is_valid());
            let previous_valid = ir_type_get(module, previous_valid_ref);
            BUSTER_CHECK(!previous_valid->kind_next.is_valid());
            previous_valid->kind_next = result;
        }
        else
        {
            BUSTER_CHECK(!first_pointer->is_valid());
            *first_pointer = result;
        }

        result = ir_type_get_ref(module, new_type);
        fill(module, new_type, context);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrDebugTypeRef ir_debug_type_get_or_create(IrModule* module, IrDebugTypeRef* first_pointer, void* context, bool (*compare)(IrDebugType*, void*), void (*fill)(IrModule*, IrDebugType*, void* context))
{
    IrDebugTypeRef result{};

    IrDebugTypeRef previous_valid_ref = *first_pointer;
    IrDebugTypeRef it = previous_valid_ref;

    while (it.is_valid())
    {
        let i = ir_debug_type_get(module, it);

        if (compare(i, context))
        {
            break;
        }

        previous_valid_ref = it;
        it = i->kind_next;
    }

    if (it.is_valid())
    {
        result = it;
    }
    else
    {
        let new_type = allocate_ir_debug_types(module, 1);

        if (previous_valid_ref.is_valid())
        {
            BUSTER_CHECK(first_pointer->is_valid());
            let previous_valid = ir_debug_type_get(module, previous_valid_ref);
            BUSTER_CHECK(!previous_valid->kind_next.is_valid());
            previous_valid->kind_next = result;
        }
        else
        {
            BUSTER_CHECK(!first_pointer->is_valid());
            *first_pointer = result;
        }

        result = ir_debug_type_get_ref(module, new_type);
        fill(module, new_type, context);
    }

    return result;
}

BUSTER_F_IMPL IrDebugTypeRef ir_debug_type_get_pointer(IrModule* module, IrDebugTypeRef element_type_ref)
{
    return ir_debug_type_get_or_create(module, &module->types.debug.first_pointer_type, &element_type_ref,
            [](IrDebugType* i, void* context) -> bool
            {
                BUSTER_CHECK(i->id == IrDebugTypeId::pointer);
                let etr = *(IrDebugTypeRef*)context;

                return etr == i->pointer.element_type;
            },
            [](IrModule* m, IrDebugType* new_type, void* context)
            {
                let etr = *(IrDebugTypeRef*)context;
                let element_type = ir_debug_type_get(m, etr);

                *new_type = {
                    .pointer = {
                        .element_type = etr,
                    },
                    .name = table_prepend(&m->intern_table, element_type->name, S8("&")),
                    .kind_next = {},
                    .id = IrDebugTypeId::pointer,
                };
            });
}

BUSTER_GLOBAL_LOCAL bool compare_function_at_creation(IrDebugType* function_type, IrDebugTypeFunctionCreationArguments* arguments)
{
    BUSTER_CHECK(function_type->id == IrDebugTypeId::function);
    BUSTER_UNUSED(arguments);
    BUSTER_TRAP();
}

BUSTER_GLOBAL_LOCAL void fill_function_type(IrModule* module, IrDebugType* function_type, IrDebugTypeFunctionCreationArguments* arguments)
{
    BUSTER_UNUSED(module);
    *function_type = {
        .function = {
            .argument_types = arguments->argument_types.pointer,
            .return_type = arguments->return_type,
            .argument_count = (u16)arguments->argument_types.length,
            .attributes = arguments->attributes,
        },
        .name = {},
        .kind_next = {},
        .id = IrDebugTypeId::function,
    };
}

BUSTER_F_IMPL IrDebugTypeRef ir_debug_type_get_function(IrModule* module, IrDebugTypeFunctionCreationArguments arguments)
{
    return ir_debug_type_get_or_create(module, &module->types.debug.first_function_type, &arguments,
            [](IrDebugType* i, void* context) -> bool
            {
                let a = (IrDebugTypeFunctionCreationArguments*)context;
                return compare_function_at_creation(i, a);
            },
            [](IrModule* m, IrDebugType* new_type, void* context)
            {
                let a = (IrDebugTypeFunctionCreationArguments*)context;
                fill_function_type(m, new_type, a);
            });
}

BUSTER_GLOBAL_LOCAL IrFunctionAttributes resolve_function_attributes(IrFunctionAttributes attributes, Target* target)
{
    attributes.calling_convention = resolve_calling_convention(attributes.calling_convention, target);
    return attributes;
}

BUSTER_GLOBAL_LOCAL Target* resolve_target(IrModule* module, Target* target)
{
    return target ? target : module->default_target;
}

BUSTER_GLOBAL_LOCAL bool compare_function_at_creation(IrType* function_type, IrTypeFunctionCreationArguments* arguments)
{
    BUSTER_CHECK(function_type->id == IrTypeId::function);
    BUSTER_UNUSED(arguments);
    BUSTER_TRAP();
}

BUSTER_GLOBAL_LOCAL void fill_function_type(IrModule* module, IrType* function_type, IrTypeFunctionCreationArguments* arguments)
{
    BUSTER_UNUSED(module);
    *function_type = IrType{
        .function = {
            .semantic = {
                .argument_types = arguments->argument_types.pointer,
                .return_type = arguments->return_type,
                .debug_type_ref = arguments->debug_type,
                .argument_count = (u16)arguments->argument_types.length,
            },
            .attributes = arguments->attributes,
            .target = arguments->target,
        },
        .name = {},
        .kind_next = {},
        .id = IrTypeId::function,
    };
}

BUSTER_F_IMPL IrTypeRef ir_type_get_function(IrModule* module, IrTypeFunctionCreationArguments arguments)
{
    arguments.target = resolve_target(module, arguments.target);
    arguments.attributes = resolve_function_attributes(arguments.attributes, arguments.target);

    return ir_type_get_or_create(module, &module->types.ir.first_function_type, &arguments,
            [](IrType* i, void* context) -> bool
            {
                let a = (IrTypeFunctionCreationArguments*)context;
                return compare_function_at_creation(i, a);
            },
            [](IrModule* m, IrType* new_type, void* context)
            {
                let a = (IrTypeFunctionCreationArguments*)context;
                fill_function_type(m, new_type, a);
            });
}

BUSTER_GLOBAL_LOCAL IrFunctionRef ir_function_get_ref(IrModule* module, IrFunction* function)
{
    return ref<IrFunctionRef>(arena_get_index(module->function_arena, function, sizeof(*function)));
}

BUSTER_F_IMPL IrFunction* ir_function_get(IrModule* module, IrFunctionRef function_ref)
{
    IrFunction* result = 0;

    if (function_ref.is_valid())
    {
        result = arena_get_pointer_at_index(module->function_arena, IrFunction, function_ref.get());
    }

    return result;
}

BUSTER_F_IMPL IrFunctionRef ir_function_create(IrModule* module, IrFunctionCreate create)
{
    let function = arena_allocate(module->function_arena, IrFunction, 1);
    *function = IrFunction {
        .argument_names = create.argument_names,
        .declaration = {
            .symbol = {
                .name = create.name,
                .type = create.ir_type,
                .debug_type = create.debug_type,
                .id = IrGlobalSymbolId::Function,
                .linkage = create.linkage,
            },
        },
    };
    return ir_function_get_ref(module, function);
}

// BUSTER_GLOBAL_LOCAL void ir_function_append_instruction(IrFunction* function, IrInstruction* instruction)
// {
//     ir_block_append_instruction(function->current_basic_block, instruction);
// }

// BUSTER_GLOBAL_LOCAL IrInstruction* ir_create_return(IrModule* module, IrFunction* function, IrValue* value)
// {
//     let result = arena_allocate(module->untyped_arena, IrInstruction, 1);
//     *result = (IrInstruction) {
//         .value = value,
//         .id = IrInstructionId::Return,
//     };
//     ir_function_append_instruction(function, result);
//     return result;
// }

// TODO: constant interning
BUSTER_F_IMPL IrValueRef ir_get_constant_integer(IrModule* module, IrTypeRef type_ref, u64 value)
{
    let type = ir_type_get(module, type_ref);
    BUSTER_CHECK(type->id <= IrTypeId::i64);
    let bit_count = (u64)type->id + 1;
    let pow2_bit_count = BUSTER_MAX(is_power_of_two(bit_count) ? bit_count : next_power_of_two(bit_count), 8);
    let pow2_byte_count = pow2_bit_count / 8;
    let index2 = BUSTER_TRAILING_ZEROES(pow2_byte_count);

    let id = (IrConstantDataId)index2;
    let index = (u64)id;
    let size = (u64)1 << (u64)id;
    let alignment = size;
    let arena = module->constant_arenas[index];
    let bytes = (u8*)arena_allocate_bytes(arena, size, alignment);
    BUSTER_CHECK(pow2_byte_count != 0);
    BUSTER_CHECK(pow2_byte_count > 0);
    BUSTER_CHECK(pow2_byte_count <= 8);
    BUSTER_CHECK(is_power_of_two(pow2_byte_count));

    for (u64 i = 0; i < pow2_byte_count; i += 1)
    {
        bytes[i] = (u8)(value >> (i * 8));
    }

    let arena_index = arena_get_index(arena, bytes, pow2_byte_count);

    let result = arena_allocate(module->value_arena, IrValue, 1);
    *result = (IrValue) {
        .type = type_ref,
        .id = IrValueId::ConstantInteger,
    };

    switch (index)
    {
        break; case 1: result->constant1 = ref<IrConstant1Ref>(arena_index);
        break; case 2: result->constant2 = ref<IrConstant2Ref>(arena_index);
        break; case 4: result->constant4 = ref<IrConstant4Ref>(arena_index);
        break; case 8: result->constant8 = ref<IrConstant8Ref>(arena_index);
        break; default: BUSTER_UNREACHABLE();
    }

    return ir_value_get_ref(module, result);
}

BUSTER_F_IMPL IrValueRef ir_get_big_constant_integer()
{
    return {};
}

// BUSTER_GLOBAL_LOCAL void append_statement(IrModule* module, IrBlockRef block_ref, IrStatementRef next_statement_ref)
// {
//     let block = ir_block_get(module, block_ref);
//     let next_statement = ir_statement_get(module, next_statement_ref);
//     let previous_statement_ref = block->last_statement;
//     next_statement->previous = previous_statement_ref;
//     next_statement->next = {};
//     next_statement->block = block_ref;
//
//     if (previous_statement_ref.is_valid())
//     {
//         let previous_statement = ir_statement_get(module, previous_statement_ref);
//         BUSTER_CHECK(!previous_statement->next.is_valid());
//         previous_statement->next = next_statement_ref;
//     }
//
//     if (!block->first_statement.is_valid())
//     {
//         block->first_statement = next_statement_ref;
//     }
//
//     if (!block->last_statement.is_valid())
//     {
//         block->last_statement = next_statement_ref;
//     }
//
//     block->statement_count += 1;
// }

// BUSTER_F_IMPL IrStatementRef ir_create_statement(IrModule* module, IrStatement statement, IrBlockRef block_ref)
// {
//     let new_statement = arena_allocate(module->statement_arena, IrStatement, 1);
//     *new_statement = statement;
//     let result = ir_statement_get_ref(module, new_statement);
//     append_statement(module, block_ref, result);
//     return result;
// }

BUSTER_F_IMPL IrBlockRef ir_create_block(IrModule* module)
{
    let new_block = arena_allocate(module->block_arena, IrBlock, 1);
    *new_block = {};
    let result = ir_block_get_ref(module, new_block);
    return result;
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_function_create(IrModule* module, IrGlobalSymbol symbol)
{
    IrFunction* function = 0;
    symbol.id = IrGlobalSymbolId::Function;

    function = arena_allocate(module->function_arena, IrFunction, 1);
    // let entry_basic_block = arena_allocate(module->untyped_arena, IrBasicBlock, 1);
    // *function = (IrFunction) {
    //     .symbol = symbol,
    //         .entry_block = entry_basic_block,
    //         .current_basic_block = entry_basic_block,
    // };

    return function;
}

BUSTER_GLOBAL_LOCAL IrGlobalVariable* ir_global_variable_create(IrModule* module, IrGlobalSymbol symbol)
{
    IrGlobalVariable* result = 0;
    symbol.id = IrGlobalSymbolId::Variable;

    if (symbol.type.is_valid())
    {
        result = arena_allocate(module->global_variable_arena, IrGlobalVariable, 1);
        *result = (IrGlobalVariable) {
            .symbol = symbol,
        };
    }

    return result;
}

BUSTER_F_IMPL Slice<IrFunction> ir_module_get_functions(IrModule* module)
{
    return arena_get_slice_at_position(module->function_arena, IrFunction, arena_minimum_position, module->function_arena->position);
}

BUSTER_GLOBAL_LOCAL IrTypeRef ir_get_integer_type(IrModule* module, u32 bit_count)
{
    IrTypeRef result{};

    if (bit_count > 0 && bit_count <= (sizeof(u64) * 8))
    {
        result = module->types.builtin.ir[bit_count - 1];
    }
    else if (bit_count == 128)
    {
        result = module->types.builtin.ir[(u64)IrTypeId::i128];
    }

    return result;
}

BUSTER_F_IMPL IrModule* ir_create_mock_module(Arena* arena)
{
    let module = ir_module_create(arena, 0, S8("basic"));
    // let i32_type = ir_get_integer_type(module, 32);
    // let function = ir_function_create(module, (IrGlobalSymbol) {
    //     .linkage = IrLinkage::External,
    //     .type = ir_function_type_get(module, {})
    //     });
    // ir_create_return(module, function, ir_constant_integer(module, i32_type, 0));

    return module;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_IMPL bool ir_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return true;
}
#endif
