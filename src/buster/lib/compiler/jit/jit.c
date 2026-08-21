// The generic object JIT: loads an already-produced host-native ObjectFile
// into executable memory in-process. It maps sections by protection class,
// resolves relocations against the object's symbols plus caller-supplied
// explicit bindings (never dlsym-style implicit lookup), inserts
// out-of-range call thunks where a target does not fit the relocation's
// reach, and finalizes W^X — pages are writable during patching or
// executable afterwards, never both. It is deliberately not a second
// compiler: no frontend or IR types appear here, only the object model.

#include <buster/lib/compiler/jit/jit.h>

#include <buster/lib/arena.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

typedef enum JitProtectionClass
{
    JIT_PROTECTION_CLASS_NONE,
    JIT_PROTECTION_CLASS_CODE,
    JIT_PROTECTION_CLASS_READ_ONLY,
    JIT_PROTECTION_CLASS_DATA,
    JIT_PROTECTION_CLASS_COUNT,
} JitProtectionClass;

BUSTER_GLOBAL_LOCAL JitProtectionClass jit_section_protection_class(ObjectSectionKind kind)
{
    JitProtectionClass result;
    switch (kind)
    {
        case OBJECT_SECTION_TEXT:
            result = JIT_PROTECTION_CLASS_CODE;
            break;
        case OBJECT_SECTION_READ_ONLY_DATA:
            result = JIT_PROTECTION_CLASS_READ_ONLY;
            break;
        case OBJECT_SECTION_DATA:
        case OBJECT_SECTION_ZERO:
            result = JIT_PROTECTION_CLASS_DATA;
            break;
        default:
            result = JIT_PROTECTION_CLASS_NONE;
            break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool jit_section_is_tls(ObjectSectionKind kind)
{
    return kind == OBJECT_SECTION_THREAD_LOCAL_DATA || kind == OBJECT_SECTION_THREAD_LOCAL_ZERO;
}

BUSTER_GLOBAL_LOCAL u64 jit_section_size(ObjectSection const* section)
{
    return BUSTER_MAX(section->data.length, section->virtual_size);
}

BUSTER_GLOBAL_LOCAL bool jit_align_forward(u64 value, u64 alignment, u64* result)
{
    bool valid = alignment && !(alignment & (alignment - 1)) && value <= UINT64_MAX - (alignment - 1);
    if (valid)
    {
        *result = (value + alignment - 1) & ~(alignment - 1);
    }

    return valid;
}

BUSTER_GLOBAL_LOCAL bool jit_add_u64(u64 left, u64 right, u64* result)
{
    bool valid = right <= UINT64_MAX - left;
    if (valid)
    {
        *result = left + right;
    }

    return valid;
}

BUSTER_GLOBAL_LOCAL bool jit_add_s64(s64 left, s64 right, s64* result)
{
    bool valid = (right <= 0 || left <= INT64_MAX - right) && (right >= 0 || left >= INT64_MIN - right);
    if (valid)
    {
        *result = left + right;
    }

    return valid;
}

BUSTER_GLOBAL_LOCAL bool jit_address_difference(u64 target, u64 place, s64 addend, s64* result)
{
    s64 difference = 0;
    if (target >= place)
    {
        u64 magnitude = target - place;
        if (magnitude > (u64)INT64_MAX)
        {
            return false;
        }
        difference = (s64)magnitude;
    }
    else
    {
        u64 magnitude = place - target;
        if (magnitude > (u64)INT64_MAX + 1)
        {
            return false;
        }
        difference = magnitude == (u64)INT64_MAX + 1 ? INT64_MIN : -(s64)magnitude;
    }
    return jit_add_s64(difference, addend, result);
}

BUSTER_GLOBAL_LOCAL bool jit_address_addend(u64 address, s64 addend, u64* result)
{
    if (addend < 0)
    {
        u64 magnitude = (u64)(-(addend + 1)) + 1;
        if (magnitude > address)
        {
            return false;
        }
        *result = address - magnitude;
    }
    else
    {
        u64 magnitude = (u64)addend;
        if (magnitude > UINT64_MAX - address)
        {
            return false;
        }
        *result = address + magnitude;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool jit_aarch64_page21_instruction_valid(u32 instruction)
{
    return (instruction & UINT32_C(0xffffffe0)) == UINT32_C(0x90000000);
}

BUSTER_GLOBAL_LOCAL bool jit_aarch64_pageoff12_shift(u32 instruction, u32* shift)
{
    if (shift)
    {
        u32 base = instruction & UINT32_C(0xffc00000);
        bool valid_ldst = false;
        switch (base)
        {
        case UINT32_C(0x39000000):
        case UINT32_C(0x39400000):
        case UINT32_C(0x39800000):
        case UINT32_C(0x39c00000):
        case UINT32_C(0x79000000):
        case UINT32_C(0x79400000):
        case UINT32_C(0x79800000):
        case UINT32_C(0x79c00000):
        case UINT32_C(0xb9000000):
        case UINT32_C(0xb9400000):
        case UINT32_C(0xb9800000):
        case UINT32_C(0xf9000000):
        case UINT32_C(0xf9400000):
        case UINT32_C(0xf9800000):
        case UINT32_C(0x3d000000):
        case UINT32_C(0x3d400000):
        case UINT32_C(0x3d800000):
        case UINT32_C(0x3dc00000):
        case UINT32_C(0x7d000000):
        case UINT32_C(0x7d400000):
        case UINT32_C(0xbd000000):
        case UINT32_C(0xbd400000):
        case UINT32_C(0xfd000000):
        case UINT32_C(0xfd400000):
            valid_ldst = true;
            break;
        default:
            break;
        }
        if (valid_ldst)
        {
            u32 implicit_shift = instruction >> 30;
            if (!implicit_shift && (instruction & UINT32_C(0x04800000)) == UINT32_C(0x04800000))
            {
                implicit_shift = 4;
            }
            if (instruction & (UINT32_C(0xfff) << 10))
            {
                return false;
            }
            *shift = implicit_shift;
            return true;
        }
        if ((instruction & UINT32_C(0xffc00000)) == UINT32_C(0x91000000) &&
            !(instruction & (UINT32_C(0xfff) << 10)))
        {
            *shift = 0;
            return true;
        }
    }

    return false;
}

bool jit_apply_aarch64_mach_page_relocation(ObjectRelocationKind kind, u8* patch, u64 place, u64 target, s64 addend)
{
    if (patch && !(place & 3))
    {
        u64 address = 0;
        if (!jit_address_addend(target, addend, &address))
        {
            return false;
        }
        u32 instruction = 0;
        memcpy(&instruction, patch, sizeof(instruction));
        if (kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21)
        {
            if (!jit_aarch64_page21_instruction_valid(instruction))
            {
                return false;
            }
            u32 patched = 0;
            if (!a64_adrp_encode(instruction & 31, place, address, &patched))
            {
                return false;
            }
            memcpy(patch, &patched, sizeof(patched));
            return true;
        }
        if (kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12)
        {
            u32 shift = 0;
            if (!jit_aarch64_pageoff12_shift(instruction, &shift) || ((address & 0xfff) & ((1u << shift) - 1)))
            {
                return false;
            }
            instruction &= ~(UINT32_C(0xfff) << 10);
            instruction |= (u32)((address & 0xfff) >> shift) << 10;
            memcpy(patch, &instruction, sizeof(instruction));
            return true;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool jit_relocation_is_tls(ObjectRelocationKind kind)
{
    bool result;
    switch (kind)
    {
        case OBJECT_RELOCATION_X86_64_TPOFF32:
        case OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32:
        case OBJECT_RELOCATION_PE_TLS_OFFSET32:
        case OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP:
        case OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12:
        case OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12:
        case OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12:
        case OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12:
        case OBJECT_RELOCATION_X86_64_MACH_TLV_PC32:
        case OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21:
        case OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12:
            result = true;
            break;
        default:
            result = false;
            break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool jit_relocation_uses_function_thunk(ObjectRelocationKind kind)
{
    return kind == OBJECT_RELOCATION_X86_64_PC32 || kind == OBJECT_RELOCATION_AARCH64_CALL26 || kind == OBJECT_RELOCATION_AARCH64_JUMP26;
}

BUSTER_GLOBAL_LOCAL bool jit_relocation_is_supported(ObjectRelocationKind kind, CpuArch arch)
{
    return kind == OBJECT_RELOCATION_ABSOLUTE64 ||
           (kind == OBJECT_RELOCATION_X86_64_PC32 && arch == CPU_ARCH_X86_64) ||
           ((kind == OBJECT_RELOCATION_AARCH64_CALL26 || kind == OBJECT_RELOCATION_AARCH64_JUMP26 ||
             kind == OBJECT_RELOCATION_AARCH64_PREL32 || kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 ||
             kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12) &&
            arch == CPU_ARCH_AARCH64);
}

BUSTER_GLOBAL_LOCAL bool jit_external_data_relocation_is_supported(ObjectRelocationKind kind, CpuArch arch)
{
    return (arch == CPU_ARCH_X86_64 && kind == OBJECT_RELOCATION_ABSOLUTE64) ||
           (arch == CPU_ARCH_AARCH64 &&
            (kind == OBJECT_RELOCATION_AARCH64_PREL32 || kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 ||
             kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12));
}

BUSTER_GLOBAL_LOCAL u64 jit_relocation_size(ObjectRelocationKind kind)
{
    return kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
}

BUSTER_GLOBAL_LOCAL JitHostBinding const* jit_binding_find(JitOptions options, ObjectSymbol const* symbol, JitError* error)
{
    bool name_found = false;
    for (u32 index = 0; index < options.binding_count; index += 1)
    {
        JitHostBinding const* binding = options.bindings + index;
        if (string_equal(binding->name, symbol->name))
        {
            name_found = true;
            if (binding->kind == symbol->kind)
            {
                if (!binding->address)
                {
                    *error = JIT_ERROR_INVALID_BINDING;
                    return 0;
                }
                *error = JIT_ERROR_NONE;
                return binding;
            }
        }
    }
    *error = name_found ? JIT_ERROR_BINDING_KIND : JIT_ERROR_UNRESOLVED_IMPORT;
    return 0;
}

BUSTER_GLOBAL_LOCAL u64 jit_thunk_size(CpuArch arch)
{
    return arch == CPU_ARCH_X86_64 ? 14 : 16;
}

BUSTER_GLOBAL_LOCAL bool jit_symbol_address(JitProgram const* program, ObjectSymbol const* symbol, u64* address)
{
    if (symbol->section == OBJECT_SECTION_UNDEFINED || symbol->section >= program->object->section_count ||
        !program->section_addresses[symbol->section])
    {
        return false;
    }
    u64 section_size = program->section_sizes[symbol->section];
    if (symbol->value > section_size || symbol->size > section_size - symbol->value)
    {
        return false;
    }
    u64 base = (u64)(uintptr_t)program->section_addresses[symbol->section];
    if (symbol->value > UINT64_MAX - base)
    {
        return false;
    }
    *address = base + symbol->value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool jit_emit_thunks(JitProgram* program, JitOptions options, void* code_base, u64 thunk_offset,
                                         u32 const* thunk_indices)
{
    ObjectFile const* object = program->object;
    u64 thunk_size = jit_thunk_size(object->target.cpu_arch);
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        ObjectSymbol const* symbol = object->symbols + symbol_index;
        if (thunk_indices[symbol_index] == UINT32_MAX)
        {
            continue;
        }
        JitError binding_error = JIT_ERROR_NONE;
        JitHostBinding const* binding = jit_binding_find(options, symbol, &binding_error);
        if (!binding)
        {
            program->error = binding_error;
            program->failing_symbol = symbol->name;
            return false;
        }
        u64 thunk_index = thunk_indices[symbol_index];
        u8* thunk = (u8*)code_base + thunk_offset + thunk_index * thunk_size;
        u64 target = (u64)(uintptr_t)binding->address;
        if (object->target.cpu_arch == CPU_ARCH_X86_64)
        {
            BusterX86MetadataPhysicalOperand memory = {
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                .width = 64,
                .memory = {
                    .address_size = 64,
                    .has_displacement = true,
                    .rip_relative = true,
                    .source_width = 64,
                },
            };
            BusterX86MetadataEmitResult encoded = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                .physical = {
                    .mnemonic = S8("JMP"),
                    .operands = &memory,
                    .operand_count = 1,
                    .features = {.names = 0, .count = 0},
                    .address_size = 64,
                    .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
                    .source_semantics = false,
                },
                .output = thunk,
                .output_capacity = 6,
            });
            if (encoded.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || encoded.byte_count != 6)
            {
                program->error = JIT_ERROR_INVALID_INPUT;
                program->failing_symbol = symbol->name;
                return false;
            }
            memcpy(thunk + encoded.byte_count, &target, sizeof(target));
        }
        else
        {
            u32 load_literal_x16 = 0x58000050;
            u32 branch_x16 = 0xd61f0200;
            memcpy(thunk, &load_literal_x16, sizeof(load_literal_x16));
            memcpy(thunk + 4, &branch_x16, sizeof(branch_x16));
            memcpy(thunk + 8, &target, sizeof(target));
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool jit_apply_relocations(JitProgram* program, JitOptions options, void* code_base, u64 thunk_offset,
                                               u32 const* thunk_indices)
{
    ObjectFile const* object = program->object;
    u64 thunk_size = jit_thunk_size(object->target.cpu_arch);
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation const* relocation = object->relocations + index;
        if (relocation->section >= object->section_count)
        {
            program->error = JIT_ERROR_INVALID_INPUT;
            return false;
        }
        ObjectSectionKind source_kind = object->sections[relocation->section].kind;
        if (jit_section_protection_class(source_kind) == JIT_PROTECTION_CLASS_NONE)
        {
            continue;
        }
        if (relocation->symbol >= object->symbol_count || (u32)relocation->kind >= (u32)OBJECT_RELOCATION_COUNT)
        {
            program->error = JIT_ERROR_INVALID_INPUT;
            return false;
        }
        ObjectSymbol const* symbol = object->symbols + relocation->symbol;
        if (jit_relocation_is_tls(relocation->kind))
        {
            program->error = JIT_ERROR_TLS_UNSUPPORTED;
            program->failing_symbol = symbol->name;
            return false;
        }
        if (!jit_relocation_is_supported(relocation->kind, object->target.cpu_arch))
        {
            program->error = JIT_ERROR_UNSUPPORTED_RELOCATION;
            program->failing_symbol = symbol->name;
            return false;
        }
        u64 patch_size = jit_relocation_size(relocation->kind);
        ObjectSection const* source = object->sections + relocation->section;
        if (relocation->offset > source->data.length || patch_size > source->data.length - relocation->offset)
        {
            program->error = JIT_ERROR_INVALID_INPUT;
            program->failing_symbol = symbol->name;
            return false;
        }
        u64 target = 0;
        if (symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            if (symbol->kind == OBJECT_SYMBOL_DATA && !options.binding_count && object->target.cpu_arch == CPU_ARCH_AARCH64 &&
                jit_external_data_relocation_is_supported(relocation->kind, object->target.cpu_arch))
            {
                program->error = JIT_ERROR_EXTERNAL_DATA;
                program->failing_symbol = symbol->name;
                return false;
            }
            if ((relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26 || relocation->kind == OBJECT_RELOCATION_AARCH64_JUMP26) &&
                relocation->addend)
            {
                program->error = JIT_ERROR_UNSUPPORTED_RELOCATION;
                program->failing_symbol = symbol->name;
                return false;
            }
            if (symbol->kind == OBJECT_SYMBOL_DATA && !jit_external_data_relocation_is_supported(relocation->kind, object->target.cpu_arch))
            {
                program->error = JIT_ERROR_EXTERNAL_DATA;
                program->failing_symbol = symbol->name;
                return false;
            }
            JitError binding_error = JIT_ERROR_NONE;
            JitHostBinding const* binding = jit_binding_find(options, symbol, &binding_error);
            if (!binding)
            {
                program->error = binding_error;
                program->failing_symbol = symbol->name;
                return false;
            }
            target = (u64)(uintptr_t)binding->address;
            if (jit_relocation_uses_function_thunk(relocation->kind) && symbol->kind == OBJECT_SYMBOL_FUNCTION)
            {
                u64 thunk_index = thunk_indices[relocation->symbol];
                if (thunk_index == UINT32_MAX)
                {
                    program->error = JIT_ERROR_INVALID_INPUT;
                    program->failing_symbol = symbol->name;
                    return false;
                }
                target = (u64)(uintptr_t)code_base + thunk_offset + thunk_index * thunk_size;
            }
        }
        else
        {
            if (symbol->section >= object->section_count)
            {
                program->error = JIT_ERROR_INVALID_INPUT;
                program->failing_symbol = symbol->name;
                return false;
            }
            if (jit_section_is_tls(object->sections[symbol->section].kind))
            {
                program->error = JIT_ERROR_TLS_UNSUPPORTED;
                program->failing_symbol = symbol->name;
                return false;
            }
            if (jit_section_protection_class(object->sections[symbol->section].kind) == JIT_PROTECTION_CLASS_NONE)
            {
                program->error = JIT_ERROR_UNSUPPORTED_RELOCATION;
                program->failing_symbol = symbol->name;
                return false;
            }
            if (!jit_symbol_address(program, symbol, &target))
            {
                program->error = JIT_ERROR_SYMBOL_BOUNDS;
                program->failing_symbol = symbol->name;
                return false;
            }
        }
        u8* patch = (u8*)program->section_addresses[relocation->section] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 displacement = 0;
            if (!jit_address_difference(target, (u64)(uintptr_t)patch, relocation->addend, &displacement) || displacement < INT32_MIN ||
                displacement > INT32_MAX)
            {
                program->error = JIT_ERROR_CAPACITY;
                program->failing_symbol = symbol->name;
                return false;
            }
            s32 value = (s32)displacement;
            memcpy(patch, &value, sizeof(value));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26 || relocation->kind == OBJECT_RELOCATION_AARCH64_JUMP26)
        {
            if (symbol->kind != OBJECT_SYMBOL_FUNCTION)
            {
                program->error = JIT_ERROR_UNSUPPORTED_RELOCATION;
                program->failing_symbol = symbol->name;
                return false;
            }
            s64 displacement = 0;
            u32 instruction = 0;
            u32 patched = 0;
            memcpy(&instruction, patch, sizeof(instruction));
            A64Opcode opcode = relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26 ? A64_OPCODE_BL : A64_OPCODE_B;
            if (((u64)(uintptr_t)patch & 3) ||
                !a64_pc_relative_displacement(target, (u64)(uintptr_t)patch, relocation->addend, &displacement) ||
                !a64_pc_relative_patch(opcode, instruction, displacement, &patched))
            {
                program->error = JIT_ERROR_CAPACITY;
                program->failing_symbol = symbol->name;
                return false;
            }
            memcpy(patch, &patched, sizeof(patched));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_PREL32)
        {
            s64 displacement = 0;
            if (((u64)(uintptr_t)patch & 3) ||
                !jit_address_difference(target, (u64)(uintptr_t)patch, relocation->addend, &displacement) || displacement < INT32_MIN ||
                displacement > INT32_MAX)
            {
                program->error = JIT_ERROR_CAPACITY;
                program->failing_symbol = symbol->name;
                return false;
            }
            s32 value = (s32)displacement;
            memcpy(patch, &value, sizeof(value));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21)
        {
            if (!jit_apply_aarch64_mach_page_relocation(relocation->kind, patch, (u64)(uintptr_t)patch, target, relocation->addend))
            {
                program->error = JIT_ERROR_CAPACITY;
                program->failing_symbol = symbol->name;
                return false;
            }
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12)
        {
            if (!jit_apply_aarch64_mach_page_relocation(relocation->kind, patch, (u64)(uintptr_t)patch, target, relocation->addend))
            {
                program->error = JIT_ERROR_CAPACITY;
                program->failing_symbol = symbol->name;
                return false;
            }
        }
        else
        {
            u64 value = 0;
            if (!jit_address_addend(target, relocation->addend, &value))
            {
                program->error = JIT_ERROR_CAPACITY;
                program->failing_symbol = symbol->name;
                return false;
            }
            memcpy(patch, &value, sizeof(value));
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void jit_program_unmap_allocations(JitProgram* program)
{
    if (program->auxiliary_allocation_base && program->auxiliary_allocation_size)
    {
        os_unreserve(program->auxiliary_allocation_base, program->auxiliary_allocation_size);
    }
    if (program->allocation_base && program->allocation_size)
    {
        os_unreserve(program->allocation_base, program->allocation_size);
    }
    program->allocation_base = 0;
    program->allocation_size = 0;
    program->auxiliary_allocation_base = 0;
    program->auxiliary_allocation_size = 0;
}

JitProgram jit_link_object(ObjectFile const* object, JitOptions options)
{
    JitProgram result = {.object = object};
    if (!object || object->error != OBJECT_ERROR_NONE || !object->sections || !object->section_count ||
        object->section_count > OBJECT_SECTION_COUNT || (object->symbol_count && !object->symbols) ||
        (object->relocation_count && !object->relocations) || (options.binding_count && !options.bindings))
    {
        result.error = JIT_ERROR_INVALID_INPUT;
        return result;
    }
    if (object->target.cpu_arch != target_native.cpu_arch || object->target.os != target_native.os ||
        !target_cpu_features_are_valid(object->target) || !target_cpu_features_are_valid(target_native) ||
        !target_cpu_features_subset(target_cpu_features_effective(object->target), target_cpu_features_effective(target_native)))
    {
        result.error = JIT_ERROR_FOREIGN_TARGET;
        return result;
    }
    if (object->target.cpu_arch == CPU_ARCH_X86_64)
    {
        // Metadata's demand-filled decode and exact-form caches are immutable
        // only after this serial prewarm.  jit_link_object may be called from
        // a worker, so keep the first fill here explicit and fail loudly via
        // the metadata serial-initialization guard if its caller violated the
        // target prewarm contract.
        buster_x86_metadata_prewarm();
    }
    for (u32 index = 0; index < options.binding_count; index += 1)
    {
        JitHostBinding const* binding = options.bindings + index;
        if (!binding->name.pointer || !binding->name.length || !binding->address || (u32)binding->kind >= (u32)OBJECT_SYMBOL_COUNT)
        {
            result.error = JIT_ERROR_INVALID_BINDING;
            result.failing_symbol = binding->name;
            return result;
        }
    }
    u64 page_size = os_get_page_size();
    if (!page_size || (page_size & (page_size - 1)))
    {
        result.error = JIT_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    for (u32 index = 0; index < object->section_count; index += 1)
    {
        ObjectSection const* section = object->sections + index;
        if ((u32)section->kind >= (u32)OBJECT_SECTION_COUNT || (section->data.length && !section->data.pointer) ||
            (section->alignment && ((section->alignment & (section->alignment - 1)) || section->alignment > page_size)) ||
            (object_section_kind_is_zero_fill(section->kind) && section->data.length))
        {
            result.error = JIT_ERROR_INVALID_INPUT;
            return result;
        }
        if (jit_section_is_tls(section->kind) && jit_section_size(section))
        {
            result.error = JIT_ERROR_TLS_UNSUPPORTED;
            return result;
        }
    }
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol const* symbol = object->symbols + index;
        if (!symbol->name.pointer || !symbol->name.length || (u32)symbol->kind >= (u32)OBJECT_SYMBOL_COUNT ||
            (symbol->section != OBJECT_SECTION_UNDEFINED && symbol->section >= object->section_count))
        {
            result.error = JIT_ERROR_INVALID_INPUT;
            result.failing_symbol = symbol->name;
            return result;
        }
    }
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation const* relocation = object->relocations + index;
        if (relocation->section >= object->section_count)
        {
            result.error = JIT_ERROR_INVALID_INPUT;
            return result;
        }
        if (jit_section_protection_class(object->sections[relocation->section].kind) == JIT_PROTECTION_CLASS_NONE)
        {
            continue;
        }
        if (relocation->symbol >= object->symbol_count || (u32)relocation->kind >= (u32)OBJECT_RELOCATION_COUNT)
        {
            result.error = JIT_ERROR_INVALID_INPUT;
            return result;
        }
        ObjectSymbol const* symbol = object->symbols + relocation->symbol;
        if (jit_relocation_is_tls(relocation->kind))
        {
            result.error = JIT_ERROR_TLS_UNSUPPORTED;
            result.failing_symbol = symbol->name;
            return result;
        }
        if (!jit_relocation_is_supported(relocation->kind, object->target.cpu_arch))
        {
            result.error = JIT_ERROR_UNSUPPORTED_RELOCATION;
            result.failing_symbol = symbol->name;
            return result;
        }
        if (relocation->kind == OBJECT_RELOCATION_AARCH64_PREL32 &&
            ((relocation->offset & 3) || object->sections[relocation->section].alignment < 4))
        {
            result.error = JIT_ERROR_INVALID_INPUT;
            result.failing_symbol = symbol->name;
            return result;
        }
        if (symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            if (symbol->kind == OBJECT_SYMBOL_DATA && !options.binding_count && object->target.cpu_arch == CPU_ARCH_AARCH64 &&
                jit_external_data_relocation_is_supported(relocation->kind, object->target.cpu_arch))
            {
                result.error = JIT_ERROR_EXTERNAL_DATA;
                result.failing_symbol = symbol->name;
                return result;
            }
            if (symbol->kind == OBJECT_SYMBOL_DATA && !jit_external_data_relocation_is_supported(relocation->kind, object->target.cpu_arch))
            {
                result.error = JIT_ERROR_EXTERNAL_DATA;
                result.failing_symbol = symbol->name;
                return result;
            }
            JitError binding_error = JIT_ERROR_NONE;
            if (!jit_binding_find(options, symbol, &binding_error))
            {
                result.error = binding_error;
                result.failing_symbol = symbol->name;
                return result;
            }
        }
    }

    TemporalArena scratch = scratch_begin(0, 0);
    u64 scratch_remaining = scratch.arena->reserved_size - scratch.arena->position;
    if ((u64)object->symbol_count > scratch_remaining / sizeof(u32))
    {
        result.error = JIT_ERROR_CAPACITY;
        scratch_end(scratch);
        return result;
    }
    u32* thunk_indices = arena_allocate(scratch.arena, u32, object->symbol_count);
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        thunk_indices[symbol_index] = UINT32_MAX;
    }
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation const* relocation = object->relocations + relocation_index;
        if (relocation->section >= object->section_count || relocation->symbol >= object->symbol_count ||
            jit_section_protection_class(object->sections[relocation->section].kind) == JIT_PROTECTION_CLASS_NONE)
        {
            continue;
        }
        ObjectSymbol const* symbol = object->symbols + relocation->symbol;
        bool call_import = symbol->section == OBJECT_SECTION_UNDEFINED && symbol->kind == OBJECT_SYMBOL_FUNCTION &&
                           jit_relocation_uses_function_thunk(relocation->kind);
        if (call_import)
        {
            thunk_indices[relocation->symbol] = 0;
        }
    }
    u64 thunk_count = 0;
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        if (thunk_indices[symbol_index] != UINT32_MAX)
        {
            if (thunk_count >= UINT32_MAX)
            {
                result.error = JIT_ERROR_CAPACITY;
                scratch_end(scratch);
                return result;
            }
            thunk_indices[symbol_index] = (u32)thunk_count;
            thunk_count += 1;
        }
    }

    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 class_sizes[JIT_PROTECTION_CLASS_COUNT] = {0};
    for (u32 class_index = JIT_PROTECTION_CLASS_CODE; class_index < JIT_PROTECTION_CLASS_COUNT; class_index += 1)
    {
        for (u32 section_index = 0; section_index < object->section_count; section_index += 1)
        {
            ObjectSection const* section = object->sections + section_index;
            if ((u32)jit_section_protection_class(section->kind) != class_index)
            {
                continue;
            }
            u64 alignment = section->alignment ? section->alignment : 1;
            u64 offset = 0;
            if (!jit_align_forward(class_sizes[class_index], alignment, &offset) ||
                !jit_add_u64(offset, jit_section_size(section), &class_sizes[class_index]))
            {
                result.error = JIT_ERROR_CAPACITY;
                scratch_end(scratch);
                return result;
            }
            section_offsets[section_index] = offset;
        }
    }
    u64 thunk_offset = class_sizes[JIT_PROTECTION_CLASS_CODE];
    if (thunk_count &&
        (!jit_align_forward(class_sizes[JIT_PROTECTION_CLASS_CODE], 8, &thunk_offset) ||
         thunk_count > UINT64_MAX / jit_thunk_size(object->target.cpu_arch) ||
         !jit_add_u64(thunk_offset, thunk_count * jit_thunk_size(object->target.cpu_arch), &class_sizes[JIT_PROTECTION_CLASS_CODE])))
    {
        result.error = JIT_ERROR_CAPACITY;
        scratch_end(scratch);
        return result;
    }
    result.executable_size = class_sizes[JIT_PROTECTION_CLASS_CODE];

    u64 class_offsets[JIT_PROTECTION_CLASS_COUNT] = {0};
    u64 class_page_sizes[JIT_PROTECTION_CLASS_COUNT] = {0};
    u64 allocation_size = 0;
    for (u32 class_index = JIT_PROTECTION_CLASS_CODE; class_index < JIT_PROTECTION_CLASS_COUNT; class_index += 1)
    {
        class_offsets[class_index] = allocation_size;
        if (!jit_align_forward(class_sizes[class_index], page_size, &class_page_sizes[class_index]) ||
            !jit_add_u64(allocation_size, class_page_sizes[class_index], &allocation_size))
        {
            result.error = JIT_ERROR_CAPACITY;
            scratch_end(scratch);
            return result;
        }
    }
    if (!allocation_size)
    {
        result.error = JIT_ERROR_INVALID_INPUT;
        scratch_end(scratch);
        return result;
    }

    void* image_base = 0;
#if BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64
    bool jit_code_is_writable = false;
    u64 code_allocation_size = class_page_sizes[JIT_PROTECTION_CLASS_CODE];
    u64 non_executable_allocation_size = allocation_size - code_allocation_size;
    if (code_allocation_size)
    {
        // MAP_FIXED cannot be combined with MAP_JIT. Let the kernel choose one
        // contiguous MAP_JIT image, then replace only its already-owned,
        // page-aligned non-code tail with an ordinary fixed mapping. Code keeps
        // the sole MAP_JIT prefix while every planned relocation distance is
        // unchanged and mutable data is outside JIT write protection.
        void* jit_allocation =
            os_reserve(0, allocation_size, (ProtectionFlags){.read = 1, .write = 1, .execute = 1},
                       (MapFlags){.priv = 1, .anonymous = 1, .jit = 1});
        if (!jit_allocation)
        {
            result.error = JIT_ERROR_EXECUTABLE_MEMORY;
            goto link_failed;
        }
        if (non_executable_allocation_size)
        {
            void* tail_address = (u8*)jit_allocation + code_allocation_size;
            void* tail_allocation =
                os_reserve(tail_address, non_executable_allocation_size, (ProtectionFlags){.read = 1, .write = 1},
                           (MapFlags){.priv = 1, .anonymous = 1, .fixed = 1});
            if (tail_allocation != tail_address)
            {
                if (tail_allocation)
                {
                    os_unreserve(tail_allocation, non_executable_allocation_size);
                }
                // A failed ordinary overlay leaves the original MAP_JIT span
                // intact, so it is still safe to release as one mapping.
                os_unreserve(jit_allocation, allocation_size);
                result.error = JIT_ERROR_EXECUTABLE_MEMORY;
                goto link_failed;
            }
            result.auxiliary_allocation_base = tail_allocation;
            result.auxiliary_allocation_size = non_executable_allocation_size;
        }
        image_base = jit_allocation;
        result.allocation_base = jit_allocation;
        result.allocation_size = code_allocation_size;
        os_jit_write_protect(false);
        jit_code_is_writable = true;
    }
    else
    {
        image_base = os_reserve(0, allocation_size, (ProtectionFlags){.read = 1, .write = 1},
                                (MapFlags){.priv = 1, .anonymous = 1});
        result.allocation_base = image_base;
        result.allocation_size = image_base ? allocation_size : 0;
    }
#else
    image_base = os_reserve(0, allocation_size, (ProtectionFlags){.read = 1, .write = 1},
                            (MapFlags){.priv = 1, .anonymous = 1, .jit = BUSTER_MACOS});
    result.allocation_base = image_base;
    result.allocation_size = image_base ? allocation_size : 0;
#endif
    if (!image_base)
    {
        result.error = JIT_ERROR_EXECUTABLE_MEMORY;
        goto link_failed;
    }
    void* class_bases[JIT_PROTECTION_CLASS_COUNT] = {0};
    for (u32 class_index = JIT_PROTECTION_CLASS_CODE; class_index < JIT_PROTECTION_CLASS_COUNT; class_index += 1)
    {
        class_bases[class_index] = (u8*)image_base + class_offsets[class_index];
    }
    for (u32 section_index = 0; section_index < object->section_count; section_index += 1)
    {
        ObjectSection const* section = object->sections + section_index;
        JitProtectionClass protection_class = jit_section_protection_class(section->kind);
        u64 size = jit_section_size(section);
        result.section_sizes[section_index] = size;
        if (protection_class == JIT_PROTECTION_CLASS_NONE || !size)
        {
            continue;
        }
        result.section_addresses[section_index] = (u8*)class_bases[protection_class] + section_offsets[section_index];
        if (section->data.length)
        {
            memcpy(result.section_addresses[section_index], section->data.pointer, section->data.length);
        }
    }
    if (!jit_emit_thunks(&result, options, class_bases[JIT_PROTECTION_CLASS_CODE], thunk_offset, thunk_indices) ||
        !jit_apply_relocations(&result, options, class_bases[JIT_PROTECTION_CLASS_CODE], thunk_offset, thunk_indices))
    {
        goto link_failed;
    }
    if ((class_page_sizes[JIT_PROTECTION_CLASS_READ_ONLY] &&
         !os_protect(class_bases[JIT_PROTECTION_CLASS_READ_ONLY], class_page_sizes[JIT_PROTECTION_CLASS_READ_ONLY],
                     (ProtectionFlags){.read = 1})) ||
        (class_page_sizes[JIT_PROTECTION_CLASS_DATA] &&
         !os_protect(class_bases[JIT_PROTECTION_CLASS_DATA], class_page_sizes[JIT_PROTECTION_CLASS_DATA],
                     (ProtectionFlags){.read = 1, .write = 1})) ||
#if !(BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64)
        (class_page_sizes[JIT_PROTECTION_CLASS_CODE] &&
         !os_protect(class_bases[JIT_PROTECTION_CLASS_CODE], class_page_sizes[JIT_PROTECTION_CLASS_CODE],
                     (ProtectionFlags){.read = 1, .execute = 1})))
#else
        false)
#endif
    {
        result.error = JIT_ERROR_PROTECTION;
        goto link_failed;
    }
#if BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64
    if (jit_code_is_writable)
    {
        os_jit_write_protect(true);
        jit_code_is_writable = false;
    }
#endif
    if (result.executable_size && !os_flush_instruction_cache(class_bases[JIT_PROTECTION_CLASS_CODE], result.executable_size))
    {
        result.error = JIT_ERROR_PROTECTION;
        goto link_failed;
    }
    scratch_end(scratch);
    return result;

link_failed:
#if BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64
    if (jit_code_is_writable)
    {
        os_jit_write_protect(true);
    }
#endif
    jit_program_unmap_allocations(&result);
    result.executable_size = 0;
    memset(result.section_addresses, 0, sizeof(result.section_addresses));
    memset(result.section_sizes, 0, sizeof(result.section_sizes));
    scratch_end(scratch);
    return result;
}

void* jit_program_symbol(JitProgram* program, String8 name)
{
    if (!program || !program->object || !program->allocation_base || !name.pointer || !name.length)
    {
        if (program)
        {
            program->error = JIT_ERROR_INVALID_INPUT;
            program->failing_symbol = name;
        }
        return 0;
    }
    program->error = JIT_ERROR_NONE;
    program->failing_symbol = (String8){0};
    for (u32 index = 0; index < program->object->symbol_count; index += 1)
    {
        ObjectSymbol const* symbol = program->object->symbols + index;
        if (symbol->section == OBJECT_SECTION_UNDEFINED || !string_equal(symbol->name, name))
        {
            continue;
        }
        u64 address = 0;
        if (symbol->section >= program->object->section_count ||
            jit_section_protection_class(program->object->sections[symbol->section].kind) == JIT_PROTECTION_CLASS_NONE ||
            !jit_symbol_address(program, symbol, &address))
        {
            program->error = JIT_ERROR_SYMBOL_BOUNDS;
            program->failing_symbol = symbol->name;
            return 0;
        }
        return (void*)(uintptr_t)address;
    }
    program->error = JIT_ERROR_SYMBOL_NOT_FOUND;
    program->failing_symbol = name;
    return 0;
}

void jit_program_release(JitProgram* program)
{
    if (!program)
    {
        return;
    }
    jit_program_unmap_allocations(program);
    *program = (JitProgram){0};
}

String8 jit_error_string(JitError error)
{
    String8 result;
    switch (error)
    {
        case JIT_ERROR_NONE:
            result = S8("no JIT error");
            break;
        case JIT_ERROR_INVALID_INPUT:
            result = S8("invalid JIT object");
            break;
        case JIT_ERROR_FOREIGN_TARGET:
            result = S8("JIT object target does not match the native target");
            break;
        case JIT_ERROR_CAPACITY:
            result = S8("JIT image or relocation is out of range");
            break;
        case JIT_ERROR_EXECUTABLE_MEMORY:
            result = S8("could not allocate JIT memory");
            break;
        case JIT_ERROR_PROTECTION:
            result = S8("could not finalize JIT memory protection");
            break;
        case JIT_ERROR_UNRESOLVED_IMPORT:
            result = S8("unresolved JIT import");
            break;
        case JIT_ERROR_TLS_UNSUPPORTED:
            result = S8("thread-local storage is not supported by the JIT");
            break;
        case JIT_ERROR_UNSUPPORTED_RELOCATION:
            result = S8("unsupported relocation in a loaded JIT section");
            break;
        case JIT_ERROR_EXTERNAL_DATA:
            result = S8("external data relocation is unsupported for this JIT target");
            break;
        case JIT_ERROR_SYMBOL_BOUNDS:
            result = S8("JIT symbol is outside its loaded section");
            break;
        case JIT_ERROR_SYMBOL_NOT_FOUND:
            result = S8("JIT symbol was not found");
            break;
        case JIT_ERROR_BINDING_KIND:
            result = S8("JIT host binding kind does not match the imported symbol");
            break;
        case JIT_ERROR_INVALID_BINDING:
            result = S8("invalid JIT host binding");
            break;
        default:
            result = S8("unknown JIT error");
            break;
    }

    return result;
}
