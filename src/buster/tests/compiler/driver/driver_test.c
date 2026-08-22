#include <buster/tests/compiler/driver/driver_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/tests/compiler/codegen/codegen_test.h>

BUSTER_GLOBAL_LOCAL bool compiler_driver_test_elf_section_find(ByteSlice image, String8 name, u64* offset, u64* size, u64* address)
{
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_SECTION_HEADER_SIZE = 64,
    };
    if (image.length >= ELF_HEADER_SIZE && memcmp(image.pointer, "\x7f" "ELF", 4) == 0)
    {
        u64 section_table;
        u16 section_count;
        u16 string_index;
        memcpy(&section_table, image.pointer + 40, sizeof(section_table));
        memcpy(&section_count, image.pointer + 60, sizeof(section_count));
        memcpy(&string_index, image.pointer + 62, sizeof(string_index));
        if (!section_count || string_index >= section_count || section_table > image.length ||
            (u64)section_count * ELF_SECTION_HEADER_SIZE > image.length - section_table)
        {
            return false;
        }
        u64 string_header = section_table + (u64)string_index * ELF_SECTION_HEADER_SIZE;
        u64 string_offset;
        u64 string_size;
        memcpy(&string_offset, image.pointer + string_header + 24, sizeof(string_offset));
        memcpy(&string_size, image.pointer + string_header + 32, sizeof(string_size));
        if (string_offset > image.length || string_size > image.length - string_offset)
        {
            return false;
        }
        for (u16 section_index = 0; section_index < section_count; section_index += 1)
        {
            u64 header = section_table + (u64)section_index * ELF_SECTION_HEADER_SIZE;
            u32 name_offset;
            memcpy(&name_offset, image.pointer + header, sizeof(name_offset));
            if (name_offset >= string_size || string_size - name_offset <= name.length)
            {
                continue;
            }
            if (memcmp(image.pointer + string_offset + name_offset, name.pointer, name.length) != 0 ||
                image.pointer[string_offset + name_offset + name.length] != 0)
            {
                continue;
            }
            memcpy(offset, image.pointer + header + 24, sizeof(*offset));
            memcpy(size, image.pointer + header + 32, sizeof(*size));
            memcpy(address, image.pointer + header + 16, sizeof(*address));
            return *offset <= image.length && *size <= image.length - *offset;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL ByteSlice compiler_driver_test_elf_section(ByteSlice image, String8 name)
{
    u64 offset = 0;
    u64 size = 0;
    u64 address = 0;
    if (!compiler_driver_test_elf_section_find(image, name, &offset, &size, &address))
    {
        return (ByteSlice){0};
    }
    return (ByteSlice){
        .pointer = image.pointer + offset,
        .length = size,
    };
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u64 compiler_driver_test_elf_section_address(ByteSlice image, String8 name)
{
    u64 offset = 0;
    u64 size = 0;
    u64 address = 0;
    if (!compiler_driver_test_elf_section_find(image, name, &offset, &size, &address))
    {
        return 0;
    }
    return address;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_bytes_contain(ByteSlice bytes, String8 needle)
{
    if (needle.length && needle.length <= bytes.length)
    {
        for (u64 offset = 0; offset + needle.length <= bytes.length; offset += 1)
        {
            if (memcmp(bytes.pointer + offset, needle.pointer, needle.length) == 0)
            {
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_aarch64_tied_input_load(ObjectFile* object)
{
    if (object && object->error == OBJECT_ERROR_NONE && object->section_count > OBJECT_SECTION_TEXT && object->sections && object->symbols)
    {
        ByteSlice text = object->sections[OBJECT_SECTION_TEXT].data;
        // Object-reader/disassembly sequence for int numeric_tied_output with an empty asm body:
        // ldr w9, [x28, #0x20]; str w9, [x28, #0x10].  The adjacent load/store
        // proves that the tied input reaches the reused output register and is
        // then published through the output place.
        static u8 const tied_sequence[] = {
            0x89, 0x23, 0x40, 0xb9,
            0x89, 0x13, 0x00, 0xb9,
        };
        for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = object->symbols + symbol_index;
            if (symbol->kind != OBJECT_SYMBOL_FUNCTION || symbol->section != OBJECT_SECTION_TEXT || !string_equal(symbol->name, S8("numeric_tied_output")) ||
                symbol->value > text.length || symbol->size > text.length - symbol->value)
            {
                continue;
            }
            u64 function_end = symbol->value + symbol->size;
            for (u64 offset = symbol->value; offset + sizeof(tied_sequence) <= function_end; offset += 4)
            {
                if (memcmp(text.pointer + offset, tied_sequence, sizeof(tied_sequence)) == 0)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL u16 compiler_driver_test_pe_read_u16(ByteSlice image, u64 offset)
{
    u16 value = 0;
    if (offset <= image.length && image.length - offset >= sizeof(value))
    {
        memcpy(&value, image.pointer + offset, sizeof(value));
    }
    return value;
}

BUSTER_GLOBAL_LOCAL u32 compiler_driver_test_pe_read_u32(ByteSlice image, u64 offset)
{
    u32 value = 0;
    if (offset <= image.length && image.length - offset >= sizeof(value))
    {
        memcpy(&value, image.pointer + offset, sizeof(value));
    }
    return value;
}

BUSTER_GLOBAL_LOCAL u64 compiler_driver_test_pe_read_u64(ByteSlice image, u64 offset)
{
    u64 value = 0;
    if (offset <= image.length && image.length - offset >= sizeof(value))
    {
        memcpy(&value, image.pointer + offset, sizeof(value));
    }
    return value;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_test_pe_rva_to_file_offset(ByteSlice image, u64 section_table, u16 section_count, u32 rva, u64 size,
                                                                     u64* file_offset)
{
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 header = section_table + (u64)section_index * 40;
        if (header > image.length || image.length - header < 40)
        {
            return false;
        }
        u32 virtual_size = compiler_driver_test_pe_read_u32(image, header + 8);
        u32 virtual_address = compiler_driver_test_pe_read_u32(image, header + 12);
        u32 raw_size = compiler_driver_test_pe_read_u32(image, header + 16);
        u32 raw_offset = compiler_driver_test_pe_read_u32(image, header + 20);
        if (rva < virtual_address)
        {
            continue;
        }
        u64 relative = (u64)rva - virtual_address;
        u64 mapped_size = BUSTER_MAX(virtual_size, raw_size);
        if (relative > mapped_size || size > mapped_size - relative || relative > raw_size || size > (u64)raw_size - relative || raw_offset > image.length ||
            relative > image.length - raw_offset || size > image.length - raw_offset - relative)
        {
            continue;
        }
        *file_offset = raw_offset + relative;
        return *file_offset <= image.length && size <= image.length - *file_offset;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_pe_tls_directory(ByteSlice image, u32 expected_alignment)
{
    if (image.length < 0x40 || image.pointer[0] != 'M' || image.pointer[1] != 'Z')
    {
        return false;
    }
    u32 pe_offset = compiler_driver_test_pe_read_u32(image, 0x3c);
    if (pe_offset > image.length || image.length - pe_offset < 4 + 20)
    {
        return false;
    }
    u64 coff = pe_offset + 4;
    u16 section_count = compiler_driver_test_pe_read_u16(image, coff + 2);
    u16 optional_size = compiler_driver_test_pe_read_u16(image, coff + 16);
    u64 optional = coff + 20;
    if (image.length - optional < optional_size || optional_size < 192 || compiler_driver_test_pe_read_u16(image, optional) != 0x20b)
    {
        return false;
    }
    u64 section_table = optional + optional_size;
    if ((u64)section_count * 40 > image.length - section_table)
    {
        return false;
    }
    u32 tls_rva = compiler_driver_test_pe_read_u32(image, optional + 184);
    u32 tls_size = compiler_driver_test_pe_read_u32(image, optional + 188);
    u64 image_base = compiler_driver_test_pe_read_u64(image, optional + 24);
    if (!tls_rva || tls_size != 40)
    {
        return false;
    }
    u64 tls_directory = 0;
    if (!compiler_driver_test_pe_rva_to_file_offset(image, section_table, section_count, tls_rva, tls_size, &tls_directory))
    {
        return false;
    }
    u64 tls_start = compiler_driver_test_pe_read_u64(image, tls_directory);
    u64 tls_end = compiler_driver_test_pe_read_u64(image, tls_directory + 8);
    u64 tls_index = compiler_driver_test_pe_read_u64(image, tls_directory + 16);
    u64 tls_callbacks = compiler_driver_test_pe_read_u64(image, tls_directory + 24);
    u32 tls_zero_fill = compiler_driver_test_pe_read_u32(image, tls_directory + 32);
    u32 tls_characteristics = compiler_driver_test_pe_read_u32(image, tls_directory + 36);
    if (!BUSTER_IS_POWER_OF_TWO(expected_alignment) || expected_alignment > 8192 || !tls_start || tls_end < tls_start || tls_zero_fill != 0 || !tls_callbacks)
    {
        return false;
    }
    u32 expected_shift = 0;
    for (u32 alignment = expected_alignment; alignment > 1; alignment >>= 1)
    {
        expected_shift += 1;
    }
    if (tls_characteristics != (expected_shift + 1) << 20 || tls_start < image_base || tls_index < image_base || tls_callbacks < image_base)
    {
        return false;
    }
    bool tls_section_found = false;
    u32 tls_virtual_size = 0;
    u32 tls_raw_size = 0;
    u32 tls_raw_offset = 0;
    u32 tls_virtual_address = 0;
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 header = section_table + (u64)section_index * 40;
        if (memcmp(image.pointer + header, ".tls", 4) != 0)
        {
            continue;
        }
        tls_section_found = true;
        tls_virtual_size = compiler_driver_test_pe_read_u32(image, header + 8);
        tls_virtual_address = compiler_driver_test_pe_read_u32(image, header + 12);
        tls_raw_size = compiler_driver_test_pe_read_u32(image, header + 16);
        tls_raw_offset = compiler_driver_test_pe_read_u32(image, header + 20);
        break;
    }
    if (!tls_section_found || !tls_virtual_size || tls_raw_size < tls_virtual_size || !tls_raw_offset || tls_raw_offset > image.length ||
        tls_raw_size > image.length - tls_raw_offset || image_base > UINT64_MAX - tls_virtual_address ||
        tls_start != image_base + tls_virtual_address || tls_end - tls_start != tls_virtual_size)
    {
        return false;
    }
    u64 index_rva = tls_index - image_base;
    u64 callbacks_rva = tls_callbacks - image_base;
    if (index_rva > UINT32_MAX || callbacks_rva > UINT32_MAX)
    {
        return false;
    }
    u64 index_file_offset = 0;
    u64 callbacks_file_offset = 0;
    if (!compiler_driver_test_pe_rva_to_file_offset(image, section_table, section_count, (u32)index_rva, sizeof(u32), &index_file_offset) ||
        !compiler_driver_test_pe_rva_to_file_offset(image, section_table, section_count, (u32)callbacks_rva, sizeof(u64), &callbacks_file_offset) ||
        compiler_driver_test_pe_read_u64(image, callbacks_file_offset) != 0)
    {
        return false;
    }
    return true;
}

#if BUSTER_WINDOWS
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_process_success(Arena* arena, String8 path)
{
    String8 run_arguments[] = {path};
    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                (ProcessSpawnOptions){.use_process_environment = true});
    if (!spawn.handle)
    {
        return false;
    }
    return os_process_wait_sync(arena, spawn).result == PROCESS_RESULT_SUCCESS;
}
#endif

#if BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_windows_x64_unwind_records(ObjectFile* object, bool* has_frame_register)
{
    if (!object || !object->sections || !has_frame_register)
    {
        return false;
    }
    *has_frame_register = false;
    ByteSlice xdata = object->sections[OBJECT_SECTION_WINDOWS_XDATA].data;
    u32 record_count = 0;
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation* relocation = object->relocations + relocation_index;
        if (relocation->section != OBJECT_SECTION_WINDOWS_PDATA || relocation->offset % 12 != 8 || relocation->addend < 0)
        {
            continue;
        }
        u64 xdata_offset = (u64)relocation->addend;
        if (xdata_offset % 4 || xdata_offset > xdata.length || xdata.length - xdata_offset < 4)
        {
            return false;
        }
        u8 const* record = xdata.pointer + xdata_offset;
        u64 record_bytes = 4 + (u64)record[2] * 2;
        if (record_bytes > xdata.length - xdata_offset)
        {
            return false;
        }
        record_count += 1;
        *has_frame_register |= (record[3] & 15) == 5 && (record[3] >> 4) == 0;
        for (u32 code_index = 0; code_index < record[2];)
        {
            u8 unwind = record[5 + code_index * 2];
            u8 operation = unwind & 15;
            u8 information = unwind >> 4;
            if (operation == 0 || operation == 2 || operation == 3)
            {
                code_index += 1;
            }
            else if (operation == 1 && information == 0)
            {
                if (code_index + 1 >= record[2])
                {
                    return false;
                }
                code_index += 2;
            }
            else if (operation == 1 && information == 1)
            {
                if (code_index + 2 >= record[2])
                {
                    return false;
                }
                code_index += 3;
            }
            else
            {
                return false;
            }
        }
    }
    return record_count != 0;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_windows_x64_has_frame_lea(ObjectFile* object)
{
    if (object)
    {
        ByteSlice text = object->sections[OBJECT_SECTION_TEXT].data;
        for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = object->symbols + symbol_index;
            if (symbol->kind != OBJECT_SYMBOL_FUNCTION || symbol->section != OBJECT_SECTION_TEXT || symbol->value > text.length || symbol->size > text.length - symbol->value)
            {
                continue;
            }
            u64 function_end = symbol->value + symbol->size;
            for (u64 offset = symbol->value; offset + 6 <= function_end; offset += 1)
            {
                bool short_lea = text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x8d && text.pointer[offset + 2] == 0x65 &&
                                 text.pointer[offset + 4] == 0x5d && text.pointer[offset + 5] == 0xc3;
                bool long_lea = offset + 9 <= function_end && text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x8d &&
                                text.pointer[offset + 2] == 0xa5 && text.pointer[offset + 7] == 0x5d && text.pointer[offset + 8] == 0xc3;
                if (short_lea || long_lea)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_x64_fallthrough_epilog(ByteSlice text, u64 offset, u64 function_end, bool windows)
{
    if (offset > function_end)
    {
        return false;
    }
    u64 remaining = function_end - offset;
    if (windows)
    {
        bool pop_return = remaining >= 2 && text.pointer[offset] == 0x5d && text.pointer[offset + 1] == 0xc3;
        bool add8_pop_return = remaining >= 6 && text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x83 &&
                               text.pointer[offset + 2] == 0xc4 && text.pointer[offset + 4] == 0x5d && text.pointer[offset + 5] == 0xc3;
        bool add32_pop_return = remaining >= 9 && text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x81 &&
                                text.pointer[offset + 2] == 0xc4 && text.pointer[offset + 7] == 0x5d && text.pointer[offset + 8] == 0xc3;
        bool lea8_pop_return = remaining >= 6 && text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x8d &&
                               text.pointer[offset + 2] == 0x65 && text.pointer[offset + 4] == 0x5d && text.pointer[offset + 5] == 0xc3;
        bool lea32_pop_return = remaining >= 9 && text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x8d &&
                                text.pointer[offset + 2] == 0xa5 && text.pointer[offset + 7] == 0x5d && text.pointer[offset + 8] == 0xc3;
        return pop_return || add8_pop_return || add32_pop_return || lea8_pop_return || lea32_pop_return;
    }
    return remaining >= 2 && text.pointer[offset] == 0xc9 && text.pointer[offset + 1] == 0xc3;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u32 compiler_driver_test_x64_restore_rbx_size(ByteSlice text, u64 offset, u64 function_end)
{
    if (text.pointer && offset <= function_end)
    {
        u64 remaining = function_end - offset;
        // MOV rbx, [rbp+disp8] is the canonical compact form; a large frame
        // uses the equivalent disp32 form.  Both restore the same callee-saved
        // register and must be recognized by the semantic edge scan.
        if (remaining >= 4 && text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x8b && text.pointer[offset + 2] == 0x5d)
        {
            return 4;
        }
        if (remaining >= 7 && text.pointer[offset] == 0x48 && text.pointer[offset + 1] == 0x8b && text.pointer[offset + 2] == 0x9d)
        {
            return 7;
        }
    }

    return 0;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_x64_unconditional_jump(ByteSlice text, u64 offset, u64 function_end)
{
    bool result;
    if (!text.pointer || offset > function_end)
    {
        result = false;
    }
    else
    {
        u64 remaining = function_end - offset;
        // Canonical metadata may select either the short or near unconditional
        // branch when the target displacement permits it.
        result = (remaining >= 2 && text.pointer[offset] == 0xeb) || (remaining >= 5 && text.pointer[offset] == 0xe9);
    }

    return result;
}

#endif

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_windows_x64_dynamic_rbx(ObjectFile* object)
{
    if (!object || object->error != OBJECT_ERROR_NONE || !object->sections || !object->symbols || !object->relocations)
    {
        return false;
    }
    u32 function_symbol = UINT32_MAX;
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        ObjectSymbol* symbol = object->symbols + symbol_index;
        if (symbol->kind == OBJECT_SYMBOL_FUNCTION && symbol->section == OBJECT_SECTION_TEXT && string_equal(symbol->name, S8("dynamic_stack_fixed_b")))
        {
            function_symbol = symbol_index;
            break;
        }
    }
    if (function_symbol == UINT32_MAX)
    {
        return false;
    }
    ObjectSymbol* function = object->symbols + function_symbol;
    ByteSlice text = object->sections[OBJECT_SECTION_TEXT].data;
    if (function->value > text.length || function->size > text.length - function->value)
    {
        return false;
    }
    u64 function_end = function->value + function->size;
    bool found_save = false;
    bool found_restore = false;
    s32 save_displacement = 0;
    s32 restore_displacement = 0;
    // The canonical encoder uses the compact disp8 form whenever the frame
    // slot fits, and widens to disp32 for larger offsets.  Decode both forms
    // semantically instead of assuming every ModRM memory operand consumes a
    // seven-byte disp32 sequence.
    for (u64 offset = function->value; offset < function_end;)
    {
        u64 next_offset = offset + 1;
        if (offset + 3 <= function_end && text.pointer[offset] == 0x48 &&
            (text.pointer[offset + 1] == 0x89 || text.pointer[offset + 1] == 0x8b))
        {
            u8 modrm = text.pointer[offset + 2];
            u8 mode = modrm >> 6;
            u8 reg = (modrm >> 3) & 7;
            u8 base = modrm & 7;
            if (reg == 3 && base == 5 && (mode == 1 || mode == 2))
            {
                s32 displacement = 0;
                if (mode == 1 && offset + 4 <= function_end)
                {
                    displacement = (s32)(s8)text.pointer[offset + 3];
                    next_offset = offset + 4;
                }
                else if (mode == 2 && offset + 7 <= function_end)
                {
                    memcpy(&displacement, text.pointer + offset + 3, sizeof(displacement));
                    next_offset = offset + 7;
                }
                else
                {
                    offset = next_offset;
                    continue;
                }
                if (text.pointer[offset + 1] == 0x89)
                {
                    save_displacement = displacement;
                    found_save = true;
                }
                else
                {
                    restore_displacement = displacement;
                    found_restore = true;
                }
            }
        }
        offset = next_offset;
    }
    if (!found_save || !found_restore || save_displacement != restore_displacement || save_displacement < 0)
    {
        return false;
    }
    u64 pdata_offset = UINT64_MAX;
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation* relocation = object->relocations + relocation_index;
        if (relocation->section == OBJECT_SECTION_WINDOWS_PDATA && relocation->kind == OBJECT_RELOCATION_COFF_ADDR32NB &&
            relocation->offset % 12 == 0 && relocation->symbol == function_symbol)
        {
            pdata_offset = relocation->offset;
            break;
        }
    }
    if (pdata_offset == UINT64_MAX)
    {
        return false;
    }
    u64 xdata_offset = UINT64_MAX;
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation* relocation = object->relocations + relocation_index;
        if (relocation->section == OBJECT_SECTION_WINDOWS_PDATA && relocation->kind == OBJECT_RELOCATION_COFF_ADDR32NB &&
            relocation->offset == pdata_offset + 8 && relocation->addend >= 0)
        {
            xdata_offset = (u64)relocation->addend;
            break;
        }
    }
    ByteSlice xdata = object->sections[OBJECT_SECTION_WINDOWS_XDATA].data;
    if (xdata_offset == UINT64_MAX || xdata_offset > xdata.length || xdata.length - xdata_offset < 4)
    {
        return false;
    }
    u8 code_slots = xdata.pointer[xdata_offset + 2];
    if (xdata_offset + 4 + (u64)code_slots * 2 > xdata.length)
    {
        return false;
    }
    u32 frame_size = 0;
    bool found_rbx_unwind = false;
    for (u32 code_index = 0; code_index < code_slots;)
    {
        u8 unwind = xdata.pointer[xdata_offset + 5 + code_index * 2];
        u8 operation = unwind & 15;
        u8 information = unwind >> 4;
        if (operation == 0 || operation == 3)
        {
            code_index += 1;
        }
        else if (operation == 1 && information == 0)
        {
            if (code_index + 1 >= code_slots)
            {
                return false;
            }
            u16 scaled = 0;
            memcpy(&scaled, xdata.pointer + xdata_offset + 6 + code_index * 2, sizeof(scaled));
            if (frame_size > UINT32_MAX - (u32)scaled * 8)
            {
                return false;
            }
            frame_size += (u32)scaled * 8;
            code_index += 2;
        }
        else if (operation == 1 && information == 1)
        {
            if (code_index + 2 >= code_slots)
            {
                return false;
            }
            u32 allocation = 0;
            memcpy(&allocation, xdata.pointer + xdata_offset + 6 + code_index * 2, sizeof(allocation));
            if (frame_size > UINT32_MAX - allocation)
            {
                return false;
            }
            frame_size += allocation;
            code_index += 3;
        }
        else if (operation == 2)
        {
            u32 allocation = ((u32)information + 1) * 8;
            if (frame_size > UINT32_MAX - allocation)
            {
                return false;
            }
            frame_size += allocation;
            code_index += 1;
        }
        else if (operation == 4 && information == 3)
        {
            if (code_index + 1 >= code_slots)
            {
                return false;
            }
            u16 scaled = 0;
            memcpy(&scaled, xdata.pointer + xdata_offset + 6 + code_index * 2, sizeof(scaled));
            found_rbx_unwind = (u32)scaled * 8 == (u32)save_displacement;
            code_index += 2;
        }
        else if (operation == 5 && information == 3)
        {
            if (code_index + 2 >= code_slots)
            {
                return false;
            }
            u32 far_offset = 0;
            memcpy(&far_offset, xdata.pointer + xdata_offset + 6 + code_index * 2, sizeof(far_offset));
            found_rbx_unwind = far_offset == (u32)save_displacement;
            code_index += 3;
        }
        else
        {
            code_index += 1;
        }
    }
    return found_rbx_unwind && frame_size != 0 && (u64)save_displacement + 8 <= frame_size;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_test_label_relocations(ObjectFile* object, u32* count_out)
{
    if (!object || !count_out || object->error != OBJECT_ERROR_NONE || !object->sections || !object->symbols || !object->relocations ||
        object->symbol_count == 0)
    {
        return false;
    }
    ByteSlice text = object->sections[OBJECT_SECTION_TEXT].data;
    u32 count = 0;
    bool distinct_addend = false;
    s64 first_addend = 0;
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation* relocation = object->relocations + relocation_index;
        if (relocation->section != OBJECT_SECTION_DATA || relocation->kind != OBJECT_RELOCATION_ABSOLUTE64)
        {
            continue;
        }
        if (relocation->symbol >= object->symbol_count || relocation->offset > object->sections[OBJECT_SECTION_DATA].data.length ||
            sizeof(u64) > object->sections[OBJECT_SECTION_DATA].data.length - relocation->offset)
        {
            return false;
        }
        ObjectSymbol* symbol = object->symbols + relocation->symbol;
        if (symbol->section != OBJECT_SECTION_TEXT || symbol->kind != OBJECT_SYMBOL_FUNCTION || relocation->addend < 0 ||
            (symbol->size && (u64)relocation->addend >= symbol->size) || symbol->value > text.length || (u64)relocation->addend > text.length - symbol->value)
        {
            return false;
        }
        for (u32 previous_index = 0; previous_index < relocation_index; previous_index += 1)
        {
            ObjectRelocation* previous = object->relocations + previous_index;
            if (previous->section == OBJECT_SECTION_DATA && previous->kind == OBJECT_RELOCATION_ABSOLUTE64 && previous->offset == relocation->offset)
            {
                return false;
            }
        }
        if (count == 0)
        {
            first_addend = relocation->addend;
        }
        else
        {
            distinct_addend |= relocation->addend != first_addend;
        }
        count += 1;
    }
    *count_out = count;
    return count >= 2 && distinct_addend;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_label_relocations_roundtrip(ObjectFile* source, ObjectFile* roundtrip)
{
    if (!source || !roundtrip || source->error != OBJECT_ERROR_NONE || roundtrip->error != OBJECT_ERROR_NONE)
    {
        return false;
    }
    u32 source_count = 0;
    u32 roundtrip_count = 0;
    if (!compiler_driver_test_label_relocations(source, &source_count) || !compiler_driver_test_label_relocations(roundtrip, &roundtrip_count) ||
        source_count != roundtrip_count)
    {
        return false;
    }
    for (u32 source_index = 0; source_index < source->relocation_count; source_index += 1)
    {
        ObjectRelocation* source_relocation = source->relocations + source_index;
        if (source_relocation->section != OBJECT_SECTION_DATA || source_relocation->kind != OBJECT_RELOCATION_ABSOLUTE64)
        {
            continue;
        }
        if (source_relocation->symbol >= source->symbol_count)
        {
            return false;
        }
        String8 source_symbol = source->symbols[source_relocation->symbol].name;
        bool found = false;
        for (u32 roundtrip_index = 0; roundtrip_index < roundtrip->relocation_count; roundtrip_index += 1)
        {
            ObjectRelocation* roundtrip_relocation = roundtrip->relocations + roundtrip_index;
            if (roundtrip_relocation->section != OBJECT_SECTION_DATA || roundtrip_relocation->kind != OBJECT_RELOCATION_ABSOLUTE64 ||
                roundtrip_relocation->offset != source_relocation->offset || roundtrip_relocation->addend != source_relocation->addend ||
                roundtrip_relocation->symbol >= roundtrip->symbol_count)
            {
                continue;
            }
            found = string_equal(roundtrip->symbols[roundtrip_relocation->symbol].name, source_symbol);
            if (found)
            {
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL ByteSlice compiler_driver_test_archive(Arena* arena, ByteSlice* members, String8* names, u32 member_count)
{
    u64 size = 8;
    for (u32 member_index = 0; member_index < member_count; member_index += 1)
    {
        size += 60 + members[member_index].length;
        size += size & 1;
    }
    ByteSlice result = {
        .pointer = arena_allocate(arena, u8, size),
        .length = size,
    };
    memcpy(result.pointer, "!<arch>\n", 8);
    u64 cursor = 8;
    for (u32 member_index = 0; member_index < member_count; member_index += 1)
    {
        memset(result.pointer + cursor, ' ', 60);
        u64 name_length = BUSTER_MIN(names[member_index].length, (u64)15);
        memcpy(result.pointer + cursor, names[member_index].pointer, name_length);
        result.pointer[cursor + name_length] = '/';
        String8 member_size = string_format(arena, S8("{u64}"), members[member_index].length);
        memcpy(result.pointer + cursor + 48, member_size.pointer, member_size.length);
        result.pointer[cursor + 58] = '`';
        result.pointer[cursor + 59] = '\n';
        cursor += 60;
        memcpy(result.pointer + cursor, members[member_index].pointer, members[member_index].length);
        cursor += members[member_index].length;
        if (cursor & 1)
        {
            result.pointer[cursor++] = '\n';
        }
    }
    return result;
}

// One lane's share of the prewarm gang test. The C frontend is driven through
// preprocessing, syntax parsing, semantic analysis, and canonical IR lowering.
// Test macros are not thread-safe, so each lane only records observations and
// the caller compares them afterwards.
enum
{
    COMPILER_PREWARM_MAX_LANE_COUNT = 8,
};

typedef struct CompilerPrewarmLaneObservation CompilerPrewarmLaneObservation;
struct CompilerPrewarmLaneObservation
{
    u64 c_token_count;
    u64 c_error_count;
    u64 c_declaration_count;
    u64 c_lower_diagnostic_count;
    u64 c_ir_function_count;
    u32 abi_cpu_arch;
    u8 ran;
    u8 reserved[3];
};

typedef struct CompilerPrewarmGangState CompilerPrewarmGangState;
struct CompilerPrewarmGangState
{
    String8 c_source;
    CompilerPrewarmLaneObservation observations[COMPILER_PREWARM_MAX_LANE_COUNT];
};

BUSTER_GLOBAL_LOCAL void compiler_prewarm_observe(CompilerPrewarmGangState* state, CompilerPrewarmLaneObservation* observation)
{
    Arena* result_arena = arena_create((ArenaCreation){0});
    Target target = target_native;
    CPreprocessResult preprocess = c_preprocess(result_arena, state->c_source, (CPreprocessOptions){
        .target = target,
        .data_layout = target_data_layout(target),
    });
    observation->c_token_count = preprocess.token_count;
    observation->c_error_count = preprocess.error_count;
    CParserResult syntax = c_parse_ast(result_arena, preprocess);
    observation->c_declaration_count = syntax.declaration_count;
    CIRLowerResult lowered = c_analyze(result_arena, S8("compiler-prewarm.c"), preprocess, syntax, target);
    observation->c_lower_diagnostic_count = lowered.diagnostic_count;
    observation->c_ir_function_count = lowered.program && lowered.program->module_count ? lowered.program->modules[0].function_count : 0;
    observation->abi_cpu_arch = (u32)codegen_target_for_abi(CODEGEN_ABI_X86_64_SYSTEM_V).cpu_arch;
    observation->ran = 1;
    arena_destroy(result_arena, 1);
}

BUSTER_GLOBAL_LOCAL ThreadReturnType compiler_prewarm_gang(void* argument)
{
    CompilerPrewarmGangState* state = (CompilerPrewarmGangState*)argument;
    compiler_prewarm_observe(state, &state->observations[lane_index()]);
}

UnitTestResult compiler_driver_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

    // compiler_prewarm() is the contract that lets a gang compile at all: the
    // frontends' remaining first-use tables are written once and read
    // afterwards through plain loads, so they must already be complete when
    // the second lane starts. Prewarm, run the same work on every lane, and
    // require every lane to reproduce the serial answer exactly -- a table
    // left out of the prewarm shows up either as this mismatch or as the
    // BUSTER_CHECK_SERIAL_INITIALIZATION the build hits from inside the gang.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        CompilerPrewarmGangState* state = arena_allocate(arena, CompilerPrewarmGangState, 1);
        memset(state, 0, sizeof(*state));
        state->c_source = S8("int compiler_prewarm_probe(int value)\n{\n    return value ? value + 1 : 0;\n}\n");

        compiler_prewarm();

#if BUSTER_SINGLE_THREADED
        u64 lanes = 1;
#else
        u64 lanes = BUSTER_MIN((u64)COMPILER_PREWARM_MAX_LANE_COUNT, (u64)os_get_logical_thread_count());
        lanes = buster_test_worker_count(lanes);
#endif
        lane_run(lanes, &compiler_prewarm_gang, state);

        CompilerPrewarmLaneObservation serial = {0};
        compiler_prewarm_observe(state, &serial);
        BUSTER_TEST(arguments, serial.ran && serial.c_token_count && !serial.c_error_count && !serial.c_lower_diagnostic_count && serial.c_ir_function_count);

        bool lanes_agree = true;
        for (u64 lane = 0; lane < lanes; lane += 1)
        {
            CompilerPrewarmLaneObservation* observation = &state->observations[lane];
            lanes_agree = lanes_agree && observation->ran && observation->c_token_count == serial.c_token_count &&
                          observation->c_error_count == serial.c_error_count &&
                          observation->c_declaration_count == serial.c_declaration_count &&
                          observation->c_lower_diagnostic_count == serial.c_lower_diagnostic_count &&
                          observation->c_ir_function_count == serial.c_ir_function_count && observation->abi_cpu_arch == serial.abi_cpu_arch;
        }
        BUSTER_TEST(arguments, lanes_agree);

        arena->position = position;
    }

    Target wasm64_target = {
        .cpu_arch = CPU_ARCH_WASM64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_FREESTANDING,
    };
    String8 wasm64_output = buster_test_temporary_path(arguments->arena, S8("buster-driver-wasm64"), S8(".wasm"));
    String8 wasm64_command_line[] = {
        S8("-target"), S8("wasm64-unknown-freestanding"), S8("-nostdinc"), S8("-o"), wasm64_output, S8("tests/basic_c_wasm64.c"),
    };
    CompilerDriverResult wasm64_compile = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wasm64_command_line)));
    if (wasm64_compile.error != COMPILER_DRIVER_ERROR_NONE && wasm64_compile.diagnostic.length)
    {
        arguments->show(arguments, S8("Wasm64 compiler driver error: {S8}\n"), wasm64_compile.diagnostic);
    }
    BUSTER_TEST(arguments, wasm64_compile.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, wasm64_compile.has_wasm64 && wasm64_compile.wasm64.stats.memory64);
    BUSTER_TEST(arguments, wasm64_compile.wasm64.bytes.length >= 8);
    BUSTER_TEST(arguments, wasm64_compile.wasm64.bytes.length >= 8 &&
                               memcmp(wasm64_compile.wasm64.bytes.pointer, "\0asm\1\0\0\0", 8) == 0);
    BUSTER_TEST(arguments, file_read(arguments->arena, wasm64_output, (FileReadOptions){0}).length == wasm64_compile.wasm64.bytes.length);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(wasm64_target));

    String8 wasm64_assembly_command_line[] = {
        S8("-S"), S8("-target"), S8("wasm64-unknown-freestanding"), S8("tests/basic_c_wasm64.c"),
    };
    CompilerDriverResult wasm64_assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wasm64_assembly_command_line)));
    BUSTER_TEST(arguments, wasm64_assembly.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, wasm64_assembly.diagnostic, S8("-S is not supported for direct Wasm64 module output"));

    String8 uefi_targets[] = {
        S8("x86_64-unknown-uefi"),
        S8("aarch64-unknown-uefi"),
    };
    u16 uefi_machines[] = {0x8664, 0xaa64};
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(uefi_targets); target_index += 1)
    {
        String8 output_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-uefi"),
                                                         target_index ? S8("-aarch64.efi") : S8("-x86_64.efi"));
        String8 uefi_compile_command_line[] = {
            S8("-target"), uefi_targets[target_index], S8("-g0"), S8("-o"), output_path, S8("tests/basic_c_uefi.c"),
        };
        CompilerDriverResult uefi_compile = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(uefi_compile_command_line)));
        if (uefi_compile.error != COMPILER_DRIVER_ERROR_NONE && uefi_compile.diagnostic.length)
        {
            arguments->show(arguments, S8("UEFI compiler driver error for {S8}: {S8}\n"), uefi_targets[target_index], uefi_compile.diagnostic);
        }
        BUSTER_TEST(arguments, uefi_compile.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, uefi_compile.has_object);
        BUSTER_TEST(arguments, uefi_compile.object.target.os == OPERATING_SYSTEM_UEFI);
        BUSTER_TEST(arguments, uefi_compile.object.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length != 0);
        BUSTER_TEST(arguments, uefi_compile.object.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length != 0);
        ByteSlice image = uefi_compile.native_link.executable;
        bool image_valid = image.length >= 0x40 && image.pointer[0] == 'M' && image.pointer[1] == 'Z';
        BUSTER_TEST(arguments, image_valid);
        if (image_valid)
        {
            u32 pe_offset = compiler_driver_test_pe_read_u32(image, 0x3c);
            image_valid = pe_offset <= image.length && 24 <= image.length - pe_offset && memcmp(image.pointer + pe_offset, "PE\0\0", 4) == 0;
            BUSTER_TEST(arguments, image_valid);
            if (image_valid)
            {
                u64 coff = (u64)pe_offset + 4;
                u16 section_count = compiler_driver_test_pe_read_u16(image, coff + 2);
                u16 optional_size = compiler_driver_test_pe_read_u16(image, coff + 16);
                u64 optional = coff + 20;
                u64 section_table = optional + optional_size;
                bool headers_valid = optional_size >= 240 && section_table <= image.length &&
                                     (u64)section_count <= (image.length - section_table) / 40;
                BUSTER_TEST(arguments, headers_valid);
                if (headers_valid)
                {
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u16(image, coff) == uefi_machines[target_index]);
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u16(image, optional) == 0x20b);
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u16(image, optional + 68) == 10);
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u16(image, optional + 70) == 0x8160);
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u32(image, optional + 16) != 0);
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u64(image, optional + 24) == UINT64_C(0x140000000));
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u32(image, optional + 120) == 0);
                    BUSTER_TEST(arguments, compiler_driver_test_pe_read_u32(image, optional + 124) == 0);
                    u32 exception_rva = compiler_driver_test_pe_read_u32(image, optional + 136);
                    u32 exception_size = compiler_driver_test_pe_read_u32(image, optional + 140);
                    u32 relocation_rva = compiler_driver_test_pe_read_u32(image, optional + 152);
                    u32 relocation_size = compiler_driver_test_pe_read_u32(image, optional + 156);
                    BUSTER_TEST(arguments, exception_rva != 0 && exception_size != 0);
                    BUSTER_TEST(arguments, relocation_rva != 0 && relocation_size >= 12);
                    u64 exception_offset = 0;
                    u64 relocation_offset = 0;
                    BUSTER_TEST(arguments,
                                compiler_driver_test_pe_rva_to_file_offset(image, section_table, section_count, exception_rva, exception_size,
                                                                           &exception_offset));
                    bool relocation_mapped = compiler_driver_test_pe_rva_to_file_offset(image, section_table, section_count, relocation_rva,
                                                                                        relocation_size, &relocation_offset);
                    BUSTER_TEST(arguments, relocation_mapped);
                    if (relocation_mapped)
                    {
                        u16 relocation_entry = compiler_driver_test_pe_read_u16(image, relocation_offset + 8);
                        BUSTER_TEST(arguments, (relocation_entry >> 12) == 10);
                    }
                }
            }
        }
        ByteSlice written = file_read(arguments->arena, output_path, (FileReadOptions){0});
        BUSTER_TEST(arguments, written.length == image.length && written.length != 0);

        // AArch64 PE unwind emission requires the canonical prologue/epilogue
        // shape even when a machine register allocator was requested. Keep a
        // direct regression for the optimized driver path that exposed this.
        if (target_index == 1)
        {
            String8 optimized_output_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-uefi"), S8("-aarch64-fast.efi"));
            String8 optimized_command_line[] = {
                S8("-target"), uefi_targets[target_index], S8("-g0"), S8("-fregister-allocator=fast"), S8("-o"), optimized_output_path,
                S8("tests/basic_c_uefi.c"),
            };
            CompilerDriverResult optimized_compile = compiler_driver_execute_invocation(
                arguments->arena,
                compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(optimized_command_line)));
            if (optimized_compile.error != COMPILER_DRIVER_ERROR_NONE && optimized_compile.diagnostic.length)
            {
                arguments->show(arguments, S8("optimized AArch64 UEFI compiler driver error: {S8}\n"), optimized_compile.diagnostic);
            }
            BUSTER_TEST(arguments, optimized_compile.error == COMPILER_DRIVER_ERROR_NONE);
            BUSTER_TEST(arguments, optimized_compile.has_object);
            BUSTER_TEST(arguments, optimized_compile.object.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length != 0);
            BUSTER_TEST(arguments, optimized_compile.object.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length != 0);
            BUSTER_TEST(arguments, optimized_compile.native_link.executable.length != 0);
            BUSTER_TEST(arguments,
                        file_read(arguments->arena, optimized_output_path, (FileReadOptions){0}).length ==
                            optimized_compile.native_link.executable.length);
        }
    }

    {
        TemporalArena uefi_archive_temporary = scratch_begin(&arguments->arena, 1);
        Arena* uefi_archive_arena = uefi_archive_temporary.arena;
        String8 member_path = buster_test_temporary_path(uefi_archive_arena, S8("buster-driver-uefi-archive-member"), S8(".obj"));
        String8 member_command_line[] = {
            S8("-c"), S8("-target"), S8("x86_64-unknown-uefi"), S8("-g0"), S8("-o"), member_path,
            S8("tests/basic_c_uefi_archive_member.c"),
        };
        CompilerDriverResult member_compile = compiler_driver_execute_invocation(
            uefi_archive_arena,
            compiler_driver_parse_arguments(uefi_archive_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(member_command_line)));
        BUSTER_TEST(arguments, member_compile.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead member_map = file_map_read(uefi_archive_arena, member_path, (FileReadOptions){0});
        BUSTER_TEST(arguments, member_map.bytes.length != 0);
        ByteSlice archive_members[] = {member_map.bytes};
        String8 archive_member_names[] = {S8("uefi_archive_member.obj")};
        ByteSlice archive_bytes = compiler_driver_test_archive(uefi_archive_arena, archive_members, archive_member_names,
                                                               BUSTER_ARRAY_LENGTH(archive_members));
        file_map_unmap(member_map);

        String8 library_directory = buster_test_temporary_path(uefi_archive_arena, S8("buster-driver-uefi-library"), S8(""));
        os_make_directory(library_directory);
        String8 archive_path = string_format_z(uefi_archive_arena, S8("{S8}/libuefi_support.a"), library_directory);
        String8 shared_path = string_format_z(uefi_archive_arena, S8("{S8}/libuefi_support.so"), library_directory);
        BUSTER_TEST(arguments, file_write(archive_path, archive_bytes));
        BUSTER_TEST(arguments, file_write(shared_path, (ByteSlice){.pointer = (u8*)"ignored", .length = 7}));

        String8 archive_output = buster_test_temporary_path(uefi_archive_arena, S8("buster-driver-uefi-archive"), S8(".efi"));
        String8 archive_command_line[] = {
            S8("-target"), S8("x86_64-unknown-uefi"), S8("-g0"), S8("-o"), archive_output,
            S8("tests/basic_c_uefi_archive_main.c"), S8("-L"), library_directory, S8("-luefi_support"),
        };
        CompilerDriverResult archive_compile = compiler_driver_execute_invocation(
            uefi_archive_arena,
            compiler_driver_parse_arguments(uefi_archive_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(archive_command_line)));
        if (archive_compile.error != COMPILER_DRIVER_ERROR_NONE && archive_compile.diagnostic.length)
        {
            arguments->show(arguments, S8("UEFI static archive compiler driver error: {S8}\n"), archive_compile.diagnostic);
        }
        BUSTER_TEST(arguments, archive_compile.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, archive_compile.has_object);
        BUSTER_TEST(arguments, archive_compile.native_link.executable.length != 0);
        BUSTER_TEST(arguments,
                    file_read(uefi_archive_arena, archive_output, (FileReadOptions){0}).length == archive_compile.native_link.executable.length);
        scratch_end(uefi_archive_temporary);
    }

    String8 command_line[] = {
        S8("-c"),
        S8("-std=gnu23"),
        S8("-target"),
        S8("aarch64-linux-android"),
        S8("-mcpu=apple-m4"),
        S8("--sysroot=/sdk"),
        S8("-Iinclude"),
        S8("-isystem"),
        S8("system"),
        S8("-DDEBUG=1"),
        S8("-U"),
        S8("NDEBUG"),
        S8("-O2"),
        S8("-g"),
        S8("-Wall"),
        S8("-fPIC"),
        S8("-pthread"),
        S8("-L/sdk/lib"),
        S8("-l:libandroid.so"),
        S8("-Wl,--gc-sections"),
        S8("-fsource-metrics=metrics.txt"),
        S8("-o"),
        S8("output.o"),
        S8("source.c"),
    };
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
    BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, invocation.action == COMPILER_DRIVER_ACTION_OBJECT);
    BUSTER_TEST(arguments, invocation.c_dialect == COMPILER_DRIVER_C_DIALECT_GNU23);
    BUSTER_TEST(arguments, invocation.target.cpu_arch == CPU_ARCH_AARCH64);
    BUSTER_TEST(arguments, invocation.target.os == OPERATING_SYSTEM_ANDROID);
    BUSTER_TEST(arguments, invocation.target.cpu_model == CPU_MODEL_A64_APPLE_M4);
    BUSTER_TEST(arguments, invocation.debug_info);
    BUSTER_TEST(arguments, invocation.include_path_count == 1);
    BUSTER_TEST(arguments, invocation.system_include_path_count >= 4);
    BUSTER_STRING_TEST(arguments, invocation.system_include_paths[0], S8("system"));
    bool found_sysroot_multiarch = false;
    bool found_sysroot_include = false;
    for (u32 path_index = 0; path_index < invocation.system_include_path_count; path_index += 1)
    {
        found_sysroot_multiarch |= string_equal(invocation.system_include_paths[path_index], S8("/sdk/usr/include/"
                                                                                                "aarch64-linux-android"));
        found_sysroot_include |= string_equal(invocation.system_include_paths[path_index], S8("/sdk/usr/include"));
    }
    BUSTER_TEST(arguments, found_sysroot_multiarch);
    BUSTER_TEST(arguments, found_sysroot_include);
    BUSTER_TEST(arguments, invocation.definition_count == 1);
    BUSTER_TEST(arguments, invocation.undefinition_count == 1);
    BUSTER_TEST(arguments, invocation.library_path_count == 1);
    BUSTER_TEST(arguments, invocation.library_count == 1);
    BUSTER_STRING_TEST(arguments, invocation.library_paths[0], S8("/sdk/lib"));
    BUSTER_STRING_TEST(arguments, invocation.libraries[0], S8(":libandroid.so"));
    BUSTER_TEST(arguments, invocation.linker_argument_count == 1);
    BUSTER_TEST(arguments, invocation.input_count == 1);
    BUSTER_STRING_TEST(arguments, invocation.output_path, S8("output.o"));
    BUSTER_STRING_TEST(arguments, invocation.sysroot, S8("/sdk"));
    BUSTER_STRING_TEST(arguments, invocation.source_metrics_path, S8("metrics.txt"));
    BUSTER_TEST(arguments, invocation.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST);

    String8 register_allocator_default_command_line[] = {S8("source.c")};
    CompilerDriverInvocation register_allocator_default = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(register_allocator_default_command_line));
    BUSTER_TEST(arguments, register_allocator_default.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, register_allocator_default.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST);
    BUSTER_TEST(arguments, !register_allocator_default.register_allocator_explicit);

    String8 register_allocator_o0_command_line[] = {S8("-O0"), S8("source.c")};
    CompilerDriverInvocation register_allocator_o0 = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(register_allocator_o0_command_line));
    BUSTER_TEST(arguments, register_allocator_o0.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, register_allocator_o0.optimization_level == 0);
    BUSTER_TEST(arguments, register_allocator_o0.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST);

    String8 register_allocator_disabled_command_line[] = {S8("-O2"), S8("-fno-register-allocator"), S8("source.c")};
    CompilerDriverInvocation register_allocator_disabled = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(register_allocator_disabled_command_line));
    BUSTER_TEST(arguments, register_allocator_disabled.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, register_allocator_disabled.register_allocator == CODEGEN_REGISTER_ALLOCATOR_NONE);
    BUSTER_TEST(arguments, register_allocator_disabled.register_allocator_explicit);

    // Driver options follow the usual last-option-wins rule, so a later -O
    // restores the default allocator after an explicit opt-out.
    String8 register_allocator_reenabled_command_line[] = {S8("-fno-register-allocator"), S8("-O1"), S8("source.c")};
    CompilerDriverInvocation register_allocator_reenabled = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(register_allocator_reenabled_command_line));
    BUSTER_TEST(arguments, register_allocator_reenabled.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, register_allocator_reenabled.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST);

    String8 register_allocator_quality_command_line[] = {S8("-O3"), S8("-fregister-allocator=quality"), S8("source.c")};
    CompilerDriverInvocation register_allocator_quality = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(register_allocator_quality_command_line));
    BUSTER_TEST(arguments, register_allocator_quality.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, register_allocator_quality.register_allocator == CODEGEN_REGISTER_ALLOCATOR_QUALITY);
    BUSTER_TEST(arguments, register_allocator_quality.register_allocator_explicit);

    String8 uefi_command_line[] = {
        S8("--target=x86_64-unknown-uefi"), S8("--entry=FirmwareEntry"), S8("-isystem"), S8("firmware/include"), S8("source.c"),
    };
    CompilerDriverInvocation uefi_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(uefi_command_line));
    BUSTER_TEST(arguments, uefi_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, uefi_invocation.target.cpu_arch == CPU_ARCH_X86_64 && uefi_invocation.target.os == OPERATING_SYSTEM_UEFI);
    BUSTER_TEST(arguments, uefi_invocation.system_include_path_count == 1);
    BUSTER_STRING_TEST(arguments, uefi_invocation.system_include_paths[0], S8("firmware/include"));
    BUSTER_STRING_TEST(arguments, uefi_invocation.entry_symbol, S8("FirmwareEntry"));
    String8 uefi_linker_argument_command_line[] = {
        S8("--target=x86_64-unknown-uefi"), S8("-Wl,--gc-sections"), S8("source.c"),
    };
    CompilerDriverInvocation uefi_linker_argument =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(uefi_linker_argument_command_line));
    BUSTER_TEST(arguments, uefi_linker_argument.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, uefi_linker_argument.diagnostic, S8("raw linker arguments are not supported for UEFI targets"));
    String8 unsupported_uefi_command_line[] = {
        S8("--target=wasm64-unknown-uefi"), S8("source.c"),
    };
    CompilerDriverInvocation unsupported_uefi =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unsupported_uefi_command_line));
    BUSTER_TEST(arguments, unsupported_uefi.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, unsupported_uefi.diagnostic, S8("UEFI output is supported only for x86_64 and aarch64 targets"));
    String8 x86_cpu_command_line[] = {
        S8("-c"),
        S8("--target=x86_64-linux"),
        S8("-march=znver5"),
        S8("source.c"),
    };
    CompilerDriverInvocation x86_cpu_invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(x86_cpu_command_line));
    BUSTER_TEST(arguments, x86_cpu_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, x86_cpu_invocation.target.cpu_model == CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, target_vector_register_size(x86_cpu_invocation.target) == 64);
    String8 amd_extended_cpu_command_line[] = {
        S8("-c"),
        S8("--target=x86_64-linux"),
        S8("-march=bdver2"),
        S8("source.c"),
    };
    CompilerDriverInvocation amd_extended_cpu_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(amd_extended_cpu_command_line));
    BUSTER_TEST(arguments, amd_extended_cpu_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, amd_extended_cpu_invocation.target.cpu_model == CPU_MODEL_AMD_BD_2);
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_extended_cpu_invocation.target, TARGET_CPU_FEATURE_X86_FMA4));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_extended_cpu_invocation.target, TARGET_CPU_FEATURE_X86_LWP));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_extended_cpu_invocation.target, TARGET_CPU_FEATURE_X86_TBM));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_extended_cpu_invocation.target, TARGET_CPU_FEATURE_X86_XOP));
    BUSTER_TEST(arguments, !target_cpu_feature_has(amd_extended_cpu_invocation.target, TARGET_CPU_FEATURE_X86_3DNOW));
    BUSTER_TEST(arguments, !target_cpu_feature_has(amd_extended_cpu_invocation.target, TARGET_CPU_FEATURE_X86_3DNOWA));
    String8 amd_feature_command_line[] = {
        S8("-c"),
        S8("--target=x86_64-linux"),
        S8("-march=baseline"),
        S8("-mattr=+avx,+3dnow,+3dnowa,+fma4,+lwp,+tbm,+xop"),
        S8("source.c"),
    };
    CompilerDriverInvocation amd_feature_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(amd_feature_command_line));
    BUSTER_TEST(arguments, amd_feature_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_feature_invocation.target, TARGET_CPU_FEATURE_X86_3DNOW));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_feature_invocation.target, TARGET_CPU_FEATURE_X86_3DNOWA));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_feature_invocation.target, TARGET_CPU_FEATURE_X86_FMA4));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_feature_invocation.target, TARGET_CPU_FEATURE_X86_LWP));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_feature_invocation.target, TARGET_CPU_FEATURE_X86_TBM));
    BUSTER_TEST(arguments, target_cpu_feature_has(amd_feature_invocation.target, TARGET_CPU_FEATURE_X86_XOP));
    String8 feature_command_line[] = {
        S8("-c"),
        S8("-mattr=+avx512f,+avx512vl,+avx2,+avx512bw,+ibt,+cldemote,+prefetchi,+shstk,+movrs,+vmx,+svm"),
        S8("--target=x86_64-linux"),
        S8("-march=haswell"),
        S8("-masm"),
        S8("att"),
        S8("source.c"),
    };
    CompilerDriverInvocation feature_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(feature_command_line));
    BUSTER_TEST(arguments, feature_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, feature_invocation.assembly_syntax == ASSEMBLY_SYNTAX_ATT);
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX2));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX512F));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX512VL));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX512BW));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_IBT));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_CLDEMOTE));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_PREFETCHI));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_SHSTK));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_MOVRS));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_VMX));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_SVM));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_FSGSBASE));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_XSAVE));
    BUSTER_TEST(arguments, !target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_XSAVES));
    String8 scalar_feature_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=baseline"),
        S8("-mattr=+avx,+f16c,+fma,+ssse3,+sse4.1,+sse4.2,+bmi2,+adx,+movbe,+rdrand,+rdseed,+sha,+waitpkg,+pku,+ptwrite,+serialize,+clflushopt,+clwb,+fsgsbase,+rtm,+tsxldtrk,+uintr,+prefetchwt1"),
        S8("-mattr=-fma,+fma,-ssse3,+ssse3"), S8("source.c"),
    };
    CompilerDriverInvocation scalar_feature_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(scalar_feature_command_line));
    BUSTER_TEST(arguments, scalar_feature_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    TargetCpuFeatures scalar_features = scalar_feature_invocation.target.cpu_features;
    TargetCpuFeature scalar_feature_bits[] = {
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_F16C, TARGET_CPU_FEATURE_X86_FMA,
        TARGET_CPU_FEATURE_X86_SSSE3, TARGET_CPU_FEATURE_X86_SSE4_1, TARGET_CPU_FEATURE_X86_SSE4_2,
        TARGET_CPU_FEATURE_X86_BMI2, TARGET_CPU_FEATURE_X86_ADX, TARGET_CPU_FEATURE_X86_MOVBE,
        TARGET_CPU_FEATURE_X86_RDRAND, TARGET_CPU_FEATURE_X86_RDSEED, TARGET_CPU_FEATURE_X86_SHA, TARGET_CPU_FEATURE_X86_WAITPKG,
        TARGET_CPU_FEATURE_X86_PKU, TARGET_CPU_FEATURE_X86_PTWRITE, TARGET_CPU_FEATURE_X86_SERIALIZE,
        TARGET_CPU_FEATURE_X86_CLFLUSHOPT, TARGET_CPU_FEATURE_X86_CLWB, TARGET_CPU_FEATURE_X86_FSGSBASE,
        TARGET_CPU_FEATURE_X86_RTM, TARGET_CPU_FEATURE_X86_TSXLDTRK, TARGET_CPU_FEATURE_X86_UINTR,
        TARGET_CPU_FEATURE_X86_PREFETCHWT1,
    };
    for (u32 scalar_index = 0; scalar_index < BUSTER_ARRAY_LENGTH(scalar_feature_bits); scalar_index += 1)
    {
        BUSTER_TEST(arguments, target_cpu_features_contains(scalar_features, scalar_feature_bits[scalar_index]));
    }
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, scalar_feature_invocation.target),
                       S8("adx,avx,bmi2,clflushopt,clwb,f16c,fma,fsgsbase,movbe,pku,prefetchwt1,ptwrite,rdrand,rdseed,rtm,serialize,sha,sse2,sse4.1,sse4.2,ssse3,tsxldtrk,uintr,waitpkg"));
    String8 sha_disable_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=znver4"), S8("-mattr=-sha"), S8("source.c"),
    };
    CompilerDriverInvocation sha_disable_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sha_disable_command_line));
    BUSTER_TEST(arguments, sha_disable_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, !target_cpu_feature_has(sha_disable_invocation.target, TARGET_CPU_FEATURE_X86_SHA));
    String8 sha_dependency_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-mattr=+sha,-sse2"), S8("source.c"),
    };
    CompilerDriverInvocation sha_dependency_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sha_dependency_command_line));
    BUSTER_TEST(arguments, sha_dependency_invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, sha_dependency_invocation.diagnostic, S8("invalid target feature combination: sha"));
    String8 sha512_sm3_sm4_ordered_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"),
        S8("-mattr=+avx,+avx2,+sha512,+sm3,+sm4,-sm4,+sm4,-sha512,+sha512,-sm3,+sm3"), S8("source.c"),
    };
    CompilerDriverInvocation sha512_sm3_sm4_ordered = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sha512_sm3_sm4_ordered_command_line));
    BUSTER_TEST(arguments, sha512_sm3_sm4_ordered.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_has(sha512_sm3_sm4_ordered.target, TARGET_CPU_FEATURE_X86_SHA512));
    BUSTER_TEST(arguments, target_cpu_feature_has(sha512_sm3_sm4_ordered.target, TARGET_CPU_FEATURE_X86_SM3));
    BUSTER_TEST(arguments, target_cpu_feature_has(sha512_sm3_sm4_ordered.target, TARGET_CPU_FEATURE_X86_SM4));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, sha512_sm3_sm4_ordered.target),
                       S8("avx,avx2,sha512,sm3,sm4,sse2"));
    String8 invalid_sha512_without_avx2_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-mattr=+avx,+sha512"), S8("source.c"),
    };
    CompilerDriverInvocation invalid_sha512_without_avx2 = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_sha512_without_avx2_command_line));
    BUSTER_TEST(arguments, invalid_sha512_without_avx2.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_sha512_without_avx2.diagnostic,
                       S8("invalid target feature combination: avx,sha512,sse2"));
    String8 invalid_sm4_without_avx2_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-mattr=+avx,+sm4"), S8("source.c"),
    };
    CompilerDriverInvocation invalid_sm4_without_avx2 = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_sm4_without_avx2_command_line));
    BUSTER_TEST(arguments, invalid_sm4_without_avx2.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_sm4_without_avx2.diagnostic,
                       S8("invalid target feature combination: avx,sm4,sse2"));
    String8 invalid_sm3_without_avx_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-mattr=+sm3"), S8("source.c"),
    };
    CompilerDriverInvocation invalid_sm3_without_avx = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_sm3_without_avx_command_line));
    BUSTER_TEST(arguments, invalid_sm3_without_avx.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_sm3_without_avx.diagnostic,
                       S8("invalid target feature combination: sm3,sse2"));
    String8 valid_sm3_with_avx_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-mattr=+avx,+sm3"), S8("source.c"),
    };
    CompilerDriverInvocation valid_sm3_with_avx = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(valid_sm3_with_avx_command_line));
    BUSTER_TEST(arguments, valid_sm3_with_avx.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_has(valid_sm3_with_avx.target, TARGET_CPU_FEATURE_X86_SM3));
    String8 valid_sha512_with_avx2_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-mattr=+avx,+avx2,+sha512"), S8("source.c"),
    };
    CompilerDriverInvocation valid_sha512_with_avx2 = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(valid_sha512_with_avx2_command_line));
    BUSTER_TEST(arguments, valid_sha512_with_avx2.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_has(valid_sha512_with_avx2.target, TARGET_CPU_FEATURE_X86_SHA512));
    String8 valid_sm4_with_avx2_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-mattr=+avx,+avx2,+sm4"), S8("source.c"),
    };
    CompilerDriverInvocation valid_sm4_with_avx2 = compiler_driver_parse_arguments(
        arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(valid_sm4_with_avx2_command_line));
    BUSTER_TEST(arguments, valid_sm4_with_avx2.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_has(valid_sm4_with_avx2.target, TARGET_CPU_FEATURE_X86_SM4));
    String8 ace_feature_command_line[] = {
        S8("--target=x86_64-linux"),
        S8("-march=haswell"),
        S8("-mattr=+ace-1"),
        S8("source.c"),
    };
    CompilerDriverInvocation ace_feature_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ace_feature_command_line));
    BUSTER_TEST(arguments, ace_feature_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_has(ace_feature_invocation.target, TARGET_CPU_FEATURE_X86_ACE_1));
    String8 invalid_ace_spelling_command_line[] = {
        S8("--target=x86_64-linux"),
        S8("-march=haswell"),
        S8("-mattr=+ACE_1"),
        S8("source.c"),
    };
    CompilerDriverInvocation invalid_ace_spelling =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_ace_spelling_command_line));
    BUSTER_TEST(arguments, invalid_ace_spelling.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_ace_spelling.diagnostic, S8("unsupported target feature: ACE_1"));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, feature_invocation.target),
                       S8("avx,avx2,avx512bw,avx512f,avx512vl,bmi1,bmi2,cldemote,cx16,f16c,fma,fsgsbase,ibt,invpcid,lzcnt,movbe,movrs,pclmul,popcnt,prefetchi,rdrand,shstk,sse2,sse3,sse4.1,sse4.2,ssse3,svm,vmx,xsave"));
    String8 invalid_avx512_dependency_command_line[] = {
        S8("--target=x86_64-linux"),
        S8("-mattr=+avx512f,+avx512vl,-avx2,+avx512bw"),
        S8("-march=haswell"),
        S8("source.c"),
    };
    CompilerDriverInvocation invalid_avx512_dependency =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_avx512_dependency_command_line));
    BUSTER_TEST(arguments, invalid_avx512_dependency.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    String8 ordered_feature_command_line[] = {S8("--target=x86_64-linux"), S8("-march=haswell"),
                                              S8("-mattr=+avx512f,+ibt,+shstk,+vmx,+svm"), S8("-mattr=-avx512f,-ibt,-shstk,-vmx"), S8("source.c")};
    CompilerDriverInvocation ordered_features =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ordered_feature_command_line));
    BUSTER_TEST(arguments, ordered_features.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, !target_cpu_feature_has(ordered_features.target, TARGET_CPU_FEATURE_X86_AVX512F));
    BUSTER_TEST(arguments, !target_cpu_feature_has(ordered_features.target, TARGET_CPU_FEATURE_X86_IBT));
    BUSTER_TEST(arguments, !target_cpu_feature_has(ordered_features.target, TARGET_CPU_FEATURE_X86_SHSTK));
    BUSTER_TEST(arguments, !target_cpu_feature_has(ordered_features.target, TARGET_CPU_FEATURE_X86_VMX));
    BUSTER_TEST(arguments, target_cpu_feature_has(ordered_features.target, TARGET_CPU_FEATURE_X86_SVM));
    String8 invalid_feature_command_line[] = {S8("--target=x86_64-linux"), S8("-mattr=+future-isa"), S8("source.c")};
    CompilerDriverInvocation invalid_feature =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_feature_command_line));
    BUSTER_TEST(arguments, invalid_feature.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_feature.diagnostic, S8("unsupported target feature: future-isa"));
    String8 incompatible_ibt_command_line[] = {S8("--target=aarch64-linux"), S8("-mattr=+ibt"), S8("source.c")};
    CompilerDriverInvocation incompatible_ibt =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(incompatible_ibt_command_line));
    BUSTER_TEST(arguments, incompatible_ibt.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, incompatible_ibt.diagnostic, S8("unsupported target feature: ibt"));
    String8 incompatible_shstk_command_line[] = {S8("--target=aarch64-linux"), S8("-mattr=+shstk"), S8("source.c")};
    CompilerDriverInvocation incompatible_shstk =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(incompatible_shstk_command_line));
    BUSTER_TEST(arguments, incompatible_shstk.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, incompatible_shstk.diagnostic, S8("unsupported target feature: shstk"));
    String8 invalid_feature_syntax_command_line[] = {S8("--target=x86_64-linux"), S8("-mattr=avx"), S8("source.c")};
    CompilerDriverInvocation invalid_feature_syntax =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_feature_syntax_command_line));
    BUSTER_TEST(arguments, invalid_feature_syntax.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_feature_syntax.diagnostic, S8("invalid target feature override: avx"));
    String8 invalid_feature_combination_command_line[] = {S8("--target=x86_64-linux"), S8("-mattr=+avx2"), S8("source.c")};
    CompilerDriverInvocation invalid_feature_combination =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_feature_combination_command_line));
    BUSTER_TEST(arguments, invalid_feature_combination.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_feature_combination.diagnostic, S8("invalid target feature combination: avx2,sse2"));
    String8 aarch64_feature_command_line[] = {S8("--target=aarch64-linux"), S8("-mattr"), S8("-neon"), S8("source.c")};
    CompilerDriverInvocation aarch64_features =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(aarch64_feature_command_line));
    BUSTER_TEST(arguments, aarch64_features.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, aarch64_features.target), S8("fp-armv8"));
    String8 invalid_aarch64_feature_command_line[] = {S8("--target=aarch64-linux"), S8("-mattr=-fp-armv8"), S8("source.c")};
    CompilerDriverInvocation invalid_aarch64_features =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_aarch64_feature_command_line));
    BUSTER_TEST(arguments, invalid_aarch64_features.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_aarch64_features.diagnostic, S8("invalid target feature combination: neon"));
    String8 apple_m1_command_line[] = {S8("--target=aarch64-macos"), S8("-mcpu=apple-m1"), S8("source.c")};
    CompilerDriverInvocation apple_m1 =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(apple_m1_command_line));
    BUSTER_TEST(arguments, apple_m1.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_has(apple_m1.target, TARGET_CPU_FEATURE_AARCH64_LOR));
    BUSTER_TEST(arguments, target_cpu_feature_has(apple_m1.target, TARGET_CPU_FEATURE_AARCH64_TRACEV8_4));
    String8 incompatible_assembly_syntax_command_line[] = {S8("--target=aarch64-linux"), S8("-masm=intel"), S8("source.c")};
    CompilerDriverInvocation incompatible_assembly_syntax =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(incompatible_assembly_syntax_command_line));
    BUSTER_TEST(arguments, incompatible_assembly_syntax.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, incompatible_assembly_syntax.diagnostic, S8("assembly syntax is incompatible with target: intel"));
    String8 no_debug_command_line[] = {S8("-g0"), S8("-c"), S8("source.c")};
    CompilerDriverInvocation no_debug_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(no_debug_command_line));
    BUSTER_TEST(arguments, no_debug_invocation.error == COMPILER_DRIVER_ERROR_NONE && !no_debug_invocation.debug_info);
    String8 unsupported_debug_command_line[] = {S8("-g1"), S8("source.c")};
    CompilerDriverInvocation unsupported_debug =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unsupported_debug_command_line));
    BUSTER_TEST(arguments, unsupported_debug.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, unsupported_debug.diagnostic, S8("unsupported debug option: -g1"));
    String8 incompatible_cpu_command_line[] = {
        S8("--target=x86_64-linux"),
        S8("-mcpu=apple-m4"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation incompatible_cpu =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(incompatible_cpu_command_line));
    BUSTER_TEST(arguments, incompatible_cpu.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, incompatible_cpu.diagnostic, S8("CPU model is incompatible with target: apple-m4"));
    String8 unknown_cpu_command_line[] = {
        S8("-march=future-fast"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation unknown_cpu = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unknown_cpu_command_line));
    BUSTER_TEST(arguments, unknown_cpu.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, unknown_cpu.diagnostic, S8("unsupported CPU model: future-fast"));
    String8 isolated_command_line[] = {
        S8("-isysroot"), S8("/isolated-sdk"), S8("-nostdinc"), S8("-isystem"), S8("explicit-system"), S8("source.c"),
    };
    CompilerDriverInvocation isolated_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(isolated_command_line));
    BUSTER_TEST(arguments, isolated_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, isolated_invocation.no_standard_includes);
    BUSTER_STRING_TEST(arguments, isolated_invocation.sysroot, S8("/isolated-sdk"));
    BUSTER_TEST(arguments, isolated_invocation.system_include_path_count == 1);
    BUSTER_STRING_TEST(arguments, isolated_invocation.system_include_paths[0], S8("explicit-system"));
    String8 invalid_command_line[] = {
        S8("-target"),
        S8("riscv64-unknown-linux-gnu"),
        S8("source.c"),
    };
    CompilerDriverInvocation invalid_invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_command_line));
    BUSTER_TEST(arguments, invalid_invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_invocation.diagnostic, S8("unsupported target: riscv64-unknown-linux-gnu"));
    // A trailing component in a target string used to be dropped, so a CPU
    // model asked for there left baseline code generation and a surprising
    // CODEGEN_ERROR_UNSUPPORTED_ABI on the first wide vector argument.
    String8 target_cpu_model_command_line[] = {
        S8("-target"),
        S8("x86_64-unknown-linux-gnu-znver4"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation target_cpu_model =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(target_cpu_model_command_line));
    BUSTER_TEST(arguments, target_cpu_model.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, target_cpu_model.diagnostic, S8("CPU model must be selected with -march=: znver4"));
    String8 target_excess_component_command_line[] = {
        S8("--target=x86_64-unknown-linux-gnu-notacpu"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation target_excess_component =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(target_excess_component_command_line));
    BUSTER_TEST(arguments, target_excess_component.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, target_excess_component.diagnostic, S8("unsupported target component: notacpu"));
    // The spelling that works, and the wide vector registers it unlocks.
    String8 target_march_command_line[] = {
        S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-march=znver4"), S8("-c"), S8("source.c"),
    };
    CompilerDriverInvocation target_march = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(target_march_command_line));
    BUSTER_TEST(arguments, target_march.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_march.target.cpu_model == CPU_MODEL_AMD_ZEN_4);
    BUSTER_TEST(arguments, target_vector_register_size(target_march.target) == 64);
    String8 preprocess_command_line[] = {
        S8("-E"),
        S8("-DADDED=5"),
        S8("tests/basic_c_driver.c"),
    };
    CompilerDriverInvocation preprocess_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(preprocess_command_line));
    CompilerDriverResult preprocess = compiler_driver_execute_invocation(arguments->arena, preprocess_invocation);
    BUSTER_TEST(arguments, preprocess.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(preprocess.output, S8("int answer = 37 ;")) != BUSTER_STRING_NO_MATCH);
    String8 warning_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_preprocessor_warning.c"),
    };
    CompilerDriverResult warning = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_command_line)));
    BUSTER_TEST(arguments, warning.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, warning.tokenizer_error_count == 0);
    BUSTER_TEST(arguments, warning.tokenizer_warning_count == 1);
    BUSTER_TEST(arguments, string_first_sequence(warning.warning, S8("warning: PREPROCESSOR_WARNING_TEXT")) != BUSTER_STRING_NO_MATCH);
    String8 warning_cross_target_command_line[] = {
        S8("-E"),
        S8("-target"),
        S8("x86_64-pc-windows-msvc"),
        S8("tests/basic_c_preprocessor_warning.c"),
    };
    CompilerDriverResult warning_cross_target = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_cross_target_command_line)));
    BUSTER_TEST(arguments, warning_cross_target.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, warning_cross_target.tokenizer_error_count == 0);
    BUSTER_TEST(arguments, warning_cross_target.tokenizer_warning_count == 1);
    BUSTER_TEST(arguments, string_first_sequence(warning_cross_target.output, S8("int main ( void )")) != BUSTER_STRING_NO_MATCH);
    String8 error_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_preprocessor_error.c"),
    };
    CompilerDriverResult preprocessor_error = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(error_command_line)));
    BUSTER_TEST(arguments, preprocessor_error.error == COMPILER_DRIVER_ERROR_TOKENIZE);
    BUSTER_TEST(arguments, preprocessor_error.tokenizer_error_count == 1);
    BUSTER_TEST(arguments, string_first_sequence(preprocessor_error.diagnostic, S8("expanded driver error")) != BUSTER_STRING_NO_MATCH);
    String8 warning_multi_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_preprocessor_warning.c"),
        S8("tests/basic_c_preprocessor_warning_second.c"),
    };
    CompilerDriverResult warning_multi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_multi_command_line)));
    BUSTER_TEST(arguments, warning_multi.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, warning_multi.tokenizer_error_count == 0);
    BUSTER_TEST(arguments, warning_multi.tokenizer_warning_count == 2);
    u64 first_warning = string_first_sequence(warning_multi.warning, S8("warning: PREPROCESSOR_WARNING_TEXT"));
    u64 second_warning = string_first_sequence(warning_multi.warning, S8("warning: second driver warning"));
    BUSTER_TEST(arguments, first_warning != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, second_warning != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, first_warning < second_warning);
    String8 syntax_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_driver.c"),
    };
    CompilerDriverResult syntax = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(syntax_command_line)));
    BUSTER_TEST(arguments, syntax.error == COMPILER_DRIVER_ERROR_NONE);
    String8 assembly_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("x86_64-unknown-linux-gnu"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_command_line)));
    BUSTER_TEST(arguments, assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, assembly.output.length != 0);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.intel_syntax noprefix\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.text\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\tpush rbp\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\tmov rbp, rsp\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.byte ")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_c23_command_line[] = {
        S8("-S"), S8("-std=c23"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_constexpr.c"),
    };
    CompilerDriverResult assembly_c23 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_c23_command_line)));
    BUSTER_TEST(arguments, assembly_c23.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_c23.output, S8("[rip + \"offset\"]")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_tls_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult assembly_tls = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_tls_command_line)));
    BUSTER_TEST(arguments, assembly_tls.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_tls.output, S8("@TPOFF")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_aarch64_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("aarch64-unknown-linux-gnu"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_aarch64_command_line)));
    BUSTER_TEST(arguments, assembly_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_aarch64.output, S8("\tstp x29, x30")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly_aarch64.output, S8("\tret\n")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_multi_command_line[] = {
        S8("-S"),
        S8("tests/basic_c_multi_main.c"),
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult assembly_multi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_multi_command_line)));
    BUSTER_TEST(arguments, assembly_multi.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_multi.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly_multi.output, S8("add_values:\n")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_output_path = buster_test_temporary_path(arguments->arena, S8("buster-c-assembly"), S8(".s"));
    String8 assembly_file_command_line[] = {
        S8("-S"),
        S8("-o"),
        assembly_output_path,
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly_file = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_file_command_line)));
    BUSTER_TEST(arguments, assembly_file.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice assembly_file_bytes = file_read(arguments->arena, assembly_output_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, assembly_file_bytes.length == assembly_file.output.length);
    if (assembly_file_bytes.length == assembly_file.output.length)
    {
        BUSTER_TEST(arguments, memcmp(assembly_file_bytes.pointer, assembly_file.output.pointer, assembly_file.output.length) == 0);
    }
    String8 retired_buster_path = buster_test_temporary_path(arguments->arena, S8("retired-buster-input"), S8(".bbb"));
    BUSTER_TEST(arguments, file_write(retired_buster_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("retired language input\n"))));
    String8 retired_buster_command_line[] = {
        S8("-fsyntax-only"),
        retired_buster_path,
    };
    CompilerDriverResult retired_buster = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(retired_buster_command_line)));
    BUSTER_TEST(arguments, retired_buster.error == COMPILER_DRIVER_ERROR_INVALID_INPUT);
    BUSTER_TEST(arguments, string_first_sequence(retired_buster.diagnostic, S8("unsupported C input")) != BUSTER_STRING_NO_MATCH);
    String8 retired_language_command_line[] = {
        S8("-x"),
        S8("buster"),
        retired_buster_path,
    };
    CompilerDriverInvocation retired_language =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(retired_language_command_line));
    BUSTER_TEST(arguments, retired_language.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, retired_language.diagnostic, S8("unsupported language: buster"));
    String8 retired_module_root_command_line[] = {
        S8("--module-root"),
        S8("tests/modules"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverInvocation retired_module_root =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(retired_module_root_command_line));
    BUSTER_TEST(arguments, retired_module_root.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, retired_module_root.diagnostic, S8("unsupported option: --module-root"));
    String8 conflicting_actions[] = {
        S8("-c"),
        S8("-S"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverInvocation conflicting =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(conflicting_actions));
    BUSTER_TEST(arguments, conflicting.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    String8 dialect_flags[] = {
        S8("-std=gnu11"), S8("-std=gnu17"), S8("-std=gnu23"), S8("-std=c11"), S8("-std=c17"), S8("-std=c23"),
    };
    String8 dialect_versions[] = {
        S8("-DEXPECTED_STDC_VERSION=201112L"), S8("-DEXPECTED_STDC_VERSION=201710L"), S8("-DEXPECTED_STDC_VERSION=202311L"),
        S8("-DEXPECTED_STDC_VERSION=201112L"), S8("-DEXPECTED_STDC_VERSION=201710L"), S8("-DEXPECTED_STDC_VERSION=202311L"),
    };
    for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialect_flags); dialect_index += 1)
    {
        TemporalArena dialect_temporary = arena_begin_temporal(arguments->arena);
        Arena* dialect_arena = dialect_temporary.arena;
#if BUSTER_IOS
        String8 dialect_command_line[] = {
            S8("-fsyntax-only"), dialect_flags[dialect_index], dialect_versions[dialect_index],
            dialect_index < 3 ? S8("-DEXPECTED_GNU=1") : S8("-DEXPECTED_GNU=0"), S8("tests/basic_c_dialect.c"),
        };
#else
        String8 dialect_object_path =
            buster_test_temporary_path(dialect_arena, S8("buster-c-dialect"), string_format(dialect_arena, S8("-{u32}.o"), dialect_index));
        String8 dialect_command_line[] = {
            S8("-c"), dialect_flags[dialect_index], dialect_versions[dialect_index], dialect_index < 3 ? S8("-DEXPECTED_GNU=1") : S8("-DEXPECTED_GNU=0"),
            S8("-o"), dialect_object_path,          S8("tests/basic_c_dialect.c"),
        };
#endif
        CompilerDriverResult dialect_result = compiler_driver_execute_invocation(
            dialect_arena, compiler_driver_parse_arguments(dialect_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(dialect_command_line)));
        BUSTER_TEST(arguments, dialect_result.error == COMPILER_DRIVER_ERROR_NONE);
        scratch_end(dialect_temporary);
    }
#if !BUSTER_ANDROID && !BUSTER_IOS
    Arena* c_object_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_object_temporary = scratch_begin(c_object_conflicts, BUSTER_ARRAY_LENGTH(c_object_conflicts));
    Arena* c_object_arena = c_object_temporary.arena;
    String8 c_object_path = buster_test_temporary_path(c_object_arena, S8("buster-c-driver"), S8(".o"));
    String8 c_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_object_path,
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult c_object = compiler_driver_execute_invocation(
        c_object_arena, compiler_driver_parse_arguments(c_object_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_object_command_line)));
    BUSTER_TEST(arguments, c_object.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_object.codegen_statistics.function_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.instruction_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.value_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.stack_value_bytes > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.stack_frame_bytes >= c_object.codegen_statistics.maximum_stack_frame_bytes);
    BUSTER_TEST(arguments, c_object.codegen_statistics.code_bytes > 0);
    FileMapRead c_object_map = file_map_read(c_object_arena, c_object_path, (FileReadOptions){0});
    ByteSlice c_object_bytes = c_object_map.bytes;
    BUSTER_TEST(arguments, c_object_bytes.length != 0);
    file_map_unmap(c_object_map);
    String8 c_object_targets[] = {
        S8("x86_64-unknown-linux-gnu"),  S8("x86_64-pc-windows-msvc"),  S8("x86_64-apple-macos"),  S8("x86_64-linux-android"),  S8("x86_64-apple-ios"),
        S8("aarch64-unknown-linux-gnu"), S8("aarch64-pc-windows-msvc"), S8("aarch64-apple-macos"), S8("aarch64-linux-android"), S8("aarch64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_object_targets); target_index += 1)
    {
        TemporalArena cross_temp = arena_begin_temporal(c_object_arena);
        String8 cross_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-object"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 cross_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), cross_object_path, S8("tests/basic_c_compile.c"),
        };
        CompilerDriverResult cross = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(cross_command_line)));
        BUSTER_TEST(arguments, cross.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead cross_map = file_map_read(cross_temp.arena, cross_object_path, (FileReadOptions){0});
        ByteSlice cross_bytes = cross_map.bytes;
        BUSTER_TEST(arguments, cross_bytes.length != 0);
        // Debug sections and their relocations must survive a round trip
        // through every object format, or linking a previously compiled
        // object back in fails.
        BUSTER_TEST(arguments, cross.has_object);
        if (cross.has_object)
        {
            ObjectFile cross_round_trip = object_read(cross_temp.arena, cross_bytes, cross.object.target);
            BUSTER_TEST(arguments, cross_round_trip.error == OBJECT_ERROR_NONE);
            if (cross_round_trip.error == OBJECT_ERROR_NONE)
            {
                bool debug_found = false;
                for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
                {
                    if (!object_section_kind_is_debug((ObjectSectionKind)kind))
                    {
                        continue;
                    }
                    BUSTER_TEST(arguments, cross_round_trip.sections[kind].data.length == cross.object.sections[kind].data.length);
                    debug_found = debug_found || cross_round_trip.sections[kind].data.length != 0;
                }
                BUSTER_TEST(arguments, debug_found);
            }
        }
        file_map_unmap(cross_map);
        String8 artifact_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-artifacts"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 artifact_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), artifact_object_path, S8("tests/basic_c_frontend_artifacts.c"),
        };
        CompilerDriverResult artifacts = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(artifact_command_line)));
        BUSTER_TEST(arguments, artifacts.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, artifacts.has_object);
        if (artifacts.has_object)
        {
            bool function_relocation = false;
            bool data_relocation = false;
            for (u32 relocation_index = 0; relocation_index < artifacts.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocation relocation = artifacts.object.relocations[relocation_index];
                if (relocation.symbol >= artifacts.object.symbol_count)
                {
                    continue;
                }
                ObjectSymbol symbol = artifacts.object.symbols[relocation.symbol];
                function_relocation |= symbol.kind == OBJECT_SYMBOL_FUNCTION;
                data_relocation |= symbol.kind == OBJECT_SYMBOL_DATA;
            }
            BUSTER_TEST(arguments, function_relocation);
            BUSTER_TEST(arguments, data_relocation);
            FileMapRead artifact_map = file_map_read(cross_temp.arena, artifact_object_path, (FileReadOptions){0});
            BUSTER_TEST(arguments, artifact_map.bytes.length != 0);
            file_map_unmap(artifact_map);
        }
        String8 fixed_enum_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-fixed-enum"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 fixed_enum_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), fixed_enum_object_path, S8("tests/basic_c_fixed_enum.c"),
        };
        CompilerDriverResult fixed_enum = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixed_enum_command_line)));
        BUSTER_TEST(arguments, fixed_enum.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead fixed_enum_map = file_map_read(cross_temp.arena, fixed_enum_object_path, (FileReadOptions){0});
        ByteSlice fixed_enum_bytes = fixed_enum_map.bytes;
        BUSTER_TEST(arguments, fixed_enum_bytes.length != 0);
        file_map_unmap(fixed_enum_map);
        String8 string_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-string"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 string_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), string_object_path, S8("tests/basic_c_string_concat.c"),
        };
        CompilerDriverResult string_literals = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(string_command_line)));
        BUSTER_TEST(arguments, string_literals.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead string_map = file_map_read(cross_temp.arena, string_object_path, (FileReadOptions){0});
        ByteSlice string_bytes = string_map.bytes;
        BUSTER_TEST(arguments, string_bytes.length != 0);
        file_map_unmap(string_map);
        String8 nullptr_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-nullptr"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 nullptr_command_line[] = {
            S8("-c"), S8("-std=c23"), S8("-target"), c_object_targets[target_index], S8("-o"), nullptr_object_path, S8("tests/basic_c_nullptr.c"),
        };
        CompilerDriverResult nullptr_result = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_command_line)));
        BUSTER_TEST(arguments, nullptr_result.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead nullptr_map = file_map_read(cross_temp.arena, nullptr_object_path, (FileReadOptions){0});
        ByteSlice nullptr_bytes = nullptr_map.bytes;
        BUSTER_TEST(arguments, nullptr_bytes.length != 0);
        file_map_unmap(nullptr_map);
        String8 constexpr_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-constexpr"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 constexpr_command_line[] = {
            S8("-c"), S8("-std=c23"), S8("-target"), c_object_targets[target_index], S8("-o"), constexpr_object_path, S8("tests/basic_c_constexpr.c"),
        };
        CompilerDriverResult constexpr_result = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_command_line)));
        BUSTER_TEST(arguments, constexpr_result.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead constexpr_map = file_map_read(cross_temp.arena, constexpr_object_path, (FileReadOptions){0});
        ByteSlice constexpr_bytes = constexpr_map.bytes;
        BUSTER_TEST(arguments, constexpr_bytes.length != 0);
        file_map_unmap(constexpr_map);
        String8 atomic_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-atomic"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 atomic_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), atomic_object_path, S8("tests/basic_c_atomic.c"),
        };
        CompilerDriverResult atomic = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_command_line)));
        BUSTER_TEST(arguments, atomic.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead atomic_map = file_map_read(cross_temp.arena, atomic_object_path, (FileReadOptions){0});
        ByteSlice atomic_bytes = atomic_map.bytes;
        BUSTER_TEST(arguments, atomic_bytes.length != 0);
        file_map_unmap(atomic_map);
        String8 stdatomic_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-stdatomic"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 stdatomic_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), stdatomic_object_path, S8("tests/basic_c_stdatomic.c"),
        };
        CompilerDriverResult stdatomic = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stdatomic_command_line)));
        BUSTER_TEST(arguments, stdatomic.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead stdatomic_map = file_map_read(cross_temp.arena, stdatomic_object_path, (FileReadOptions){0});
        ByteSlice stdatomic_bytes = stdatomic_map.bytes;
        BUSTER_TEST(arguments, stdatomic_bytes.length != 0);
        file_map_unmap(stdatomic_map);
        // Aggregates passed and returned by value that are far too wide for a
        // register. Every convention moves one an eightbyte at a time, so a
        // function full of them is the unit that outgrows a code buffer
        // reserved per instruction rather than per byte moved.
        String8 wide_argument_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-wide-argument"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 wide_argument_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), wide_argument_object_path, S8("tests/basic_c_wide_argument.c"),
        };
        CompilerDriverResult wide_argument = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wide_argument_command_line)));
        BUSTER_TEST(arguments, wide_argument.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead wide_argument_map = file_map_read(cross_temp.arena, wide_argument_object_path, (FileReadOptions){0});
        ByteSlice wide_argument_bytes = wide_argument_map.bytes;
        BUSTER_TEST(arguments, wide_argument_bytes.length != 0);
        file_map_unmap(wide_argument_map);
        // The same shapes as a 512-bit vector, which the conventions answer
        // differently: Win64 and aarch64 pass one by reference, and x86-64
        // SystemV and Darwin in a vector register -- or, on a model with no
        // register that wide, split across the ones it has. Every row here
        // builds at its target's default model, so the x86-64 ones are the
        // split, and the host run below is what covers the whole one.
        String8 wide_vector_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-wide-vector-argument"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 wide_vector_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), wide_vector_object_path, S8("tests/basic_c_wide_vector_argument.c"),
        };
        CompilerDriverResult wide_vector = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wide_vector_command_line)));
        BUSTER_TEST(arguments, wide_vector.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead wide_vector_map = file_map_read(cross_temp.arena, wide_vector_object_path, (FileReadOptions){0});
        ByteSlice wide_vector_bytes = wide_vector_map.bytes;
        BUSTER_TEST(arguments, wide_vector_bytes.length != 0);
        file_map_unmap(wide_vector_map);
        scratch_end(cross_temp);
    }
    {
        TemporalArena large_frame_temporary = arena_begin_temporal(c_object_arena);
        String8 large_frame_object_path = buster_test_temporary_path(large_frame_temporary.arena, S8("buster-c-large-frame"), S8(".o"));
        String8 large_frame_command_line[] = {
            S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), large_frame_object_path, S8("tests/basic_c_large_frame.c"),
        };
        CompilerDriverResult large_frame = compiler_driver_execute_invocation(
            large_frame_temporary.arena,
            compiler_driver_parse_arguments(large_frame_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(large_frame_command_line)));
        BUSTER_TEST(arguments, large_frame.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead large_frame_map = file_map_read(large_frame_temporary.arena, large_frame_object_path, (FileReadOptions){0});
        ByteSlice large_frame_bytes = large_frame_map.bytes;
        BUSTER_TEST(arguments, large_frame_bytes.length != 0);
        file_map_unmap(large_frame_map);
        scratch_end(large_frame_temporary);
    }
    {
        TemporalArena ucontext_temporary = arena_begin_temporal(c_object_arena);
        String8 ucontext_object_path = buster_test_temporary_path(ucontext_temporary.arena, S8("buster-c-ucontext"), S8(".o"));
        String8 ucontext_command_line[] = {
            S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), ucontext_object_path, S8("tests/basic_c_ucontext.c"),
        };
        CompilerDriverResult ucontext = compiler_driver_execute_invocation(
            ucontext_temporary.arena, compiler_driver_parse_arguments(ucontext_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ucontext_command_line)));
        BUSTER_TEST(arguments, ucontext.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead ucontext_map = file_map_read(ucontext_temporary.arena, ucontext_object_path, (FileReadOptions){0});
        ByteSlice ucontext_bytes = ucontext_map.bytes;
        BUSTER_TEST(arguments, ucontext_bytes.length != 0);
        file_map_unmap(ucontext_map);
        scratch_end(ucontext_temporary);
    }
    scratch_end(c_object_temporary);
#endif
#if BUSTER_LINK_LIBC && !BUSTER_ANDROID && !BUSTER_IOS && !BUSTER_SANITIZE
    String8 c_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-driver"),
#if BUSTER_WINDOWS
                                                           S8(".exe"));
#else
                                                           S8(""));
#endif
    String8 c_link_command_line[] = {
        S8("-o"),
        c_executable_path,
        S8("tests/basic_c_operations.c"),
    };
    CompilerDriverResult c_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_link_command_line)));
    BUSTER_TEST(arguments, c_link.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_link.has_object);
    if (c_link.has_object)
    {
        BUSTER_TEST(arguments, c_link.object.sections[OBJECT_SECTION_ZERO].data.length == 0);
        BUSTER_TEST(arguments, c_link.object.sections[OBJECT_SECTION_ZERO].virtual_size >= BUSTER_MB(1));
        BUSTER_TEST(arguments, c_link.object.sections[OBJECT_SECTION_READ_ONLY_DATA].data.length >= 64);
        ByteSlice c_image = file_read(arguments->arena, c_executable_path, (FileReadOptions){0});
        BUSTER_TEST(arguments, c_image.length != 0 && c_image.length < BUSTER_MB(1));
    }
    if (c_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_run_arguments[] = {
            c_executable_path,
        };
        ProcessSpawnResult c_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                      (ProcessSpawnOptions){
                                                          .use_process_environment = true,
                                                      });
        BUSTER_TEST(arguments, c_spawn.handle != 0);
        if (c_spawn.handle)
        {
            ProcessWaitResult c_wait = os_process_wait_sync(arguments->arena, c_spawn);
            BUSTER_TEST(arguments, c_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_static_local_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-static-local"),
#if BUSTER_WINDOWS
                                                                         S8(".exe"));
#else
                                                                         S8(""));
#endif
    String8 c_static_local_command_line[] = {
        S8("-o"),
        c_static_local_executable_path,
        S8("tests/basic_c_static_local.c"),
    };
    CompilerDriverResult c_static_local = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_local_command_line)));
    BUSTER_TEST(arguments, c_static_local.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_static_local.has_object);
    if (c_static_local.has_object)
    {
        BUSTER_TEST(arguments, c_static_local.object.sections[OBJECT_SECTION_READ_ONLY_DATA].data.length != 0);
        bool mutable_zero_found = false;
        bool tls_zero_found = false;
        bool const_zero_found = false;
        bool empty_string_found = false;
        bool tls_empty_string_found = false;
        bool const_empty_string_found = false;
        bool mutable_scalar_zero_found = false;
        bool tls_scalar_zero_found = false;
        bool const_scalar_zero_found = false;
        bool mutable_pointer_zero_found = false;
        bool tls_pointer_zero_found = false;
        bool const_pointer_zero_found = false;
        bool nonzero_found = false;
        for (u32 symbol_index = 0; symbol_index < c_static_local.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = c_static_local.object.symbols + symbol_index;
            if (symbol->kind != OBJECT_SYMBOL_DATA)
            {
                continue;
            }
            mutable_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.mutable_zero.")) &&
                                  symbol->section == OBJECT_SECTION_ZERO;
            tls_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.tls_zero.")) &&
                              symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO;
            const_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.const_zero.")) &&
                                symbol->section == OBJECT_SECTION_READ_ONLY_DATA;
            empty_string_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.empty_string.")) &&
                                  symbol->section == OBJECT_SECTION_ZERO;
            tls_empty_string_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.tls_empty_string.")) &&
                                      symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO;
            const_empty_string_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.const_empty_string.")) &&
                                        symbol->section == OBJECT_SECTION_READ_ONLY_DATA;
            mutable_scalar_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.mutable_scalar_zero.")) &&
                                         symbol->section == OBJECT_SECTION_ZERO;
            tls_scalar_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.tls_scalar_zero.")) &&
                                     symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO;
            const_scalar_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.const_scalar_zero.")) &&
                                       symbol->section == OBJECT_SECTION_READ_ONLY_DATA;
            mutable_pointer_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.mutable_pointer_zero.")) &&
                                          symbol->section == OBJECT_SECTION_ZERO;
            tls_pointer_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.tls_pointer_zero.")) &&
                                      symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO;
            const_pointer_zero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.const_pointer_zero.")) &&
                                        symbol->section == OBJECT_SECTION_READ_ONLY_DATA;
            nonzero_found |= string_starts_with_sequence(symbol->name, S8(".L.static_zero_aggregate_sum.nonzero.")) &&
                             symbol->section == OBJECT_SECTION_DATA;
        }
        BUSTER_TEST(arguments, mutable_zero_found);
        BUSTER_TEST(arguments, tls_zero_found);
        BUSTER_TEST(arguments, const_zero_found);
        BUSTER_TEST(arguments, empty_string_found);
        BUSTER_TEST(arguments, tls_empty_string_found);
        BUSTER_TEST(arguments, const_empty_string_found);
        BUSTER_TEST(arguments, mutable_scalar_zero_found);
        BUSTER_TEST(arguments, tls_scalar_zero_found);
        BUSTER_TEST(arguments, const_scalar_zero_found);
        BUSTER_TEST(arguments, mutable_pointer_zero_found);
        BUSTER_TEST(arguments, tls_pointer_zero_found);
        BUSTER_TEST(arguments, const_pointer_zero_found);
        BUSTER_TEST(arguments, nonzero_found);
    }
#if BUSTER_WINDOWS
    if (c_static_local.error == COMPILER_DRIVER_ERROR_NONE && c_static_local.has_object)
    {
        u32 tls_alignment = BUSTER_MAX(c_static_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_DATA].alignment,
                                       c_static_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment);
        BUSTER_TEST(arguments, compiler_driver_test_pe_tls_directory(c_static_local.native_link.executable, tls_alignment));
    }
#endif
    if (c_static_local.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_static_local_run_arguments[] = {
            c_static_local_executable_path,
        };
        ProcessSpawnResult c_static_local_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_local_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_static_local_spawn.handle != 0);
        if (c_static_local_spawn.handle)
        {
            ProcessWaitResult c_static_local_wait = os_process_wait_sync(arguments->arena, c_static_local_spawn);
            BUSTER_TEST(arguments, c_static_local_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#if BUSTER_WINDOWS
    String8 c_static_local_probe_names[] = {
        S8("initialized TLS"),
        S8("zero TLS"),
        S8("non-TLS static aggregates"),
    };
    for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(c_static_local_probe_names); probe_index += 1)
    {
        String8 probe_path = buster_test_temporary_path(arguments->arena, S8("buster-c-static-local-probe"),
                                                         string_format(arguments->arena, S8("-{u32}.exe"), probe_index + 1)
        );
        String8 probe_command_line[] = {
            S8("-g0"),
            string_format(arguments->arena, S8("-DBUSTER_C_STATIC_LOCAL_PROBE={u32}"), probe_index + 1),
            S8("-o"),
            probe_path,
            S8("tests/basic_c_static_local.c"),
        };
        CompilerDriverResult probe = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(probe_command_line)));
        BUSTER_TEST_RAW(arguments, probe.error == COMPILER_DRIVER_ERROR_NONE,
                        string_format(arguments->arena, S8("C static-local {S8} compile"), c_static_local_probe_names[probe_index]));
        if (probe.error == COMPILER_DRIVER_ERROR_NONE && probe_index < 2 && probe.has_object)
        {
            u32 tls_alignment = BUSTER_MAX(probe.object.sections[OBJECT_SECTION_THREAD_LOCAL_DATA].alignment,
                                           probe.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment);
            BUSTER_TEST_RAW(arguments, compiler_driver_test_pe_tls_directory(probe.native_link.executable, tls_alignment),
                            string_format(arguments->arena, S8("C static-local {S8} PE TLS metadata"), c_static_local_probe_names[probe_index]));
        }
        if (probe.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST_RAW(arguments, compiler_driver_test_process_success(arguments->arena, probe_path),
                            string_format(arguments->arena, S8("C static-local {S8} runtime"), c_static_local_probe_names[probe_index]));
        }
    }
#endif
    String8 c_static_aggregate_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-static-aggregate-member"),
#if BUSTER_WINDOWS
                                                                              S8(".exe"));
#else
                                                                              S8(""));
#endif
    String8 c_static_aggregate_command_line[] = {
        S8("-o"),
        c_static_aggregate_executable_path,
        S8("tests/basic_c_static_aggregate_member.c"),
    };
    CompilerDriverResult c_static_aggregate = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_aggregate_command_line)));
    BUSTER_TEST(arguments, c_static_aggregate.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_static_aggregate.has_object);
    BUSTER_TEST(arguments, c_static_aggregate.codegen_statistics.maximum_stack_frame_bytes < BUSTER_KB(64));
    if (c_static_aggregate.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_static_aggregate_run_arguments[] = {
            c_static_aggregate_executable_path,
        };
        ProcessSpawnResult c_static_aggregate_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_aggregate_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_static_aggregate_spawn.handle != 0);
        if (c_static_aggregate_spawn.handle)
        {
            ProcessWaitResult c_static_aggregate_wait = os_process_wait_sync(arguments->arena, c_static_aggregate_spawn);
            BUSTER_TEST(arguments, c_static_aggregate_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_auto_type_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-auto-type"),
#if BUSTER_WINDOWS
                                                                      S8(".exe"));
#else
                                                                      S8(""));
#endif
    String8 c_auto_type_command_line[] = {
        S8("-std=gnu23"),
        S8("-o"),
        c_auto_type_executable_path,
        S8("tests/basic_c_auto_type.c"),
    };
    CompilerDriverResult c_auto_type = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_auto_type_command_line)));
    BUSTER_TEST(arguments, c_auto_type.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_auto_type.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_auto_type_run_arguments[] = {
            c_auto_type_executable_path,
        };
        ProcessSpawnResult c_auto_type_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_auto_type_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_auto_type_spawn.handle != 0);
        if (c_auto_type_spawn.handle)
        {
            ProcessWaitResult c_auto_type_wait = os_process_wait_sync(arguments->arena, c_auto_type_spawn);
            BUSTER_TEST(arguments, c_auto_type_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_cleanup_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-cleanup"),
#if BUSTER_WINDOWS
                                                                    S8(".exe"));
#else
                                                                    S8(""));
#endif
    String8 c_cleanup_command_line[] = {
        S8("-std=gnu23"),
        S8("-o"),
        c_cleanup_executable_path,
        S8("tests/basic_c_cleanup.c"),
    };
    CompilerDriverResult c_cleanup = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_cleanup_command_line)));
    BUSTER_TEST(arguments, c_cleanup.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_cleanup.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_cleanup_run_arguments[] = {
            c_cleanup_executable_path,
        };
        ProcessSpawnResult c_cleanup_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_cleanup_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                               (ProcessSpawnOptions){
                                                                   .use_process_environment = true,
                                                               });
        BUSTER_TEST(arguments, c_cleanup_spawn.handle != 0);
        if (c_cleanup_spawn.handle)
        {
            ProcessWaitResult c_cleanup_wait = os_process_wait_sync(arguments->arena, c_cleanup_spawn);
            BUSTER_TEST(arguments, c_cleanup_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_case_range_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-case-range"),
#if BUSTER_WINDOWS
                                                                       S8(".exe"));
#else
                                                                       S8(""));
#endif
    String8 c_case_range_command_line[] = {
        S8("-std=gnu23"),
        S8("-o"),
        c_case_range_executable_path,
        S8("tests/basic_c_case_range.c"),
    };
    CompilerDriverResult c_case_range = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_case_range_command_line)));
    BUSTER_TEST(arguments, c_case_range.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_case_range.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_case_range_run_arguments[] = {
            c_case_range_executable_path,
        };
        ProcessSpawnResult c_case_range_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_case_range_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_case_range_spawn.handle != 0);
        if (c_case_range_spawn.handle)
        {
            ProcessWaitResult c_case_range_wait = os_process_wait_sync(arguments->arena, c_case_range_spawn);
            BUSTER_TEST(arguments, c_case_range_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Compiling the wide-argument unit for every target only says the code
    // buffer held it. Running it on the host says the caller and the callee
    // agree on where each of those arguments went.
    String8 c_wide_argument_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-wide-argument"),
#if BUSTER_WINDOWS
                                                                          S8(".exe"));
#else
                                                                          S8(""));
#endif
    String8 c_wide_argument_command_line[] = {
        S8("-o"),
        c_wide_argument_executable_path,
        S8("tests/basic_c_wide_argument.c"),
    };
    CompilerDriverResult c_wide_argument = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_wide_argument_command_line)));
    BUSTER_TEST(arguments, c_wide_argument.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_wide_argument.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_wide_argument_run_arguments[] = {
            c_wide_argument_executable_path,
        };
        ProcessSpawnResult c_wide_argument_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_wide_argument_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_wide_argument_spawn.handle != 0);
        if (c_wide_argument_spawn.handle)
        {
            ProcessWaitResult c_wide_argument_wait = os_process_wait_sync(arguments->arena, c_wide_argument_spawn);
            BUSTER_TEST(arguments, c_wide_argument_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    if (target_native.cpu_arch == CPU_ARCH_X86_64 &&
        (target_native.os == OPERATING_SYSTEM_LINUX || target_native.os == OPERATING_SYSTEM_MACOS))
    {
        String8 c_f80_transport_path = buster_test_temporary_path(arguments->arena, S8("buster-c-f80-transport"), S8(""));
        String8 c_f80_transport_command_line[] = {
            S8("-o"), c_f80_transport_path, S8("tests/basic_c_f80_transport.c"),
        };
        CompilerDriverResult c_f80_transport = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_f80_transport_command_line)));
        BUSTER_TEST(arguments, c_f80_transport.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_f80_transport.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 c_f80_transport_run_arguments[] = {c_f80_transport_path};
            ProcessSpawnResult c_f80_transport_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_f80_transport_run_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, c_f80_transport_spawn.handle != 0);
            if (c_f80_transport_spawn.handle)
            {
                ProcessWaitResult c_f80_transport_wait = os_process_wait_sync(arguments->arena, c_f80_transport_spawn);
                BUSTER_TEST(arguments, c_f80_transport_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    if (target_native.cpu_arch == CPU_ARCH_X86_64)
    {
        String8 c_int128_path = buster_test_temporary_path(arguments->arena, S8("buster-c-int128"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
        String8 c_int128_command_line[] = {
            S8("-o"), c_int128_path, S8("tests/basic_c_int128.c"),
        };
        CompilerDriverResult c_int128 = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_int128_command_line)));
        BUSTER_TEST(arguments, c_int128.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_int128.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 run_arguments[] = {c_int128_path};
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                        (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    // And the same for the vector shapes, whose x86-64 answer is registers
    // rather than a reference: the host model decides how many of them one
    // vector takes, and running it is what says the caller and the callee
    // counted the same way. It exits 1, 2 or 3 to name which shape disagreed.
    String8 c_wide_vector_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-wide-vector-argument"),
#if BUSTER_WINDOWS
                                                                        S8(".exe"));
#else
                                                                        S8(""));
#endif
    String8 c_wide_vector_command_line[] = {
        S8("-o"),
        c_wide_vector_executable_path,
        S8("tests/basic_c_wide_vector_argument.c"),
    };
    CompilerDriverResult c_wide_vector = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_wide_vector_command_line)));
    BUSTER_TEST(arguments, c_wide_vector.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_wide_vector.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_wide_vector_run_arguments[] = {
            c_wide_vector_executable_path,
        };
        ProcessSpawnResult c_wide_vector_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_wide_vector_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_wide_vector_spawn.handle != 0);
        if (c_wide_vector_spawn.handle)
        {
            ProcessWaitResult c_wide_vector_wait = os_process_wait_sync(arguments->arena, c_wide_vector_spawn);
            BUSTER_TEST(arguments, c_wide_vector_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_infinite_loop_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-infinite-loop"),
#if BUSTER_WINDOWS
                                                                          S8(".exe"));
#else
                                                                          S8(""));
#endif
    String8 c_infinite_loop_command_line[] = {
        S8("-std=gnu23"),
        S8("-o"),
        c_infinite_loop_executable_path,
        S8("tests/basic_c_infinite_loop.c"),
    };
    CompilerDriverResult c_infinite_loop = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_infinite_loop_command_line)));
    BUSTER_TEST(arguments, c_infinite_loop.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_infinite_loop.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_infinite_loop_run_arguments[] = {
            c_infinite_loop_executable_path,
        };
        ProcessSpawnResult c_infinite_loop_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_infinite_loop_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_infinite_loop_spawn.handle != 0);
        if (c_infinite_loop_spawn.handle)
        {
            ProcessWaitResult c_infinite_loop_wait = os_process_wait_sync(arguments->arena, c_infinite_loop_spawn);
            BUSTER_TEST(arguments, c_infinite_loop_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_artifact_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-frontend-artifacts"),
#if BUSTER_WINDOWS
                                                                     S8(".exe"));
#else
                                                                     S8(""));
#endif
    String8 c_artifact_command_line[] = {
        S8("-o"),
        c_artifact_executable_path,
        S8("tests/basic_c_frontend_artifacts.c"),
    };
    CompilerDriverResult c_artifacts = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_artifact_command_line)));
    BUSTER_TEST(arguments, c_artifacts.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_artifacts.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_artifact_run_arguments[] = {
            c_artifact_executable_path,
        };
        ProcessSpawnResult c_artifact_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_artifact_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                                (ProcessSpawnOptions){
                                                                    .use_process_environment = true,
                                                                });
        BUSTER_TEST(arguments, c_artifact_spawn.handle != 0);
        if (c_artifact_spawn.handle)
        {
            ProcessWaitResult c_artifact_wait = os_process_wait_sync(arguments->arena, c_artifact_spawn);
            BUSTER_TEST(arguments, c_artifact_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Windows targets must select CodeView rather than DWARF: the COFF object
    // carries .debug$S/.debug$T with SECREL32/SECTION relocations, and no
    // DWARF sections at all.
    {
        TemporalArena codeview_temporary = scratch_begin(&arguments->arena, 1);
        String8 codeview_object_path = buster_test_temporary_path(codeview_temporary.arena, S8("buster-c-codeview"), S8(".o"));
        String8 codeview_command_line[] = {
            S8("-target"), S8("x86_64-windows"), S8("-c"), S8("-o"), codeview_object_path, S8("tests/basic_c_operations.c"),
        };
        CompilerDriverResult codeview_compile = compiler_driver_execute_invocation(
            codeview_temporary.arena, compiler_driver_parse_arguments(codeview_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(codeview_command_line)));
        BUSTER_TEST(arguments, codeview_compile.error == COMPILER_DRIVER_ERROR_NONE);
        if (codeview_compile.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, codeview_compile.has_object);
            ObjectFile* codeview_object = &codeview_compile.object;
            ByteSlice codeview_symbols = codeview_object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data;
            ByteSlice codeview_types = codeview_object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data;
            BUSTER_TEST(arguments, codeview_symbols.length > 16);
            BUSTER_TEST(arguments, codeview_types.length >= 4);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_DEBUG_INFO].data.length == 0);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_DEBUG_LINE].data.length == 0);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_ZERO].data.length == 0);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_ZERO].virtual_size >= BUSTER_MB(1));
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length >= 64);
            ByteSlice codeview_pdata = codeview_object->sections[OBJECT_SECTION_WINDOWS_PDATA].data;
            ByteSlice codeview_xdata = codeview_object->sections[OBJECT_SECTION_WINDOWS_XDATA].data;
            BUSTER_TEST(arguments, codeview_pdata.length >= 12 && codeview_pdata.length % 12 == 0);
            BUSTER_TEST(arguments, codeview_xdata.length >= 4);
            bool codeview_unwind_relocation = false;
            for (u32 relocation_index = 0; relocation_index < codeview_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = codeview_object->relocations + relocation_index;
                codeview_unwind_relocation |= relocation->section == OBJECT_SECTION_WINDOWS_PDATA &&
                                              relocation->kind == OBJECT_RELOCATION_COFF_ADDR32NB;
            }
            BUSTER_TEST(arguments, codeview_unwind_relocation);
            ByteSlice codeview_file = file_read(codeview_temporary.arena, codeview_object_path, (FileReadOptions){0});
            BUSTER_TEST(arguments, codeview_file.length != 0 && codeview_file.length < BUSTER_MB(1));
            u32 codeview_signature = 0;
            memcpy(&codeview_signature, codeview_symbols.pointer, sizeof(codeview_signature));
            BUSTER_TEST(arguments, codeview_signature == 4);
            u32 secrel_count = 0;
            u32 section_count = 0;
            for (u32 relocation_index = 0; relocation_index < codeview_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation relocation = codeview_object->relocations[relocation_index];
                if (relocation.section != OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS)
                {
                    continue;
                }
                secrel_count += relocation.kind == OBJECT_RELOCATION_COFF_SECREL32;
                section_count += relocation.kind == OBJECT_RELOCATION_COFF_SECTION16;
                BUSTER_TEST(arguments, relocation.symbol < codeview_object->symbol_count);
                BUSTER_TEST(arguments, relocation.offset + 2 <= codeview_symbols.length);
            }
            // Functions and materialized globals each contribute matching
            // section-relative and section-index slots.
            BUSTER_TEST(arguments, secrel_count != 0 && secrel_count == section_count);
        }
        scratch_end(codeview_temporary);
    }
    // Windows ARM64 uses full unwind records so non-canonical frame-base
    // saves, page touches, and every emitted epilog remain explicit.
    {
        TemporalArena arm64_unwind_temporary = scratch_begin(&arguments->arena, 1);
        String8 arm64_unwind_object_path = buster_test_temporary_path(arm64_unwind_temporary.arena, S8("buster-c-arm64-unwind"), S8(".o"));
        String8 arm64_unwind_command_line[] = {
            S8("-target"), S8("aarch64-windows"), S8("-c"), S8("-g0"), S8("-o"), arm64_unwind_object_path, S8("tests/basic_c_large_frame.c"),
        };
        CompilerDriverResult arm64_unwind_compile = compiler_driver_execute_invocation(
            arm64_unwind_temporary.arena,
            compiler_driver_parse_arguments(arm64_unwind_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arm64_unwind_command_line)));
        BUSTER_TEST(arguments, arm64_unwind_compile.error == COMPILER_DRIVER_ERROR_NONE);
        if (arm64_unwind_compile.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ObjectFile* arm64_unwind_object = &arm64_unwind_compile.object;
            ByteSlice arm64_pdata = arm64_unwind_object->sections[OBJECT_SECTION_WINDOWS_PDATA].data;
            ByteSlice arm64_xdata = arm64_unwind_object->sections[OBJECT_SECTION_WINDOWS_XDATA].data;
            BUSTER_TEST(arguments, arm64_pdata.length >= 8 && arm64_pdata.length % 8 == 0);
            BUSTER_TEST(arguments, arm64_xdata.length >= 8);
            bool arm64_large_allocation = false;
            for (u64 byte_index = 0; byte_index < arm64_xdata.length; byte_index += 1)
            {
                arm64_large_allocation |= arm64_xdata.pointer[byte_index] == 0xe0;
            }
            BUSTER_TEST(arguments, arm64_large_allocation);
            u32 arm64_unwind_relocations = 0;
            for (u32 relocation_index = 0; relocation_index < arm64_unwind_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = arm64_unwind_object->relocations + relocation_index;
                arm64_unwind_relocations += relocation->section == OBJECT_SECTION_WINDOWS_PDATA &&
                                            relocation->kind == OBJECT_RELOCATION_COFF_ADDR32NB;
            }
            BUSTER_TEST(arguments, arm64_unwind_relocations == arm64_pdata.length / 4);
            ByteSlice arm64_unwind_file = file_read(arm64_unwind_temporary.arena, arm64_unwind_object_path, (FileReadOptions){0});
            BUSTER_TEST(arguments, arm64_unwind_file.length != 0 && arm64_unwind_file.length < BUSTER_MB(1));
        }
        scratch_end(arm64_unwind_temporary);
    }
    // Recording line rows must not change the code that is generated for
    // them: the machine code has to be identical with and without -g.
    {
        TemporalArena debug_parity_temporary = scratch_begin(&arguments->arena, 1);
        String8 debug_parity_targets[] = {
            S8("x86_64-unknown-linux-gnu"),
            S8("aarch64-apple-macos"),
        };
        String8 debug_parity_sources[] = {
            S8("tests/basic_c_operations.c"),
            S8("tests/basic_c_archive_bias.c"),
            S8("tests/basic_c_multi_add.c"),
        };
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(debug_parity_targets); target_index += 1)
        {
            for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(debug_parity_sources); source_index += 1)
            {
                String8 source = debug_parity_sources[source_index];
                String8 debug_command_line[] = {
                    S8("-c"), S8("-g"), S8("-target"), debug_parity_targets[target_index], source,
                };
                String8 stripped_command_line[] = {
                    S8("-c"), S8("-g0"), S8("-target"), debug_parity_targets[target_index], source,
                };
                CompilerDriverResult with_debug = compiler_driver_execute_invocation(
                    debug_parity_temporary.arena,
                    compiler_driver_parse_arguments(debug_parity_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(debug_command_line)));
                CompilerDriverResult without_debug = compiler_driver_execute_invocation(
                    debug_parity_temporary.arena,
                    compiler_driver_parse_arguments(debug_parity_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stripped_command_line)));
                BUSTER_TEST(arguments, with_debug.error == COMPILER_DRIVER_ERROR_NONE && with_debug.has_object);
                BUSTER_TEST(arguments, without_debug.error == COMPILER_DRIVER_ERROR_NONE && without_debug.has_object);
                if (with_debug.has_object && without_debug.has_object)
                {
                    ByteSlice debug_text = with_debug.object.sections[OBJECT_SECTION_TEXT].data;
                    ByteSlice stripped_text = without_debug.object.sections[OBJECT_SECTION_TEXT].data;
                    BUSTER_TEST(arguments, debug_text.length != 0);
                    BUSTER_TEST(arguments, debug_text.length == stripped_text.length);
                    if (debug_text.length == stripped_text.length)
                    {
                        BUSTER_TEST(arguments, memcmp(debug_text.pointer, stripped_text.pointer, debug_text.length) == 0);
                    }
                    BUSTER_TEST(arguments, with_debug.object.sections[OBJECT_SECTION_DEBUG_LINE].data.length != 0);
                    BUSTER_TEST(arguments, without_debug.object.sections[OBJECT_SECTION_DEBUG_LINE].data.length == 0);
                }
            }
        }
        scratch_end(debug_parity_temporary);
    }
#if BUSTER_LINUX
    // -g must produce loadable executables that carry DWARF line and info
    // sections resolvable back to source lines.
    String8 c_debug_path = buster_test_temporary_path(arguments->arena, S8("buster-c-debug"), S8(""));
    String8 c_debug_command_line[] = {
        S8("-g"),
        S8("-o"),
        c_debug_path,
        S8("tests/basic_c_operations.c"),
    };
    CompilerDriverResult c_debug_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_debug_command_line)));
    BUSTER_TEST(arguments, c_debug_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_debug_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice c_debug_image = file_read(arguments->arena, c_debug_path, (FileReadOptions){0});
        ByteSlice c_debug_line = compiler_driver_test_elf_section(c_debug_image, S8(".debug_line"));
        ByteSlice c_debug_info = compiler_driver_test_elf_section(c_debug_image, S8(".debug_info"));
        ByteSlice c_debug_text = compiler_driver_test_elf_section(c_debug_image, S8(".text"));
        u64 c_debug_text_address = compiler_driver_test_elf_section_address(c_debug_image, S8(".text"));
        BUSTER_TEST(arguments, c_debug_line.length > 4);
        BUSTER_TEST(arguments, c_debug_info.length > 11);
        BUSTER_TEST(arguments, c_debug_text.length != 0);
        BUSTER_TEST(arguments, c_debug_text_address != 0);
        DwarfLineRow c_debug_row = {0};
        BUSTER_TEST(arguments, dwarf_line_lookup(c_debug_line, c_debug_text_address, &c_debug_row));
        BUSTER_TEST(arguments, c_debug_row.line != 0 && c_debug_row.file != 0);
        String8 c_debug_run_arguments[] = {
            c_debug_path,
        };
        ProcessSpawnResult c_debug_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_debug_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_debug_spawn.handle != 0);
        if (c_debug_spawn.handle)
        {
            ProcessWaitResult c_debug_wait = os_process_wait_sync(arguments->arena, c_debug_spawn);
            BUSTER_TEST(arguments, c_debug_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
    String8 string_concat_path = buster_test_temporary_path(arguments->arena, S8("buster-c-string-concat"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
    String8 string_concat_command_line[] = {
        S8("-o"),
        string_concat_path,
        S8("tests/basic_c_string_concat.c"),
    };
    CompilerDriverResult string_concat_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(string_concat_command_line)));
    BUSTER_TEST(arguments, string_concat_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (string_concat_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 string_concat_run_arguments[] = {
            string_concat_path,
        };
        ProcessSpawnResult string_concat_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(string_concat_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, string_concat_spawn.handle != 0);
        if (string_concat_spawn.handle)
        {
            ProcessWaitResult string_concat_wait = os_process_wait_sync(arguments->arena, string_concat_spawn);
            BUSTER_TEST(arguments, string_concat_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 nullptr_path = buster_test_temporary_path(arguments->arena, S8("buster-c-nullptr"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 nullptr_command_line[] = {
        S8("-std=c23"),
        S8("-o"),
        nullptr_path,
        S8("tests/basic_c_nullptr.c"),
    };
    CompilerDriverResult nullptr_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_command_line)));
    BUSTER_TEST(arguments, nullptr_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (nullptr_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 nullptr_run_arguments[] = {
            nullptr_path,
        };
        ProcessSpawnResult nullptr_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, nullptr_spawn.handle != 0);
        if (nullptr_spawn.handle)
        {
            ProcessWaitResult nullptr_wait = os_process_wait_sync(arguments->arena, nullptr_spawn);
            BUSTER_TEST(arguments, nullptr_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    {
        TemporalArena constexpr_temporary = arena_begin_temporal(arguments->arena);
        String8 constexpr_path = buster_test_temporary_path(constexpr_temporary.arena, S8("buster-c-constexpr"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
        String8 constexpr_command_line[] = {
            S8("-std=c23"),
            S8("-o"),
            constexpr_path,
            S8("tests/basic_c_constexpr.c"),
        };
        CompilerDriverResult constexpr_link = compiler_driver_execute_invocation(
            constexpr_temporary.arena, compiler_driver_parse_arguments(constexpr_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_command_line)));
        BUSTER_TEST(arguments, constexpr_link.error == COMPILER_DRIVER_ERROR_NONE);
        if (constexpr_link.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 constexpr_run_arguments[] = {
                constexpr_path,
            };
            ProcessSpawnResult constexpr_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_run_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, constexpr_spawn.handle != 0);
            if (constexpr_spawn.handle)
            {
                ProcessWaitResult constexpr_wait = os_process_wait_sync(constexpr_temporary.arena, constexpr_spawn);
                BUSTER_TEST(arguments, constexpr_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(constexpr_temporary);
    }
#if BUSTER_LINUX
    String8 c_dynamic_library_path = buster_test_temporary_path(arguments->arena, S8("buster-c-dynamic-library"), S8(""));
    String8 c_dynamic_library_command_line[] = {
        S8("-o"),
        c_dynamic_library_path,
        S8("-l:libm.so.6"),
        S8("tests/basic_c_dynamic_library.c"),
    };
    CompilerDriverResult c_dynamic_library = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_dynamic_library_command_line)));
    BUSTER_TEST(arguments, c_dynamic_library.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_dynamic_library.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, compiler_driver_bytes_contain(c_dynamic_library.native_link.executable, S8("libm.so.6")));
        String8 run_arguments[] = {
            c_dynamic_library_path,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .use_process_environment = true,
                                                    });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
    String8 c_atomic_path = buster_test_temporary_path(arguments->arena, S8("buster-c-atomic"),
#if BUSTER_WINDOWS
                                                       S8(".exe"));
#else
                                                       S8(""));
#endif
    String8 c_atomic_command_line[] = {
        S8("-o"),
        c_atomic_path,
        S8("tests/basic_c_atomic.c"),
    };
    CompilerDriverResult c_atomic = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_atomic_command_line)));
    BUSTER_TEST(arguments, c_atomic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_atomic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_atomic_arguments[] = {
            c_atomic_path,
        };
        ProcessSpawnResult c_atomic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_atomic_arguments), (SliceString8){0}, (SliceString8){0},
                                                             (ProcessSpawnOptions){
                                                                 .use_process_environment = true,
                                                             });
        BUSTER_TEST(arguments, c_atomic_spawn.handle != 0);
        if (c_atomic_spawn.handle)
        {
            ProcessWaitResult c_atomic_wait = os_process_wait_sync(arguments->arena, c_atomic_spawn);
            BUSTER_TEST(arguments, c_atomic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    TemporalArena c_stdatomic_temporary = arena_begin_temporal(arguments->arena);
    String8 c_stdatomic_path = buster_test_temporary_path(c_stdatomic_temporary.arena, S8("buster-c-stdatomic"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 c_stdatomic_command_line[] = {
        S8("-o"),
        c_stdatomic_path,
        S8("tests/basic_c_stdatomic.c"),
    };
    CompilerDriverResult c_stdatomic = compiler_driver_execute_invocation(
        c_stdatomic_temporary.arena,
        compiler_driver_parse_arguments(c_stdatomic_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_stdatomic_command_line)));
    BUSTER_TEST(arguments, c_stdatomic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_stdatomic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_stdatomic_arguments[] = {
            c_stdatomic_path,
        };
        ProcessSpawnResult c_stdatomic_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_stdatomic_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_stdatomic_spawn.handle != 0);
        if (c_stdatomic_spawn.handle)
        {
            ProcessWaitResult c_stdatomic_wait = os_process_wait_sync(c_stdatomic_temporary.arena, c_stdatomic_spawn);
            BUSTER_TEST(arguments, c_stdatomic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(c_stdatomic_temporary);
    String8 c_generic_path = buster_test_temporary_path(arguments->arena, S8("buster-c-generic"),
#if BUSTER_WINDOWS
                                                        S8(".exe"));
#else
                                                        S8(""));
#endif
    String8 c_generic_command_line[] = {
        S8("-o"),
        c_generic_path,
        S8("tests/basic_c_generic.c"),
    };
    CompilerDriverResult c_generic = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_generic_command_line)));
    BUSTER_TEST(arguments, c_generic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_generic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_generic_arguments[] = {
            c_generic_path,
        };
        ProcessSpawnResult c_generic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_generic_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_generic_spawn.handle != 0);
        if (c_generic_spawn.handle)
        {
            ProcessWaitResult c_generic_wait = os_process_wait_sync(arguments->arena, c_generic_spawn);
            BUSTER_TEST(arguments, c_generic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_alignas_path = buster_test_temporary_path(arguments->arena, S8("buster-c-alignas"),
#if BUSTER_WINDOWS
                                                        S8(".exe"));
#else
                                                        S8(""));
#endif
    String8 c_alignas_command_line[] = {
        S8("-o"),
        c_alignas_path,
        S8("tests/basic_c_alignas.c"),
    };
    CompilerDriverResult c_alignas = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_alignas_command_line)));
    BUSTER_TEST(arguments, c_alignas.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_alignas.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_alignas_arguments[] = {
            c_alignas_path,
        };
        ProcessSpawnResult c_alignas_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_alignas_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_alignas_spawn.handle != 0);
        if (c_alignas_spawn.handle)
        {
            ProcessWaitResult c_alignas_wait = os_process_wait_sync(arguments->arena, c_alignas_spawn);
            BUSTER_TEST(arguments, c_alignas_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_declarator_list_path = buster_test_temporary_path(arguments->arena, S8("buster-c-declarator-list"),
#if BUSTER_WINDOWS
                                                                S8(".exe"));
#else
                                                                S8(""));
#endif
    String8 c_declarator_list_command_line[] = {
        S8("-o"),
        c_declarator_list_path,
        S8("tests/basic_c_declarator_list.c"),
    };
    CompilerDriverResult c_declarator_list = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_declarator_list_command_line)));
    BUSTER_TEST(arguments, c_declarator_list.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_declarator_list.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_declarator_list_arguments[] = {
            c_declarator_list_path,
        };
        ProcessSpawnResult c_declarator_list_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_declarator_list_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_declarator_list_spawn.handle != 0);
        if (c_declarator_list_spawn.handle)
        {
            ProcessWaitResult c_declarator_list_wait = os_process_wait_sync(arguments->arena, c_declarator_list_spawn);
            BUSTER_TEST(arguments, c_declarator_list_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vla_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vla"),
#if BUSTER_WINDOWS
                                                    S8(".exe"));
#else
                                                    S8(""));
#endif
    String8 c_vla_command_line[] = {
        S8("-o"),
        c_vla_path,
        S8("tests/basic_c_vla.c"),
    };
    CompilerDriverResult c_vla = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vla_command_line)));
    BUSTER_TEST(arguments, c_vla.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_vla.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_vla_arguments[] = {
            c_vla_path,
        };
        ProcessSpawnResult c_vla_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_vla_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .use_process_environment = true,
                                                          });
        BUSTER_TEST(arguments, c_vla_spawn.handle != 0);
        if (c_vla_spawn.handle)
        {
            ProcessWaitResult c_vla_wait = os_process_wait_sync(arguments->arena, c_vla_spawn);
            BUSTER_TEST(arguments, c_vla_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#if BUSTER_CPU_ARCH_X86_64
    {
        TemporalArena c_vla_windows_temporary = scratch_begin(&arguments->arena, 1);
        String8 c_vla_windows_object_path = buster_test_temporary_path(c_vla_windows_temporary.arena, S8("buster-c-vla-windows"), S8(".obj"));
        String8 c_vla_windows_command_line[] = {
            S8("-c"), S8("-g"), S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-o"), c_vla_windows_object_path, S8("tests/basic_c_vla.c"),
        };
        CompilerDriverResult c_vla_windows = compiler_driver_execute_invocation(
            c_vla_windows_temporary.arena,
            compiler_driver_parse_arguments(c_vla_windows_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vla_windows_command_line)));
        BUSTER_TEST(arguments, c_vla_windows.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_vla_windows.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, c_vla_windows.has_object);
            if (c_vla_windows.has_object)
            {
                bool has_frame_register = false;
                bool unwind_records_valid = compiler_driver_test_windows_x64_unwind_records(&c_vla_windows.object, &has_frame_register);
                BUSTER_TEST(arguments, unwind_records_valid && has_frame_register);
                BUSTER_TEST(arguments, compiler_driver_test_windows_x64_has_frame_lea(&c_vla_windows.object));
            }
        }
        scratch_end(c_vla_windows_temporary);
    }
#endif
    String8 c_aggregate_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aggregate"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 c_aggregate_command_line[] = {
        S8("-o"),
        c_aggregate_path,
        S8("tests/basic_c_argv_aggregate.c"),
    };
    CompilerDriverResult c_aggregate = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_aggregate_command_line)));
    BUSTER_TEST(arguments, c_aggregate.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_aggregate.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_aggregate_arguments[] = {
            c_aggregate_path,
            S8("cc"),
            S8("a"),
            S8("b"),
        };
        ProcessSpawnResult c_aggregate_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_aggregate_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_aggregate_spawn.handle != 0);
        if (c_aggregate_spawn.handle)
        {
            ProcessWaitResult c_aggregate_wait = os_process_wait_sync(arguments->arena, c_aggregate_spawn);
            BUSTER_TEST(arguments, c_aggregate_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_labels_path = buster_test_temporary_path(arguments->arena, S8("buster-c-labels"),
#if BUSTER_WINDOWS
                                                       S8(".exe"));
#else
                                                       S8(""));
#endif
    String8 c_labels_command_line[] = {
        S8("-o"),
        c_labels_path,
        S8("tests/basic_c_labels.c"),
    };
    CompilerDriverResult c_labels = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_labels_command_line)));
    BUSTER_TEST(arguments, c_labels.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_labels.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_labels_arguments[] = {
            c_labels_path,
        };
        ProcessSpawnResult c_labels_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_labels_arguments), (SliceString8){0}, (SliceString8){0},
                                                             (ProcessSpawnOptions){
                                                                 .use_process_environment = true,
                                                             });
        BUSTER_TEST(arguments, c_labels_spawn.handle != 0);
        if (c_labels_spawn.handle)
        {
            ProcessWaitResult c_labels_wait = os_process_wait_sync(arguments->arena, c_labels_spawn);
            BUSTER_TEST(arguments, c_labels_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A zeroed label-table slot must remain a non-label value.  The runtime
    // dispatch therefore traps on the selector-0 path instead of treating a
    // sibling label as an over-approximation of the overwritten element.
    String8 c_invalid_labels_path = buster_test_temporary_path(arguments->arena, S8("buster-c-invalid-labels"),
#if BUSTER_WINDOWS
                                                                S8(".exe"));
#else
                                                                S8(""));
#endif
    String8 c_invalid_labels_command_line[] = {
        S8("-g0"),
        S8("-o"),
        c_invalid_labels_path,
        S8("tests/basic_c_invalid_labels.c"),
    };
    CompilerDriverResult c_invalid_labels = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_invalid_labels_command_line)));
    BUSTER_TEST(arguments, c_invalid_labels.error == COMPILER_DRIVER_ERROR_ANALYSIS);
    BUSTER_TEST(arguments, c_invalid_labels.codegen_error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, c_invalid_labels.tokenizer_error_count == 0 && c_invalid_labels.parser_diagnostic_count == 0 &&
                           c_invalid_labels.analysis_diagnostic_count == 1 && !c_invalid_labels.has_object);
    BUSTER_TEST(arguments, string_starts_with_sequence(c_invalid_labels.diagnostic,
                                                       S8("tests/basic_c_invalid_labels.c:17:11: in function 'invalid_unsafe_branch': computed goto requires a function-local void pointer label value")));
    String8 c_labels_aarch64_path = buster_test_temporary_path(arguments->arena, S8("buster-c-labels-aarch64"), S8(".o"));
    String8 c_labels_aarch64_command_line[] = {
        S8("-c"),
        S8("-target"),
        S8("aarch64-unknown-linux-gnu"),
        S8("-o"),
        c_labels_aarch64_path,
        S8("tests/basic_c_labels.c"),
    };
    CompilerDriverResult c_labels_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_labels_aarch64_command_line)));
    BUSTER_TEST(arguments, c_labels_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_labels_aarch64.has_object);
    if (c_labels_aarch64.has_object)
    {
        ByteSlice aarch64_label_text = c_labels_aarch64.object.sections[OBJECT_SECTION_TEXT].data;
        BUSTER_TEST(arguments, aarch64_label_text.length != 0);
        bool asm_goto_value_found = false;
        bool asm_goto_value_branch = false;
        bool asm_goto_value_sevl = false;
        for (u32 symbol_index = 0; symbol_index < c_labels_aarch64.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = c_labels_aarch64.object.symbols + symbol_index;
            if (!string_equal(symbol->name, S8("asm_goto_value")) || symbol->section != OBJECT_SECTION_TEXT || symbol->value > aarch64_label_text.length ||
                symbol->size > aarch64_label_text.length - symbol->value)
            {
                continue;
            }
            asm_goto_value_found = true;
            for (u64 byte_offset = symbol->value; byte_offset + sizeof(u32) <= symbol->value + symbol->size; byte_offset += sizeof(u32))
            {
                u32 word = 0;
                memcpy(&word, aarch64_label_text.pointer + byte_offset, sizeof(word));
                asm_goto_value_sevl |= word == UINT32_C(0xd50320bf);
                asm_goto_value_branch |= (word & UINT32_C(0xfc000000)) == UINT32_C(0x14000000);
            }
        }
        BUSTER_TEST(arguments, asm_goto_value_found && asm_goto_value_branch && !asm_goto_value_sevl);
        u32 aarch64_label_relocation_count = 0;
        BUSTER_TEST(arguments, compiler_driver_test_label_relocations(&c_labels_aarch64.object, &aarch64_label_relocation_count));
        ObjectFormat aarch64_format = object_format_for_target(c_labels_aarch64.object.target);
        ObjectArtifact aarch64_artifact = object_write(arguments->arena, &c_labels_aarch64.object, aarch64_format);
        BUSTER_TEST(arguments, aarch64_artifact.error == OBJECT_ERROR_NONE);
        ObjectFile aarch64_roundtrip = object_read(arguments->arena, aarch64_artifact.bytes, c_labels_aarch64.object.target);
        BUSTER_TEST(arguments, compiler_driver_test_label_relocations_roundtrip(&c_labels_aarch64.object, &aarch64_roundtrip));
    }
    {
        Arena* long_label_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
        String8 long_label_prefix = S8("int long_label_address(void)\n{\n    void *target = &&target;\n");
        String8 long_label_asm_prefix = S8("    __asm__(\"");
        String8 long_label_asm_nop = S8("nop\\n");
        String8 long_label_asm_suffix = S8("\");\n");
        String8 long_label_suffix = S8("    goto *target;\ntarget:\n    return 0;\n}\n");
        u64 statement_count = 9000;
        u64 nops_per_statement = 31;
        u64 statement_length = long_label_asm_prefix.length + nops_per_statement * long_label_asm_nop.length + long_label_asm_suffix.length;
        u64 source_length = long_label_prefix.length + statement_count * statement_length + long_label_suffix.length;
        char8* source_pointer = arena_allocate(long_label_arena, char8, source_length);
        u64 source_offset = 0;
        memcpy(source_pointer + source_offset, long_label_prefix.pointer, long_label_prefix.length);
        source_offset += long_label_prefix.length;
        for (u64 statement_index = 0; statement_index < statement_count; statement_index += 1)
        {
            memcpy(source_pointer + source_offset, long_label_asm_prefix.pointer, long_label_asm_prefix.length);
            source_offset += long_label_asm_prefix.length;
            for (u64 nop_index = 0; nop_index < nops_per_statement; nop_index += 1)
            {
                memcpy(source_pointer + source_offset, long_label_asm_nop.pointer, long_label_asm_nop.length);
                source_offset += long_label_asm_nop.length;
            }
            memcpy(source_pointer + source_offset, long_label_asm_suffix.pointer, long_label_asm_suffix.length);
            source_offset += long_label_asm_suffix.length;
        }
        memcpy(source_pointer + source_offset, long_label_suffix.pointer, long_label_suffix.length);
        source_offset += long_label_suffix.length;
        String8 long_label_source_path = buster_test_temporary_path(long_label_arena, S8("buster-c-label-address-long"), S8(".c"));
        BUSTER_TEST(arguments, file_write(long_label_source_path, (ByteSlice){.pointer = (u8*)source_pointer, .length = source_offset}));
        String8 long_label_object_path = buster_test_temporary_path(long_label_arena, S8("buster-c-label-address-long"), S8(".o"));
        String8 long_label_command_line[] = {
            S8("-c"), S8("-g0"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), long_label_object_path, long_label_source_path,
        };
        CompilerDriverResult long_label = compiler_driver_execute_invocation(
            long_label_arena, compiler_driver_parse_arguments(long_label_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(long_label_command_line)));
        BUSTER_TEST(arguments, long_label.error == COMPILER_DRIVER_ERROR_NONE && long_label.has_object);
        bool long_label_sequence = false;
        if (long_label.has_object)
        {
            ByteSlice long_text = long_label.object.sections[OBJECT_SECTION_TEXT].data;
            u64 function_offset = 0;
            u64 function_size = 0;
            bool function_found = false;
            for (u32 symbol_index = 0; symbol_index < long_label.object.symbol_count; symbol_index += 1)
            {
                ObjectSymbol* symbol = long_label.object.symbols + symbol_index;
                if (string_equal(symbol->name, S8("long_label_address")) && symbol->section == OBJECT_SECTION_TEXT)
                {
                    function_offset = symbol->value;
                    function_size = symbol->size;
                    function_found = true;
                    break;
                }
            }
            BUSTER_TEST(arguments, function_found && function_offset <= long_text.length && function_size <= long_text.length - function_offset);
            if (function_found && function_offset <= long_text.length && function_size <= long_text.length - function_offset)
            {
                for (u64 byte_offset = function_offset; byte_offset + 24 <= function_offset + function_size; byte_offset += 4)
                {
                    u32 adr = 0;
                    u32 movz = 0;
                    u32 movk16 = 0;
                    u32 movk32 = 0;
                    u32 movk48 = 0;
                    u32 add = 0;
                    memcpy(&adr, long_text.pointer + byte_offset, sizeof(adr));
                    memcpy(&movz, long_text.pointer + byte_offset + 4, sizeof(movz));
                    memcpy(&movk16, long_text.pointer + byte_offset + 8, sizeof(movk16));
                    memcpy(&movk32, long_text.pointer + byte_offset + 12, sizeof(movk32));
                    memcpy(&movk48, long_text.pointer + byte_offset + 16, sizeof(movk48));
                    memcpy(&add, long_text.pointer + byte_offset + 20, sizeof(add));
                    bool is_adr_x9 = adr == UINT32_C(0x10000009);
                    bool is_movz_x10 = (movz & UINT32_C(0xffe0001f)) == UINT32_C(0xd280000a);
                    bool is_movk16_x10 = (movk16 & UINT32_C(0xffe0001f)) == UINT32_C(0xf2a0000a);
                    bool is_movk32_x10 = (movk32 & UINT32_C(0xffe0001f)) == UINT32_C(0xf2c0000a);
                    bool is_movk48_x10 = (movk48 & UINT32_C(0xffe0001f)) == UINT32_C(0xf2e0000a);
                    bool is_add_x9_x9_x10 = add == UINT32_C(0x8b0a0129);
                    long_label_sequence |= is_adr_x9 && is_movz_x10 && is_movk16_x10 && is_movk32_x10 && is_movk48_x10 && is_add_x9_x9_x10;
                }
            }
        }
        BUSTER_TEST(arguments, long_label_sequence);
        BUSTER_TEST(arguments, arena_destroy(long_label_arena, 1));
    }
    String8 c_labels_object_path = buster_test_temporary_path(arguments->arena, S8("buster-c-labels-object"), S8(".o"));
    String8 c_labels_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_labels_object_path,
        S8("tests/basic_c_labels.c"),
    };
    CompilerDriverResult c_labels_object = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_labels_object_command_line)));
    BUSTER_TEST(arguments, c_labels_object.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_labels_object.has_object);
    if (c_labels_object.has_object)
    {
        ByteSlice text = c_labels_object.object.sections[OBJECT_SECTION_TEXT].data;
        BUSTER_TEST(arguments, text.length != 0);
        u32 label_relocation_count = 0;
        BUSTER_TEST(arguments, compiler_driver_test_label_relocations(&c_labels_object.object, &label_relocation_count));
        ObjectFormat label_format = object_format_for_target(c_labels_object.object.target);
        ObjectArtifact label_artifact = object_write(arguments->arena, &c_labels_object.object, label_format);
        BUSTER_TEST(arguments, label_artifact.error == OBJECT_ERROR_NONE);
        ObjectFile label_roundtrip = object_read(arguments->arena, label_artifact.bytes, c_labels_object.object.target);
        BUSTER_TEST(arguments, compiler_driver_test_label_relocations_roundtrip(&c_labels_object.object, &label_roundtrip));
#if BUSTER_CPU_ARCH_X86_64
        u64 function_offset = 0;
        u64 function_size = 0;
        bool function_found = false;
        for (u32 symbol_index = 0; symbol_index < c_labels_object.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = c_labels_object.object.symbols + symbol_index;
            if (string_equal(symbol->name, S8("asm_goto_saved_register")) && symbol->section == OBJECT_SECTION_TEXT)
            {
                function_offset = symbol->value;
                function_size = symbol->size;
                function_found = true;
                break;
            }
        }
        BUSTER_TEST(arguments, function_found && function_offset <= text.length && function_size <= text.length - function_offset);
        bool restored_before_taken_edge = false;
        bool restored_before_fallthrough = false;
        bool windows_target = c_labels_object.object.target.os == OPERATING_SYSTEM_WINDOWS;
        if (function_found && function_offset <= text.length && function_size <= text.length - function_offset)
        {
            for (u64 byte_index = function_offset; byte_index < function_offset + function_size; byte_index += 1)
            {
                u64 function_end = function_offset + function_size;
                u32 restore_size = compiler_driver_test_x64_restore_rbx_size(text, byte_index, function_end);
                if (restore_size)
                {
                    u64 after_restore = byte_index + restore_size;
                    restored_before_taken_edge |= compiler_driver_test_x64_unconditional_jump(text, after_restore, function_end);
                    restored_before_fallthrough |= compiler_driver_test_x64_fallthrough_epilog(text, after_restore, function_end, windows_target);
                }
            }
        }
        BUSTER_TEST(arguments, restored_before_taken_edge);
        BUSTER_TEST(arguments, restored_before_fallthrough);
#endif
    }
    String8 invalid_asm_jump_path = buster_test_temporary_path(arguments->arena, S8("buster-invalid-asm-jump"), S8(".o"));
    String8 invalid_asm_jump_command_line[] = {
        S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-o"), invalid_asm_jump_path, S8("tests/basic_c_invalid_asm_goto.c"),
    };
    CompilerDriverResult invalid_asm_jump = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_asm_jump_command_line)));
    BUSTER_TEST(arguments, invalid_asm_jump.error == COMPILER_DRIVER_ERROR_CODEGEN);
    BUSTER_TEST(arguments, invalid_asm_jump.codegen_error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION);
    BUSTER_TEST(arguments, invalid_asm_jump.tokenizer_error_count == 0);
    BUSTER_TEST(arguments, invalid_asm_jump.parser_diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_asm_jump.analysis_diagnostic_count == 0);
    BUSTER_TEST(arguments, !invalid_asm_jump.has_object && invalid_asm_jump.diagnostic.length != 0);
    BUSTER_TEST(arguments, string_starts_with_sequence(invalid_asm_jump.diagnostic, S8("C code generation failed with error 2, function 0 ('main'")));
    {
        String8 unsupported_template_source_path = buster_test_temporary_path(arguments->arena, S8("buster-invalid-asm-conditional"), S8(".c"));
        String8 unsupported_template_source = S8("int conditional_asm_goto(int value) {"
                                                 " __asm__ goto (\"jne %l1\" : : \"r\"(value) : \"cc\" : taken);"
                                                 " return 0; taken: return 1; }\n");
        BUSTER_TEST(arguments, file_write(unsupported_template_source_path, BUSTER_SLICE_TO_BYTE_SLICE(unsupported_template_source)));
        String8 unsupported_template_object_path = buster_test_temporary_path(arguments->arena, S8("buster-invalid-asm-conditional"), S8(".o"));
        String8 unsupported_template_command_line[] = {
            S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-o"), unsupported_template_object_path, unsupported_template_source_path,
        };
        CompilerDriverResult unsupported_template = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unsupported_template_command_line)));
        BUSTER_TEST(arguments, unsupported_template.error == COMPILER_DRIVER_ERROR_CODEGEN);
        BUSTER_TEST(arguments, unsupported_template.codegen_error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION);
        BUSTER_TEST(arguments, unsupported_template.tokenizer_error_count == 0 && unsupported_template.parser_diagnostic_count == 0 &&
                               unsupported_template.analysis_diagnostic_count == 0);
        BUSTER_TEST(arguments, string_starts_with_sequence(unsupported_template.diagnostic,
                                                           S8("C code generation failed with error 2, function 0 ('conditional_asm_goto'")));
    }
    {
        // Ordinary templates are deliberately a closed, scalar register-only
        // subset.  Keep each rejection explicit so a future assembler-table
        // expansion cannot accidentally widen compiler-side effects.
        String8 rejected_templates[] = {
            S8("ret"),
            S8("call %0"),
            S8("jmp %0"),
            S8(".byte 0x90"),
            S8("mul %0"),
            S8("mov %%rax, %%rax"),
            S8("add %0, 1"),
            S8("mov foo, %0"),
        };
        String8 rejected_dialects[] = {S8("att"), S8("intel")};
        for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(rejected_dialects); dialect_index += 1)
        {
            for (u32 template_index = 0; template_index < BUSTER_ARRAY_LENGTH(rejected_templates); template_index += 1)
            {
                u32 case_index = dialect_index * (u32)BUSTER_ARRAY_LENGTH(rejected_templates) + template_index;
                String8 source_path = buster_test_temporary_path(arguments->arena, S8("buster-invalid-asm-template"),
                                                                  string_format(arguments->arena, S8("-{u32}.c"), case_index));
                String8 source = string_format(arguments->arena, S8("int invalid_template(int value) {{ __asm__ volatile(\"{S8}\" : \"+r\"(value)); return value; }}\n"),
                                                rejected_templates[template_index]);
                BUSTER_TEST(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source)));
                String8 rejected_command_line[] = {
                    S8("-c"), S8("-masm"), rejected_dialects[dialect_index], S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-o"),
                    buster_test_temporary_path(arguments->arena, S8("buster-invalid-asm-template"), string_format(arguments->arena, S8("-{u32}.o"), case_index)),
                    source_path,
                };
                CompilerDriverResult rejected = compiler_driver_execute_invocation(
                    arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(rejected_command_line)));
                BUSTER_TEST(arguments, rejected.error == COMPILER_DRIVER_ERROR_CODEGEN);
                BUSTER_TEST(arguments, rejected.codegen_error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION);
            }
        }
    }
    {
        String8 label_object_targets[] = {
            S8("x86_64-pc-windows-msvc"), S8("x86_64-apple-macos"), S8("aarch64-pc-windows-msvc"), S8("aarch64-apple-macos"),
        };
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(label_object_targets); target_index += 1)
        {
            TemporalArena label_object_temporary = scratch_begin(&arguments->arena, 1);
            String8 object_path = buster_test_temporary_path(label_object_temporary.arena, S8("buster-c-label-format"),
                                                             string_format(label_object_temporary.arena, S8("-{u32}.o"), target_index));
            String8 label_object_command_line[] = {
                S8("-c"), S8("-g0"), S8("-target"), label_object_targets[target_index], S8("-o"), object_path, S8("tests/basic_c_labels.c"),
            };
            CompilerDriverResult label_object = compiler_driver_execute_invocation(
                label_object_temporary.arena,
                compiler_driver_parse_arguments(label_object_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(label_object_command_line)));
            BUSTER_TEST(arguments, label_object.error == COMPILER_DRIVER_ERROR_NONE && label_object.has_object);
            if (label_object.error == COMPILER_DRIVER_ERROR_NONE && label_object.has_object)
            {
                u32 label_relocation_count = 0;
                BUSTER_TEST(arguments, compiler_driver_test_label_relocations(&label_object.object, &label_relocation_count));
                ObjectFormat label_format = object_format_for_target(label_object.object.target);
                ObjectArtifact label_artifact = object_write(label_object_temporary.arena, &label_object.object, label_format);
                BUSTER_TEST(arguments, label_artifact.error == OBJECT_ERROR_NONE);
                ObjectFile label_roundtrip = object_read(label_object_temporary.arena, label_artifact.bytes, label_object.object.target);
                BUSTER_TEST(arguments, compiler_driver_test_label_relocations_roundtrip(&label_object.object, &label_roundtrip));
            }
            scratch_end(label_object_temporary);
        }
    }
#if (BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64) || BUSTER_MACOS
    String8 c_thread_local_path = buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local"), S8(""));
    String8 c_thread_local_command_line[] = {
        S8("-o"),
        c_thread_local_path,
        S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult c_thread_local = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_command_line)));
    BUSTER_TEST(arguments, c_thread_local.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_thread_local.has_object);
    if (c_thread_local.has_object)
    {
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length == sizeof(u32));
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].data.length == 0);
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size == sizeof(u32));
        bool found_thread_local_symbol = false;
        bool found_thread_local_zero_symbol = false;
        bool found_thread_local_relocation = false;
        for (u32 symbol_index = 0; symbol_index < c_thread_local.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = &c_thread_local.object.symbols[symbol_index];
            found_thread_local_symbol |= symbol->section == OBJECT_SECTION_THREAD_LOCAL_DATA && string_equal(symbol->name, S8("thread_local_value"));
            found_thread_local_zero_symbol |= symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO && string_equal(symbol->name, S8("thread_local_zero"));
        }
        for (u32 relocation_index = 0; relocation_index < c_thread_local.object.relocation_count; relocation_index += 1)
        {
            ObjectRelocationKind kind = c_thread_local.object.relocations[relocation_index].kind;
            found_thread_local_relocation |= kind == OBJECT_RELOCATION_X86_64_TPOFF32 || kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ||
                                             kind == OBJECT_RELOCATION_PE_TLS_OFFSET32 || kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP ||
                                             kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12 || kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12 ||
                                             kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 || kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12 ||
                                             kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 || kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 ||
                                             kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12;
        }
        BUSTER_TEST(arguments, found_thread_local_symbol);
        BUSTER_TEST(arguments, found_thread_local_zero_symbol);
        BUSTER_TEST(arguments, found_thread_local_relocation);
    }
    if (c_thread_local.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_thread_local_arguments[] = {
            c_thread_local_path,
        };
        ProcessSpawnResult c_thread_local_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_thread_local_spawn.handle != 0);
        if (c_thread_local_spawn.handle)
        {
            ProcessWaitResult c_thread_local_wait = os_process_wait_sync(arguments->arena, c_thread_local_spawn);
            BUSTER_TEST(arguments, c_thread_local_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_thread_local_aarch64_path = buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-aarch64"), S8(""));
    String8 c_thread_local_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_thread_local_aarch64_path, S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult c_thread_local_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_aarch64_command_line)));
    BUSTER_TEST(arguments, c_thread_local_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_thread_local_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice executable = c_thread_local_aarch64.native_link.executable;
        bool machine_matches = executable.length >= 20 && executable.pointer[18] == 183 && executable.pointer[19] == 0;
        bool found_thread_pointer_add = false;
        static u8 const thread_pointer_add[] = {
            0x29,
            0x41,
            0x00,
            0x91,
        };
        for (u64 byte_index = 0; byte_index + sizeof(thread_pointer_add) <= executable.length; byte_index += 1)
        {
            if (memcmp(executable.pointer + byte_index, thread_pointer_add, sizeof(thread_pointer_add)) == 0)
            {
                found_thread_pointer_add = true;
                break;
            }
        }
        BUSTER_TEST(arguments, machine_matches);
        BUSTER_TEST(arguments, found_thread_pointer_add);
    }
    String8 c_thread_local_windows_targets[] = {
        S8("x86_64-pc-windows-msvc"),
        S8("aarch64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_thread_local_windows_targets); target_index += 1)
    {
        String8 c_thread_local_windows_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-windows"), string_format(arguments->arena, S8("-{u32}.exe"), target_index));
        String8 windows_tls_command_line[] = {
            S8("-target"), c_thread_local_windows_targets[target_index], S8("-o"), c_thread_local_windows_path, S8("tests/basic_c_thread_local.c"),
        };
        CompilerDriverResult windows_tls = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_tls_command_line)));
        BUSTER_TEST(arguments, windows_tls.error == COMPILER_DRIVER_ERROR_NONE);
        if (windows_tls.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ByteSlice executable = windows_tls.native_link.executable;
            bool pe_header = executable.length > 0x84 && executable.pointer[0] == 'M' && executable.pointer[1] == 'Z' && executable.pointer[0x80] == 'P' &&
                             executable.pointer[0x81] == 'E';
            u32 tls_directory_rva = 0;
            if (executable.length >= 0x80 + 4 + 20 + 188)
            {
                memcpy(&tls_directory_rva, executable.pointer + 0x80 + 4 + 20 + 184, sizeof(tls_directory_rva));
            }
            BUSTER_TEST(arguments, pe_header);
            BUSTER_TEST(arguments, tls_directory_rva != 0);
            BUSTER_TEST(arguments, compiler_driver_test_pe_tls_directory(executable, 16));
        }
    }
    String8 c_thread_local_apple_targets[] = {
        S8("x86_64-apple-macos"),
        S8("arm64-apple-macos"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_thread_local_apple_targets); target_index += 1)
    {
        String8 apple_tls_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-apple"), string_format(arguments->arena, S8("-{u32}"), target_index));
        String8 apple_tls_command_line[] = {
            S8("-target"),  c_thread_local_apple_targets[target_index], S8("-framework"), S8("CoreFoundation"), S8("-o"),
            apple_tls_path, S8("tests/basic_c_thread_local.c"),
        };
        CompilerDriverResult apple_tls = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(apple_tls_command_line)));
        BUSTER_TEST(arguments, apple_tls.error == COMPILER_DRIVER_ERROR_NONE);
        if (apple_tls.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ByteSlice executable = apple_tls.native_link.executable;
            u32 mach_flags = 0;
            if (executable.length >= 28)
            {
                memcpy(&mach_flags, executable.pointer + 24, sizeof(mach_flags));
            }
            BUSTER_TEST(arguments, executable.length >= 32 && executable.pointer[0] == 0xcf && executable.pointer[1] == 0xfa && executable.pointer[2] == 0xed &&
                                       executable.pointer[3] == 0xfe);
            BUSTER_TEST(arguments, (mach_flags & 0x800000) != 0);
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_vars")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_data")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_bss")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__tlv_bootstrap")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("/System/Library/Frameworks/"
                                                                                "CoreFoundation.framework/"
                                                                                "CoreFoundation")));
            bool found_tlv_relocation = false;
            for (u32 relocation_index = 0; relocation_index < apple_tls.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocationKind kind = apple_tls.object.relocations[relocation_index].kind;
                found_tlv_relocation |= kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 || kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21;
            }
            BUSTER_TEST(arguments, found_tlv_relocation);
        }
    }
    String8 c_float_abi_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi"), S8(""));
    String8 c_float_abi_command_line[] = {
        S8("-o"),
        c_float_abi_path,
        S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_command_line)));
    BUSTER_TEST(arguments, c_float_abi.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_float_abi_arguments[] = {
            c_float_abi_path,
        };
        ProcessSpawnResult c_float_abi_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_float_abi_spawn.handle != 0);
        if (c_float_abi_spawn.handle)
        {
            ProcessWaitResult c_float_abi_wait = os_process_wait_sync(arguments->arena, c_float_abi_spawn);
            BUSTER_TEST(arguments, c_float_abi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vector_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector"),
#if BUSTER_WINDOWS
                                                       S8(".exe"));
#else
                                                       S8(""));
#endif
    String8 c_vector_command_line[] = {
        S8("-o"),
        c_vector_path,
        S8("tests/basic_c_vector.c"),
    };
    CompilerDriverResult c_vector = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_command_line)));
    BUSTER_TEST(arguments, c_vector.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_native.cpu_arch != CPU_ARCH_X86_64 || c_vector.codegen_statistics.native_vector_operation_count > 0);
    String8 c_vector_baseline_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-baseline"), S8(".o"));
    String8 c_vector_baseline_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-o"), c_vector_baseline_path, S8("tests/basic_c_vector.c"),
    };
    Arena* c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_baseline_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_baseline_command_line));
    CompilerDriverResult c_vector_baseline = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_baseline_invocation);
    BUSTER_TEST(arguments, c_vector_baseline.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.split_vector_operation_count == 9);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.vzeroupper_count == 0);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.forwarded_wide_vector_load_count == 0);
    u64 c_vector_baseline_native_operations = c_vector_baseline.codegen_statistics.native_vector_operation_count;
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    String8 c_vector_avx2_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-avx2"), S8(".o"));
    String8 c_vector_avx2_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=haswell"), S8("-o"), c_vector_avx2_path, S8("tests/basic_c_vector.c"),
    };
    c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_avx2_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_avx2_command_line));
    CompilerDriverResult c_vector_avx2 = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_avx2_invocation);
    BUSTER_TEST(arguments, c_vector_avx2.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.split_vector_operation_count == 7);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.native_vector_operation_count == c_vector_baseline_native_operations + 2);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.vzeroupper_count == 1);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.forwarded_wide_vector_load_count == 1);
    u64 c_vector_avx2_native_operations = c_vector_avx2.codegen_statistics.native_vector_operation_count;
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    String8 c_vector_avx512_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-avx512"), S8(".o"));
    String8 c_vector_avx512_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=znver5"), S8("-o"), c_vector_avx512_path, S8("tests/basic_c_vector.c"),
    };
    c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_avx512_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_avx512_command_line));
    CompilerDriverResult c_vector_avx512 = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_avx512_invocation);
    BUSTER_TEST(arguments, c_vector_avx512.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.split_vector_operation_count == 0);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.native_vector_operation_count == c_vector_avx2_native_operations + 7);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.vzeroupper_count == 5);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.forwarded_wide_vector_load_count == 4);
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    if (c_vector.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_vector_arguments[] = {
            c_vector_path,
        };
        ProcessSpawnResult c_vector_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_arguments), (SliceString8){0}, (SliceString8){0},
                                                             (ProcessSpawnOptions){
                                                                 .use_process_environment = true,
                                                             });
        BUSTER_TEST(arguments, c_vector_spawn.handle != 0);
        if (c_vector_spawn.handle)
        {
            ProcessWaitResult c_vector_wait = os_process_wait_sync(arguments->arena, c_vector_spawn);
            BUSTER_TEST(arguments, c_vector_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vector_cross_targets[] = {
        S8("x86_64-unknown-linux-gnu"),  S8("x86_64-pc-windows-msvc"),  S8("x86_64-apple-macos"),  S8("x86_64-linux-android"),  S8("x86_64-apple-ios"),
        S8("aarch64-unknown-linux-gnu"), S8("aarch64-pc-windows-msvc"), S8("aarch64-apple-macos"), S8("aarch64-linux-android"), S8("aarch64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_vector_cross_targets); target_index += 1)
    {
        String8 c_vector_cross_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-vector-cross"), string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_vector_cross_command_line[] = {
            S8("-c"), S8("-target"), c_vector_cross_targets[target_index], S8("-o"), c_vector_cross_path, S8("tests/basic_c_vector.c"),
        };
        CompilerDriverResult c_vector_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_cross_command_line)));
        BUSTER_TEST(arguments, c_vector_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_vector_cross.has_object);
    }
    // The 512-bit vocabulary. The fixture is self-contained and guards itself
    // on the predefined feature macros, so it builds for every target and
    // compiles its body out where the vocabulary is unavailable; that is what
    // lets the cross-target loop below cover it on a host whose sysroot has
    // nobody else's libc in it.
    String8 c_simd_path = buster_test_temporary_path(arguments->arena, S8("buster-c-simd"),
#if BUSTER_WINDOWS
                                                     S8(".exe"));
#else
                                                     S8(""));
#endif
    String8 c_simd_command_line[] = {
        S8("-o"),
        c_simd_path,
        S8("tests/basic_c_simd.c"),
    };
    CompilerDriverResult c_simd = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_simd_command_line)));
    BUSTER_TEST(arguments, c_simd.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_simd.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_simd_arguments[] = {
            c_simd_path,
        };
        ProcessSpawnResult c_simd_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_simd_arguments), (SliceString8){0}, (SliceString8){0},
                                                           (ProcessSpawnOptions){
                                                               .use_process_environment = true,
                                                           });
        BUSTER_TEST(arguments, c_simd_spawn.handle != 0);
        if (c_simd_spawn.handle)
        {
            ProcessWaitResult c_simd_wait = os_process_wait_sync(arguments->arena, c_simd_spawn);
            BUSTER_TEST(arguments, c_simd_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // znver5 has every feature the vocabulary needs, so this build must go
    // through the builtins; baseline has none of them and must emit no SIMD
    // instruction at all. Without the statistic a fixture that quietly guarded
    // itself out everywhere would still pass and cover nothing.
    String8 c_simd_avx512_path = buster_test_temporary_path(arguments->arena, S8("buster-c-simd-avx512"), S8(".o"));
    String8 c_simd_avx512_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=znver5"), S8("-o"), c_simd_avx512_path, S8("tests/basic_c_simd.c"),
    };
    Arena* c_simd_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverResult c_simd_avx512 = compiler_driver_execute_invocation(
        c_simd_target_arena, compiler_driver_parse_arguments(c_simd_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_simd_avx512_command_line)));
    BUSTER_TEST(arguments, c_simd_avx512.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_simd_avx512.codegen_statistics.simd_operation_count > 0);
    BUSTER_TEST(arguments, arena_destroy(c_simd_target_arena, 1));
    String8 c_simd_baseline_path = buster_test_temporary_path(arguments->arena, S8("buster-c-simd-baseline"), S8(".o"));
    String8 c_simd_baseline_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-o"), c_simd_baseline_path, S8("tests/basic_c_simd.c"),
    };
    c_simd_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverResult c_simd_baseline = compiler_driver_execute_invocation(
        c_simd_target_arena, compiler_driver_parse_arguments(c_simd_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_simd_baseline_command_line)));
    BUSTER_TEST(arguments, c_simd_baseline.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_simd_baseline.codegen_statistics.simd_operation_count == 0);
    BUSTER_TEST(arguments, arena_destroy(c_simd_target_arena, 1));
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_vector_cross_targets); target_index += 1)
    {
        String8 c_simd_cross_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-simd-cross"), string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_simd_cross_command_line[] = {
            S8("-c"), S8("-target"), c_vector_cross_targets[target_index], S8("-o"), c_simd_cross_path, S8("tests/basic_c_simd.c"),
        };
        CompilerDriverResult c_simd_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_simd_cross_command_line)));
        BUSTER_TEST(arguments, c_simd_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_simd_cross.has_object);
    }

    String8 c_conversions_path = buster_test_temporary_path(arguments->arena, S8("buster-c-conversions"), S8(""));
    String8 c_conversions_command_line[] = {
        S8("-o"),
        c_conversions_path,
        S8("tests/basic_c_conversions.c"),
    };
    CompilerDriverResult c_conversions = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_command_line)));
    BUSTER_TEST(arguments, c_conversions.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_conversions.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_conversions_arguments[] = {
            c_conversions_path,
        };
        ProcessSpawnResult c_conversions_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_conversions_spawn.handle != 0);
        if (c_conversions_spawn.handle)
        {
            ProcessWaitResult c_conversions_wait = os_process_wait_sync(arguments->arena, c_conversions_spawn);
            BUSTER_TEST(arguments, c_conversions_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Lazily evaluated operands are only observable at run time -- the wrong
    // lowering still selects the right value -- so this fixture has to run,
    // not merely compile.
    String8 c_conditional_operand_path = buster_test_temporary_path(arguments->arena, S8("buster-c-conditional-operand"), S8(""));
    String8 c_conditional_operand_command_line[] = {
        S8("-o"),
        c_conditional_operand_path,
        S8("tests/basic_c_conditional_operand.c"),
    };
    CompilerDriverResult c_conditional_operand = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conditional_operand_command_line)));
    BUSTER_TEST(arguments, c_conditional_operand.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_conditional_operand.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_conditional_operand_arguments[] = {
            c_conditional_operand_path,
        };
        ProcessSpawnResult c_conditional_operand_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_conditional_operand_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_conditional_operand_spawn.handle != 0);
        if (c_conditional_operand_spawn.handle)
        {
            ProcessWaitResult c_conditional_operand_wait = os_process_wait_sync(arguments->arena, c_conditional_operand_spawn);
            BUSTER_TEST(arguments, c_conditional_operand_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Unevaluated sizeof/_Alignof operands and enum constants over object
    // sizeofs are both only observable at run time -- the wrong lowering
    // still produces plausible constants -- so these fixtures run.
    String8 c_runtime_fixture_paths[] = {
        S8("tests/basic_c_sizeof_unevaluated.c"),
    };
    String8 c_runtime_fixture_names[] = {
        S8("buster-c-sizeof-unevaluated"),
    };
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_runtime_fixture_paths); fixture_index += 1)
    {
        String8 c_runtime_fixture_path = buster_test_temporary_path(arguments->arena, c_runtime_fixture_names[fixture_index], S8(""));
        String8 c_runtime_fixture_command_line[] = {
            S8("-o"),
            c_runtime_fixture_path,
            c_runtime_fixture_paths[fixture_index],
        };
        CompilerDriverResult c_runtime_fixture = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_runtime_fixture_command_line)));
        BUSTER_TEST(arguments, c_runtime_fixture.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_runtime_fixture.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 c_runtime_fixture_arguments[] = {
                c_runtime_fixture_path,
            };
            ProcessSpawnResult c_runtime_fixture_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_runtime_fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, c_runtime_fixture_spawn.handle != 0);
            if (c_runtime_fixture_spawn.handle)
            {
                ProcessWaitResult c_runtime_fixture_wait = os_process_wait_sync(arguments->arena, c_runtime_fixture_spawn);
                BUSTER_TEST(arguments, c_runtime_fixture_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    // The calling-convention fixture runs natively, so the host's own ABI and
    // register allocator are executed rather than only inspected: on a
    // Windows runner that is the Win64 positional register assignment, the
    // shadow space below the outgoing stack arguments, and the callee-saved
    // file the allocator binds across the calls inside it.
    String8 c_call_abi_path = buster_test_temporary_path(arguments->arena, S8("buster-c-call-abi"), S8(""));
    String8 c_call_abi_command_line[] = {
        S8("-o"),
        c_call_abi_path,
        S8("tests/basic_c_call_abi.c"),
    };
    CompilerDriverResult c_call_abi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_call_abi_command_line)));
    BUSTER_TEST(arguments, c_call_abi.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_call_abi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, c_call_abi.codegen_statistics.fallback_function_count < c_call_abi.codegen_statistics.function_count);
        String8 c_call_abi_arguments[] = {
            c_call_abi_path,
        };
        ProcessSpawnResult c_call_abi_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_call_abi_arguments), (SliceString8){0}, (SliceString8){0},
                                                               (ProcessSpawnOptions){
                                                                   .use_process_environment = true,
                                                               });
        BUSTER_TEST(arguments, c_call_abi_spawn.handle != 0);
        if (c_call_abi_spawn.handle)
        {
            ProcessWaitResult c_call_abi_wait = os_process_wait_sync(arguments->arena, c_call_abi_spawn);
            BUSTER_TEST(arguments, c_call_abi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 conversion_cross_targets[] = {
        S8("aarch64-unknown-linux-gnu"),
        S8("x86_64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(conversion_cross_targets); target_index += 1)
    {
        String8 c_conversions_cross_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-conversions-cross"), string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_conversions_cross_command_line[] = {
            S8("-c"), S8("-target"), conversion_cross_targets[target_index], S8("-o"), c_conversions_cross_path, S8("tests/basic_c_conversions.c"),
        };
        CompilerDriverResult c_conversions_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_cross_command_line)));
        BUSTER_TEST(arguments, c_conversions_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_conversions_cross.has_object);
    }
    String8 c_float_abi_aarch64_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi-aarch64"), S8(""));
    String8 c_float_abi_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_float_abi_aarch64_path, S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_aarch64_command_line)));
    BUSTER_TEST(arguments, c_float_abi_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        static u8 const aarch64_float_add[] = {
            0x00,
            0x28,
            0x61,
            0x1e,
        };
        ByteSlice executable = c_float_abi_aarch64.native_link.executable;
        bool found_float_add = false;
        for (u64 byte_index = 0; byte_index + sizeof(aarch64_float_add) <= executable.length; byte_index += 1)
        {
            if (memcmp(executable.pointer + byte_index, aarch64_float_add, sizeof(aarch64_float_add)) == 0)
            {
                found_float_add = true;
                break;
            }
        }
        BUSTER_TEST(arguments, found_float_add);
    }
    // The assertions below are the canonical Windows frame contract: one
    // fixed prologue allocation that also covers the outgoing argument area,
    // and no stack adjustment anywhere in the body. The machine path pushes
    // its outgoing arguments instead — sound under the frame register its
    // unwind data establishes, but a different shape — so this block names
    // the canonical emitter explicitly and the machine modes are covered by
    // their own block below.
    String8 c_float_abi_windows_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi-windows"), S8(".obj"));
    String8 c_float_abi_windows_command_line[] = {
        S8("-c"),
        S8("-target"),
        S8("x86_64-pc-windows-msvc"),
        S8("-fno-register-allocator"),
        S8("-o"),
        c_float_abi_windows_path,
        S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi_windows = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_windows_command_line)));
    BUSTER_TEST(arguments, c_float_abi_windows.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi_windows.error == COMPILER_DRIVER_ERROR_NONE)
    {
        static u8 const load_xmm0[] = {
            0xf2,
            0x0f,
            0x10,
            0x85,
        };
        static u8 const load_indirect_second_part[] = {
            0x48,
            0x8b,
            0x41,
            0x08,
        };
        ByteSlice text = c_float_abi_windows.object.sections[OBJECT_SECTION_TEXT].data;
        ByteSlice xdata = c_float_abi_windows.object.sections[OBJECT_SECTION_WINDOWS_XDATA].data;
        u32 text_function_count = 0;
        u32 pdata_relocation_count = 0;
        bool found_fixed_outgoing_frame = false;
        bool found_call_with_fixed_frame = false;
        bool found_single_fixed_allocation = false;
        bool fixed_frame_alignment_valid = true;
        bool body_stack_adjust_valid = true;
        bool full_body_decode_valid = true;
        bool full_body_stack_store_bounds_valid = true;
        bool unwind_matches_frame = true;
        bool unwind_allocation_count_matches = true;
        bool found_load_xmm0 = false;
        bool found_indirect_second_part = false;
        for (u32 relocation_index = 0; relocation_index < c_float_abi_windows.object.relocation_count; relocation_index += 1)
        {
            ObjectRelocation* relocation = c_float_abi_windows.object.relocations + relocation_index;
            pdata_relocation_count += relocation->section == OBJECT_SECTION_WINDOWS_PDATA;
        }
        for (u32 symbol_index = 0; symbol_index < c_float_abi_windows.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = c_float_abi_windows.object.symbols + symbol_index;
            if (symbol->kind != OBJECT_SYMBOL_FUNCTION || symbol->section != OBJECT_SECTION_TEXT)
            {
                continue;
            }
            text_function_count += 1;
            u64 function_start = symbol->value;
            u64 function_end = function_start + symbol->size;
            if (function_end > text.length || symbol->size < 8 || memcmp(text.pointer + function_start, (u8[]){0x55, 0x48, 0x89, 0xe5}, 4) != 0)
            {
                unwind_matches_frame = false;
                continue;
            }
            u64 cursor = function_start + 4;
            u32 prolog_stack_bytes = 0;
            u32 prolog_stack_adjust_count = 0;
            if (cursor + 4 <= function_end && text.pointer[cursor] == 0x48 && text.pointer[cursor + 1] == 0x83 &&
                text.pointer[cursor + 2] == 0xec)
            {
                prolog_stack_bytes = text.pointer[cursor + 3];
                prolog_stack_adjust_count += 1;
                cursor += 4;
            }
            else if (cursor + 42 <= function_end && text.pointer[cursor] == 0x49 && text.pointer[cursor + 1] == 0x89 &&
                     text.pointer[cursor + 2] == 0xe2 && text.pointer[cursor + 3] == 0x49 && text.pointer[cursor + 4] == 0x81 &&
                     text.pointer[cursor + 5] == 0xea && text.pointer[cursor + 10] == 0x49 && text.pointer[cursor + 11] == 0x89 &&
                     text.pointer[cursor + 12] == 0xe3 && text.pointer[cursor + 13] == 0x49 && text.pointer[cursor + 14] == 0x81 &&
                     text.pointer[cursor + 15] == 0xeb && text.pointer[cursor + 20] == 0x4d && text.pointer[cursor + 21] == 0x39 &&
                     text.pointer[cursor + 22] == 0xd3 && text.pointer[cursor + 23] == 0x76 && text.pointer[cursor + 25] == 0x41 &&
                     text.pointer[cursor + 26] == 0xf6 && text.pointer[cursor + 27] == 0x03 && text.pointer[cursor + 28] == 0 &&
                     text.pointer[cursor + 29] == 0xeb && text.pointer[cursor + 31] == 0x41 && text.pointer[cursor + 32] == 0xf6 &&
                     text.pointer[cursor + 33] == 0x02 && text.pointer[cursor + 34] == 0 && text.pointer[cursor + 35] == 0x48 &&
                     text.pointer[cursor + 36] == 0x81 && text.pointer[cursor + 37] == 0xec)
            {
                memcpy(&prolog_stack_bytes, text.pointer + cursor + 38, sizeof(prolog_stack_bytes));
                prolog_stack_adjust_count += 1;
                cursor += 42;
            }
            else if (cursor + 7 <= function_end && text.pointer[cursor] == 0x48 && text.pointer[cursor + 1] == 0x81 &&
                     text.pointer[cursor + 2] == 0xec)
            {
                memcpy(&prolog_stack_bytes, text.pointer + cursor + 3, sizeof(prolog_stack_bytes));
                prolog_stack_adjust_count += 1;
                cursor += 7;
            }
            fixed_frame_alignment_valid &= prolog_stack_adjust_count == 0 || (prolog_stack_bytes != 0 && !(prolog_stack_bytes & 15));
            if (prolog_stack_adjust_count > 1)
            {
                unwind_matches_frame = false;
            }
            if (cursor + 4 <= function_end && text.pointer[cursor] == 0xf6 && text.pointer[cursor + 1] == 0x04 &&
                text.pointer[cursor + 2] == 0x24 && text.pointer[cursor + 3] == 0)
            {
                cursor += 4;
            }
            CodegenTestX64BodyScan full_body_scan = codegen_test_x64_scan_body(text, cursor, function_end, prolog_stack_bytes, UINT32_MAX);
            full_body_decode_valid &= full_body_scan.valid;
            full_body_stack_store_bounds_valid &= full_body_scan.valid &&
                                                   (!full_body_scan.has_stack_store || full_body_scan.maximum_stack_store_end <= prolog_stack_bytes);
            body_stack_adjust_valid &= full_body_scan.valid;
            bool has_outgoing_stack_store = false;
            u32 maximum_stack_store_end = 0;
            for (u64 byte_index = cursor; byte_index + 4 <= function_end; byte_index += 1)
            {
                u8 rex = text.pointer[byte_index];
                u8 modrm = text.pointer[byte_index + 2];
                u8 mod = modrm >> 6;
                u32 displacement_size = mod == 1 ? 1 : mod == 2 ? 4 : 0;
                if ((rex == 0x48 || rex == 0x4c) && text.pointer[byte_index + 1] == 0x89 && mod != 3 && (modrm & 7) == 4 &&
                    text.pointer[byte_index + 3] == 0x24 && displacement_size && byte_index + 4 + displacement_size <= function_end)
                {
                    u32 displacement = 0;
                    if (displacement_size == 1)
                    {
                        displacement = text.pointer[byte_index + 4];
                    }
                    else
                    {
                        memcpy(&displacement, text.pointer + byte_index + 4, sizeof(displacement));
                    }
                    if (displacement <= UINT32_MAX - 8)
                    {
                        has_outgoing_stack_store = true;
                        maximum_stack_store_end = BUSTER_MAX(maximum_stack_store_end, displacement + 8);
                    }
                }
            }
            bool has_direct_call = false;
            bool function_body_stack_adjust_valid = true;
            for (u32 relocation_index = 0; relocation_index < c_float_abi_windows.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = c_float_abi_windows.object.relocations + relocation_index;
                if (relocation->section != OBJECT_SECTION_TEXT || relocation->kind != OBJECT_RELOCATION_X86_64_PC32 || relocation->offset < function_start + 5 ||
                    relocation->offset > function_end - 4 || text.pointer[relocation->offset - 1] != 0xe8)
                {
                    continue;
                }
                has_direct_call = true;
                u64 call_start = relocation->offset - 1;
                if ((call_start >= function_start + 4 && text.pointer[call_start - 4] == 0x48 && text.pointer[call_start - 3] == 0x83 &&
                     text.pointer[call_start - 2] == 0xec) ||
                    (call_start >= function_start + 7 && text.pointer[call_start - 7] == 0x48 && text.pointer[call_start - 6] == 0x81 &&
                     text.pointer[call_start - 5] == 0xec))
                {
                    function_body_stack_adjust_valid = false;
                }
                u64 call_end = relocation->offset + 4;
                if ((call_end + 4 <= function_end && text.pointer[call_end] == 0x48 && text.pointer[call_end + 1] == 0x83 &&
                     text.pointer[call_end + 2] == 0xc4) ||
                    (call_end + 7 <= function_end && text.pointer[call_end] == 0x48 && text.pointer[call_end + 1] == 0x81 &&
                     text.pointer[call_end + 2] == 0xc4))
                {
                    function_body_stack_adjust_valid = false;
                }
            }
            body_stack_adjust_valid &= function_body_stack_adjust_valid;
            found_fixed_outgoing_frame |= has_direct_call && has_outgoing_stack_store && prolog_stack_adjust_count == 1 &&
                                         prolog_stack_bytes >= maximum_stack_store_end;
            found_call_with_fixed_frame |= has_direct_call && prolog_stack_adjust_count == 1;
            found_single_fixed_allocation |= has_direct_call && prolog_stack_adjust_count == 1 && function_body_stack_adjust_valid;
            u64 xdata_offset = UINT64_MAX;
            for (u32 relocation_index = 0; relocation_index < c_float_abi_windows.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = c_float_abi_windows.object.relocations + relocation_index;
                if (relocation->section == OBJECT_SECTION_WINDOWS_PDATA && relocation->offset == (u64)(text_function_count - 1) * 12 + 8)
                {
                    xdata_offset = relocation->addend >= 0 ? (u64)relocation->addend : UINT64_MAX;
                    break;
                }
            }
            if (xdata_offset == UINT64_MAX || xdata_offset + 4 > xdata.length)
            {
                unwind_matches_frame = false;
                continue;
            }
            u8 code_slots = xdata.pointer[xdata_offset + 2];
            if (xdata_offset + 4 + (u64)code_slots * 2 > xdata.length)
            {
                unwind_matches_frame = false;
                continue;
            }
            u32 unwind_stack_bytes = 0;
            u32 unwind_stack_adjust_count = 0;
            for (u32 code_index = 0; code_index < code_slots;)
            {
                u8 unwind = xdata.pointer[xdata_offset + 5 + code_index * 2];
                u8 operation = unwind & 15;
                u8 information = unwind >> 4;
                if (operation == 1 && information == 0 && code_index + 1 < code_slots)
                {
                    u16 scaled = 0;
                    memcpy(&scaled, xdata.pointer + xdata_offset + 6 + code_index * 2, sizeof(scaled));
                    if (unwind_stack_bytes > UINT32_MAX - (u32)scaled * 8)
                    {
                        unwind_matches_frame = false;
                        break;
                    }
                    unwind_stack_bytes += (u32)scaled * 8;
                    unwind_stack_adjust_count += 1;
                    code_index += 2;
                }
                else if (operation == 1 && information == 1 && code_index + 2 < code_slots)
                {
                    u32 large = 0;
                    memcpy(&large, xdata.pointer + xdata_offset + 6 + code_index * 2, sizeof(large));
                    if (unwind_stack_bytes > UINT32_MAX - large)
                    {
                        unwind_matches_frame = false;
                        break;
                    }
                    unwind_stack_bytes += large;
                    unwind_stack_adjust_count += 1;
                    code_index += 3;
                }
                else if (operation == 2)
                {
                    unwind_stack_bytes += ((u32)information + 1) * 8;
                    unwind_stack_adjust_count += 1;
                    code_index += 1;
                }
                else
                {
                    code_index += 1;
                }
            }
            unwind_matches_frame &= unwind_stack_bytes == prolog_stack_bytes;
            unwind_allocation_count_matches &= unwind_stack_adjust_count == prolog_stack_adjust_count;
        }
        for (u64 byte_index = 0; byte_index < text.length; byte_index += 1)
        {
            if (byte_index + sizeof(load_xmm0) <= text.length && memcmp(text.pointer + byte_index, load_xmm0, sizeof(load_xmm0)) == 0)
            {
                found_load_xmm0 = true;
            }
            if (byte_index + sizeof(load_indirect_second_part) <= text.length &&
                memcmp(text.pointer + byte_index, load_indirect_second_part, sizeof(load_indirect_second_part)) == 0)
            {
                found_indirect_second_part = true;
            }
        }
        BUSTER_TEST(arguments, found_fixed_outgoing_frame);
        BUSTER_TEST(arguments, found_call_with_fixed_frame);
        BUSTER_TEST(arguments, found_single_fixed_allocation);
        BUSTER_TEST(arguments, fixed_frame_alignment_valid);
        BUSTER_TEST(arguments, body_stack_adjust_valid);
        BUSTER_TEST(arguments, full_body_decode_valid);
        BUSTER_TEST(arguments, full_body_stack_store_bounds_valid);
        BUSTER_TEST(arguments, text_function_count != 0);
        BUSTER_TEST(arguments, pdata_relocation_count == text_function_count * 3);
        BUSTER_TEST(arguments, c_float_abi_windows.object.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length == (u64)text_function_count * 12);
        BUSTER_TEST(arguments, c_float_abi_windows.object.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length != 0);
        BUSTER_TEST(arguments, unwind_matches_frame);
        BUSTER_TEST(arguments, unwind_allocation_count_matches);
        BUSTER_TEST(arguments, found_load_xmm0);
        BUSTER_TEST(arguments, found_indirect_second_part);
    }
    // The machine register allocators on Win64: every mode must select part
    // of the fixture, and the unwind data it writes for those functions must
    // decode as a well-formed record — the machine prologue pushes RSI and
    // RDI beside the System V file, and its page-chunked allocation emits one
    // action per chunk where the canonical Windows prologue emits one for the
    // whole frame. The record reader is x86-64-only, like every other caller
    // of it, so the block follows it behind the same guard.
#if BUSTER_CPU_ARCH_X86_64
    String8 windows_machine_modes[] = {
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=quality"),
    };
    String8 windows_machine_sources[] = {
        S8("tests/basic_c_float_abi.c"),
        S8("tests/basic_c_call_abi.c"),
    };
    for (u32 windows_machine_source = 0; windows_machine_source < BUSTER_ARRAY_LENGTH(windows_machine_sources); windows_machine_source += 1)
    {
        for (u32 windows_machine_index = 0; windows_machine_index < BUSTER_ARRAY_LENGTH(windows_machine_modes); windows_machine_index += 1)
        {
            TemporalArena windows_machine_temporary = scratch_begin(&arguments->arena, 1);
            String8 windows_machine_path = buster_test_temporary_path(windows_machine_temporary.arena, S8("buster-c-windows-machine"), S8(".obj"));
            String8 windows_machine_command_line[] = {
                S8("-c"),
                S8("-target"),
                S8("x86_64-pc-windows-msvc"),
                windows_machine_modes[windows_machine_index],
                S8("-o"),
                windows_machine_path,
                windows_machine_sources[windows_machine_source],
            };
            CompilerDriverResult windows_machine = compiler_driver_execute_invocation(
                windows_machine_temporary.arena,
                compiler_driver_parse_arguments(windows_machine_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_machine_command_line)));
            BUSTER_TEST(arguments, windows_machine.error == COMPILER_DRIVER_ERROR_NONE);
            if (windows_machine.error == COMPILER_DRIVER_ERROR_NONE)
            {
                BUSTER_TEST(arguments, windows_machine.has_object);
                BUSTER_TEST(arguments, windows_machine.codegen_statistics.function_count != 0);
                BUSTER_TEST(arguments, windows_machine.codegen_statistics.fallback_function_count < windows_machine.codegen_statistics.function_count);
                if (windows_machine.has_object)
                {
                    // A frame register is not asserted: Win64 can only record one
                    // for a frame of at most 240 bytes, and the machine path does
                    // not need it. Its calls write the outgoing area in the frame
                    // instead of pushing, so the stack pointer never moves inside
                    // the body and the push and allocation codes alone describe
                    // every instruction.
                    bool windows_machine_frame_register = false;
                    BUSTER_TEST(arguments, compiler_driver_test_windows_x64_unwind_records(&windows_machine.object, &windows_machine_frame_register));
                    BUSTER_TEST(arguments, windows_machine.object.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length != 0);
                    BUSTER_TEST(arguments, windows_machine.object.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length != 0);
                }
            }
            scratch_end(windows_machine_temporary);
        }
    }
#endif
    Arena* c_asm_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_asm_temporary = scratch_begin(c_asm_conflicts, BUSTER_ARRAY_LENGTH(c_asm_conflicts));
    Arena* c_asm_arena = c_asm_temporary.arena;
    String8 c_asm_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm"), S8(""));
    String8 c_asm_command_line[] = {
        S8("-o"),
        c_asm_path,
        S8("tests/basic_c_asm.c"),
    };
    CompilerDriverResult c_asm =
        compiler_driver_execute_invocation(c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_command_line)));
    BUSTER_TEST(arguments, c_asm.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_asm.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_asm_arguments[] = {
            c_asm_path,
        };
        ProcessSpawnResult c_asm_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .use_process_environment = true,
                                                          });
        BUSTER_TEST(arguments, c_asm_spawn.handle != 0);
        if (c_asm_spawn.handle)
        {
            ProcessWaitResult c_asm_wait = os_process_wait_sync(c_asm_arena, c_asm_spawn);
            BUSTER_TEST(arguments, c_asm_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // The ordinary-template path is deliberately checked in both GNU
    // dialects.  The fixture has fixed, generic, tied/read-write, named, and
    // 8/16/32/64-bit operands; running each executable catches stale input
    // loads as well as incorrect physical-register width spelling.
    {
        String8 default_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm-default"), S8(""));
        String8 default_command_line[] = {S8("-o"), default_path, S8("tests/basic_c_inline_asm.c")};
        CompilerDriverResult default_dialect = compiler_driver_execute_invocation(
            c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(default_command_line)));
        BUSTER_TEST(arguments, default_dialect.error == COMPILER_DRIVER_ERROR_NONE);
        if (default_dialect.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 run_arguments[] = {default_path};
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                        (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(c_asm_arena, spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
#if BUSTER_CPU_ARCH_X86_64
        String8 dialects[] = {S8("att"), S8("intel")};
        for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialects); dialect_index += 1)
        {
            String8 dialect_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm-dialect"),
                                                               string_format(c_asm_arena, S8("-{u32}"), dialect_index));
            String8 dialect_command_line[] = {
                S8("-masm"), dialects[dialect_index], S8("-o"), dialect_path, S8("tests/basic_c_inline_asm.c"),
            };
            CompilerDriverResult dialect = compiler_driver_execute_invocation(
                c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(dialect_command_line)));
            BUSTER_TEST(arguments, dialect.error == COMPILER_DRIVER_ERROR_NONE);
            if (dialect.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {dialect_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(c_asm_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
#endif
    }
    String8 c_asm_aarch64_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm-aarch64"), S8(""));
    String8 c_asm_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_asm_aarch64_path, S8("tests/basic_c_asm.c"),
    };
    CompilerDriverResult c_asm_aarch64 = compiler_driver_execute_invocation(
        c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_aarch64_command_line)));
    BUSTER_TEST(arguments, c_asm_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_asm_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, compiler_driver_bytes_contain(c_asm_aarch64.native_link.executable, (String8){
                                                                                                       .pointer =
                                                                                                           (char8*)(u8[]){
                                                                                                               0x1f,
                                                                                                               0x20,
                                                                                                               0x03,
                                                                                                               0xd5,
                                                                                                           },
                                                                                                       .length = 4,
                                                                                                   }));
        BUSTER_TEST(arguments, compiler_driver_test_aarch64_tied_input_load(&c_asm_aarch64.object));
    }
    String8 c_asm_windows_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm-windows"), S8(".obj"));
    String8 c_asm_windows_command_line[] = {
        S8("-c"), S8("-g0"), S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-o"), c_asm_windows_path, S8("tests/basic_c_asm.c"),
    };
    CompilerDriverResult c_asm_windows = compiler_driver_execute_invocation(
        c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_windows_command_line)));
    BUSTER_TEST(arguments, c_asm_windows.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_asm_windows.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, c_asm_windows.has_object);
        BUSTER_TEST(arguments, compiler_driver_test_windows_x64_dynamic_rbx(&c_asm_windows.object));
    }
    scratch_end(c_asm_temporary);
#endif
    Arena* c_multi_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_multi_temporary = scratch_begin(c_multi_conflicts, BUSTER_ARRAY_LENGTH(c_multi_conflicts));
    Arena* c_multi_arena = c_multi_temporary.arena;
    String8 c_multi_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-multi-driver"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 c_multi_command_line[] = {
        S8("-o"),
        c_multi_path,
        S8("tests/basic_c_multi_main.c"),
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult c_multi = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_command_line)));
    BUSTER_TEST(arguments, c_multi.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_multi.has_object);
    if (c_multi.has_object)
    {
        BUSTER_TEST(arguments, c_multi.object.section_count != 0 && c_multi.object.symbol_count != 0);
    }
    if (c_multi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_multi_arguments[] = {
            c_multi_path,
        };
        ProcessSpawnResult c_multi_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_multi_spawn.handle != 0);
        if (c_multi_spawn.handle)
        {
            ProcessWaitResult c_multi_wait = os_process_wait_sync(c_multi_arena, c_multi_spawn);
            BUSTER_TEST(arguments, c_multi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_multi_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-multi-object"),
#if BUSTER_WINDOWS
                                                             S8(".obj"));
#else
                                                             S8(".o"));
#endif
    String8 c_multi_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_multi_object_path,
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult c_multi_object = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_object_command_line)));
    BUSTER_TEST(arguments, c_multi_object.error == COMPILER_DRIVER_ERROR_NONE);
    String8 c_mixed_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-mixed-driver"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 c_mixed_command_line[] = {
        S8("-o"),
        c_mixed_path,
        S8("tests/basic_c_multi_main.c"),
        c_multi_object_path,
    };
    CompilerDriverResult c_mixed = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_mixed_command_line)));
    BUSTER_TEST(arguments, c_mixed.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_mixed.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_mixed_arguments[] = {
            c_mixed_path,
        };
        ProcessSpawnResult c_mixed_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_mixed_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_mixed_spawn.handle != 0);
        if (c_mixed_spawn.handle)
        {
            ProcessWaitResult c_mixed_wait = os_process_wait_sync(c_multi_arena, c_mixed_spawn);
            BUSTER_TEST(arguments, c_mixed_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_archive_bias_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-bias"),
#if BUSTER_WINDOWS
                                                                    S8(".obj"));
#else
                                                                    S8(".o"));
#endif
    String8 c_archive_add_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-add"),
#if BUSTER_WINDOWS
                                                                   S8(".obj"));
#else
                                                                   S8(".o"));
#endif
    String8 c_archive_bias_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_archive_bias_object_path,
        S8("tests/basic_c_archive_bias.c"),
    };
    String8 c_archive_add_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_archive_add_object_path,
        S8("tests/basic_c_archive_add.c"),
    };
    CompilerDriverResult c_archive_bias = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_bias_command_line)));
    CompilerDriverResult c_archive_add = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_add_command_line)));
    BUSTER_TEST(arguments, c_archive_bias.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_archive_add.error == COMPILER_DRIVER_ERROR_NONE);
    FileMapRead c_archive_bias_map = file_map_read(c_multi_arena, c_archive_bias_object_path, (FileReadOptions){0});
    FileMapRead c_archive_add_map = file_map_read(c_multi_arena, c_archive_add_object_path, (FileReadOptions){0});
    ByteSlice c_archive_members[] = {
        c_archive_bias_map.bytes,
        c_archive_add_map.bytes,
    };
    String8 c_archive_names[] = {
        S8("bias.o"),
        S8("add.o"),
    };
    ByteSlice c_archive_bytes = compiler_driver_test_archive(c_multi_arena, c_archive_members, c_archive_names, BUSTER_ARRAY_LENGTH(c_archive_members));
    file_map_unmap(c_archive_bias_map);
    file_map_unmap(c_archive_add_map);
    String8 c_archive_directory = buster_test_temporary_path(c_multi_arena, S8("buster-c-driver-archive"), S8(""));
    os_make_directory(c_archive_directory);
    String8 c_archive_path = string_format_z(c_multi_arena,
#if BUSTER_WINDOWS
                                             S8("{S8}/archive_chain.lib"),
#else
                                             S8("{S8}/libarchive_chain.a"),
#endif
                                             c_archive_directory);
    BUSTER_TEST(arguments, file_write(c_archive_path, c_archive_bytes));
    String8 c_archive_executable_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-driver"),
#if BUSTER_WINDOWS
                                                                   S8(".exe"));
#else
                                                                   S8(""));
#endif
    String8 c_archive_command_line[] = {
        S8("-o"), c_archive_executable_path, S8("tests/basic_c_multi_main.c"), S8("-L"), c_archive_directory, S8("-larchive_chain"),
    };
    CompilerDriverResult c_archive = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_command_line)));
    BUSTER_TEST(arguments, c_archive.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_archive.has_object);
    if (c_archive.has_object)
    {
        BUSTER_TEST(arguments, c_archive.object.section_count != 0 && c_archive.object.symbol_count != 0);
    }
    if (c_archive.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_archive_arguments[] = {
            c_archive_executable_path,
        };
        ProcessSpawnResult c_archive_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_archive_spawn.handle != 0);
        if (c_archive_spawn.handle)
        {
            ProcessWaitResult c_archive_wait = os_process_wait_sync(c_multi_arena, c_archive_spawn);
            BUSTER_TEST(arguments, c_archive_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(c_multi_temporary);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
#endif
