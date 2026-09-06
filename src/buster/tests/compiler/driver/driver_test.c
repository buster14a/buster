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
        if (string_offset <= image.length && string_size <= image.length - string_offset)
        {
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
    BUSTER_UNUSED_DECL u64 result;
    if (!compiler_driver_test_elf_section_find(image, name, &offset, &size, &address))
    {
        result = 0;
    }
    else
    {
        result = address;
    }

    return result;
}

// The function the one R_X86_64_64 in `.rela<section>` registers, for an
// initializer array section.  A relocated slot carries no name of its own, so
// naming its symbol is the only way to prove a `.init_array.NNNNN` group holds
// the constructor whose priority it is spelled with rather than merely
// existing; the slot offset has to be zero because the group was rebased to
// the front of its own section.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL String8 compiler_driver_test_elf_initializer_symbol(Arena* arena, ByteSlice image, String8 section)
{
    enum
    {
        ELF_SYMBOL_SIZE = 24,
        ELF_RELOCATION_SIZE = 24,
        ELF_RELOCATION_X86_64_64 = 1,
    };
    String8 result = {0};
    ByteSlice relocations = compiler_driver_test_elf_section(image, string_format(arena, S8(".rela{S8}"), section));
    ByteSlice symbols = compiler_driver_test_elf_section(image, S8(".symtab"));
    ByteSlice strings = compiler_driver_test_elf_section(image, S8(".strtab"));
    if (relocations.length == ELF_RELOCATION_SIZE && symbols.length && strings.length)
    {
        u64 offset;
        u64 information;
        memcpy(&offset, relocations.pointer, sizeof(offset));
        memcpy(&information, relocations.pointer + 8, sizeof(information));
        u64 symbol = information >> 32;
        if (!offset && (information & UINT32_MAX) == ELF_RELOCATION_X86_64_64 && (symbol + 1) * ELF_SYMBOL_SIZE <= symbols.length)
        {
            u32 name_offset;
            memcpy(&name_offset, symbols.pointer + symbol * ELF_SYMBOL_SIZE, sizeof(name_offset));
            if (name_offset < strings.length)
            {
                result = string_from_pointer((char8*)strings.pointer + name_offset);
            }
        }
    }

    return result;
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

// The FS-prefixed load a thread-pointer read compiles to: the 0x64 segment
// override, a REX.W byte, and the MOV opcode. The destination register is
// whatever the emitter assigned, so it is not part of the pattern; the prefix
// and the width are, and nothing else in a fixture without thread-local
// storage emits them.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_x64_segment_override_load(ObjectFile* object)
{
    if (!object || object->error != OBJECT_ERROR_NONE || object->section_count <= OBJECT_SECTION_TEXT || !object->sections)
    {
        return false;
    }
    ByteSlice text = object->sections[OBJECT_SECTION_TEXT].data;
    for (u64 offset = 0; offset + 3 <= text.length; offset += 1)
    {
        if (text.pointer[offset] == 0x64 && (text.pointer[offset + 1] & 0xf8) == 0x48 && text.pointer[offset + 2] == 0x8b)
        {
            return true;
        }
    }
    return false;
}

// One row of the symbol table tests/basic_c_weak_alias.c must produce.
// `target` names the definition an alias has to coincide with; it is empty
// for a symbol that owns its own storage.
typedef struct CompilerDriverWeakAliasExpectation CompilerDriverWeakAliasExpectation;
struct CompilerDriverWeakAliasExpectation
{
    String8 name;
    String8 target;
    bool global;
    bool weak;
    u8 reserved[6];
};

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL ObjectSymbol* compiler_driver_test_symbol_by_name(ObjectFile* object, String8 name)
{
    ObjectSymbol* result = 0;
    for (u32 symbol_index = 0; symbol_index < object->symbol_count && !result; symbol_index += 1)
    {
        result = string_equal(object->symbols[symbol_index].name, name) ? object->symbols + symbol_index : 0;
    }

    return result;
}

// The symbol table __attribute__((weak)) and __attribute__((alias)) have to
// produce, which is the one Clang produces for the same fixture: an alias
// takes its target's section, offset, size and kind and contributes only its
// own binding, a weak definition keeps its own storage and is only rebound,
// and a declaration whose name merely happens to be an attribute spelling
// stays a strong definition.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_weak_alias_symbols(ObjectFile* object)
{
    CompilerDriverWeakAliasExpectation expectations[] = {
        {.name = S8("alias_target_function"), .global = true},
        {.name = S8("alias_static_target")},
        {.name = S8("alias_target_object"), .global = true},
        {.name = S8("weak_definition_function"), .global = true, .weak = true},
        {.name = S8("weak_definition_object"), .global = true, .weak = true},
        {.name = S8("weak"), .global = true},
        {.name = S8("alias"), .global = true},
        {.name = S8("alias_function"), .target = S8("alias_target_function"), .global = true, .weak = true},
        {.name = S8("alias_function_typeof"), .target = S8("alias_target_function"), .global = true, .weak = true},
        {.name = S8("alias_function_typedef"), .target = S8("alias_target_function"), .global = true},
        {.name = S8("alias_of_static"), .target = S8("alias_static_target"), .global = true},
        {.name = S8("alias_object"), .target = S8("alias_target_object"), .global = true},
        {.name = S8("alias_object_long"), .target = S8("alias_target_object"), .global = true},
    };
    bool result = object && object->error == OBJECT_ERROR_NONE && object->symbols;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expectations) && result; index += 1)
    {
        CompilerDriverWeakAliasExpectation expectation = expectations[index];
        ObjectSymbol* symbol = compiler_driver_test_symbol_by_name(object, expectation.name);
        result = symbol != 0 && symbol->global == expectation.global && symbol->weak == expectation.weak &&
                 symbol->section != OBJECT_SECTION_UNDEFINED;
        if (result && expectation.target.length)
        {
            ObjectSymbol* target = compiler_driver_test_symbol_by_name(object, expectation.target);
            result = target != 0 && symbol->section == target->section && symbol->value == target->value && symbol->size == target->size &&
                     symbol->kind == target->kind;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_aarch64_tied_input_load(ObjectFile* object)
{
    if (object && object->error == OBJECT_ERROR_NONE && object->section_count > OBJECT_SECTION_TEXT && object->sections && object->symbols)
    {
        ByteSlice text = object->sections[OBJECT_SECTION_TEXT].data;
        // Object-reader/disassembly sequence for int numeric_tied_output with an empty asm body:
        // ldr w9, [x28, #0x18]; str w9, [x28, #0x10].  The adjacent load/store
        // proves that the tied input reaches the reused output register and is
        // then published through the output place.  (The frame shrank when
        // operand lowering stopped pre-loading the place it recovers, which
        // is what moved the slot from #0x20.)
        static u8 const tied_sequence[] = {
            0x89, 0x1b, 0x40, 0xb9,
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

// Finds one symbol a module-level assembly block put in an object. The
// global-assembly fixtures assert on binding, visibility and section rather
// than on encoded bytes, because what a startup object has to get right is
// what the linker reads out of its symbol table.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL ObjectSymbol const* compiler_driver_test_object_symbol(ObjectFile* object, String8 name)
{
    ObjectSymbol const* result = 0;
    if (object && object->error == OBJECT_ERROR_NONE && object->symbols)
    {
        for (u32 symbol_index = 0; symbol_index < object->symbol_count && !result; symbol_index += 1)
        {
            if (string_equal(object->symbols[symbol_index].name, name))
            {
                result = object->symbols + symbol_index;
            }
        }
    }

    return result;
}

// True when the object relocates `kind` against `name` somewhere in its text.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_test_object_relocates(ObjectFile* object, String8 name, ObjectRelocationKind kind)
{
    bool result = false;
    if (object && object->error == OBJECT_ERROR_NONE && object->relocations && object->symbols)
    {
        for (u32 relocation_index = 0; relocation_index < object->relocation_count && !result; relocation_index += 1)
        {
            ObjectRelocation relocation = object->relocations[relocation_index];
            result = relocation.section == OBJECT_SECTION_TEXT && relocation.kind == kind && relocation.symbol < object->symbol_count &&
                     string_equal(object->symbols[relocation.symbol].name, name);
        }
    }

    return result;
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

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_include_population(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    // Exactly sized prefix with a neighboring sentinel: the old fixed-array
    // append would overwrite unrelated invocation state after enough entries.
    String8 prefix[] = {S8("explicit"), S8("resource"), S8("sentinel")};
    CompilerDriverInvocation invocation = {.system_include_paths = prefix, .system_include_path_count = 2};
    char8 environment[4096] = {0};
    u64 length = 0;
    String8 expected[128];
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(expected); ++i)
    {
        expected[i] = string_format(arguments->arena, S8("C:/sdk/{u32}/include"), i);
        environment[length++] = ';';
        environment[length++] = ';';
        memcpy(environment + length, expected[i].pointer, expected[i].length);
        length += expected[i].length;
    }
    environment[length++] = ';';
    compiler_driver_test_append_environment_includes(arguments->arena, &invocation, (String8){.pointer = environment, .length = length});
    BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, invocation.system_include_path_count == 130);
    BUSTER_STRING_TEST(arguments, invocation.system_include_paths[0], S8("explicit"));
    BUSTER_STRING_TEST(arguments, invocation.system_include_paths[1], S8("resource"));
    BUSTER_STRING_TEST(arguments, prefix[2], S8("sentinel"));
    memset(environment, '?', sizeof(environment));
    if (invocation.system_include_path_count == 130)
        for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(expected); ++i)
            BUSTER_STRING_TEST(arguments, invocation.system_include_paths[2 + i], expected[i]);
    // Empty entries disappear, but nonempty duplicates and order are retained.
    compiler_driver_test_append_environment_includes(arguments->arena, &invocation, S8(";;repeat;;repeat;"));
    BUSTER_TEST(arguments, invocation.system_include_path_count == 132);
    if (invocation.system_include_path_count == 132)
    {
        BUSTER_STRING_TEST(arguments, invocation.system_include_paths[130], S8("repeat"));
        BUSTER_STRING_TEST(arguments, invocation.system_include_paths[131], S8("repeat"));
    }
    String8* before = invocation.system_include_paths;
    compiler_driver_test_append_environment_includes(arguments->arena, &invocation, (String8){0});
    compiler_driver_test_append_environment_includes(arguments->arena, &invocation, S8(";;;"));
    BUSTER_TEST(arguments, invocation.system_include_path_count == 132 && invocation.system_include_paths == before);
    CompilerDriverInvocation overflow = {.system_include_path_count = UINT32_MAX};
    compiler_driver_test_append_environment_includes(arguments->arena, &overflow, S8("one"));
    BUSTER_TEST(arguments, overflow.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_TEST(arguments, overflow.system_include_path_count == UINT32_MAX && !overflow.system_include_paths);
    return result;
}

UnitTestResult compiler_driver_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = compiler_driver_test_include_population(arguments);

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

    // Keep over-aligned C types in the ordinary driver suite. When Node is
    // available, validate AND execute the emitted memory64 module; a header
    // check alone cannot detect an illegal memory-argument alignment hint.
    String8 wasm64_alignment_output = buster_test_temporary_path(arguments->arena, S8("buster-wasm64-alignment"), S8(".wasm"));
    String8 wasm64_alignment_command[] = {
        S8("-target"), S8("wasm64-unknown-freestanding"), S8("-nostdinc"), S8("-o"), wasm64_alignment_output,
        S8("tests/basic_c_wasm64_alignment.c"),
    };
    CompilerDriverResult wasm64_alignment = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wasm64_alignment_command)));
    BUSTER_TEST(arguments, wasm64_alignment.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, wasm64_alignment.has_wasm64 && wasm64_alignment.wasm64.stats.memory64);
    if (wasm64_alignment.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 node = executable_resolve_in_path(arguments->arena, S8("node"));
        if (node.length)
        {
            String8 node_arguments[] = {node, S8("--experimental-wasm-memory64"), S8("tests/wasm_memory_alignment_execution.js"), wasm64_alignment_output};
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(node_arguments), (SliceString8){0}, (SliceString8){0},
                                                       (ProcessSpawnOptions){.use_process_environment = 1});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(arguments->arena, spawn, 30000000);
                BUSTER_TEST(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
        else
        {
            arguments->show(arguments, S8("Wasm64 alignment engine execution skipped: Node is not installed\n"));
        }
    }

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
    // `-D` values: an `=` with nothing after it is an empty replacement list --
    // the spelling a build uses to switch a decoration off -- while the form
    // with no `=` at all is the one that means `1`.
    {
        TemporalArena define_temporary = scratch_begin(&arguments->arena, 1);
        Arena* define_arena = define_temporary.arena;
        String8 define_source_path = buster_test_temporary_path(define_arena, S8("buster-driver-define-value"), S8(".c"));
        String8 define_source = S8("int probe = 0 DECORATION;\n");
        BUSTER_TEST(arguments, file_write(define_source_path, (ByteSlice){.pointer = (u8*)define_source.pointer, .length = define_source.length}));
        String8 define_values[] = {
            S8("-DDECORATION="),
            S8("-DDECORATION"),
            S8("-DDECORATION=7"),
        };
        String8 define_expected[] = {
            S8("int probe = 0 ;"),
            S8("int probe = 0 1 ;"),
            S8("int probe = 0 7 ;"),
        };
        for (u32 define_index = 0; define_index < BUSTER_ARRAY_LENGTH(define_values); define_index += 1)
        {
            String8 define_command_line[] = {
                S8("-E"),
                define_values[define_index],
                define_source_path,
            };
            CompilerDriverResult define_preprocess = compiler_driver_execute_invocation(
                define_arena, compiler_driver_parse_arguments(define_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(define_command_line)));
            BUSTER_TEST(arguments, define_preprocess.error == COMPILER_DRIVER_ERROR_NONE);
            BUSTER_TEST(arguments, string_first_sequence(define_preprocess.output, define_expected[define_index]) != BUSTER_STRING_NO_MATCH);
        }
        // The separated spelling reaches the same splitter.
        String8 separated_command_line[] = {
            S8("-E"),
            S8("-D"),
            S8("DECORATION="),
            define_source_path,
        };
        CompilerDriverResult separated_preprocess = compiler_driver_execute_invocation(
            define_arena, compiler_driver_parse_arguments(define_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(separated_command_line)));
        BUSTER_TEST(arguments, separated_preprocess.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, string_first_sequence(separated_preprocess.output, S8("int probe = 0 ;")) != BUSTER_STRING_NO_MATCH);
        scratch_end(define_temporary);
    }
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
    // The -E text reproduces the source's own spacing: adjacency and line
    // structure are recovered from the source map, so `int main(void)` comes
    // back as written rather than space-joined, and the brace stays on its
    // own line.  autoconf's grep-the-preprocessor idiom -- CPython's
    // `grep '^PLATFORM_TRIPLET='` over Misc/platform_triplet.c -- reads
    // exactly these two facts.
    BUSTER_TEST(arguments, string_first_sequence(warning_cross_target.output, S8("int main(void)\n{\n")) != BUSTER_STRING_NO_MATCH);
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
    // The GNU flavours come first and the strict ones after, which is what the
    // EXPECTED_GNU split below indexes on. C99 is here because it is the
    // dialect a libc's own build system asks for: musl's makefile passes
    // -std=c99 and its headers branch on __STDC_VERSION__.
    String8 dialect_flags[] = {
        S8("-std=gnu99"), S8("-std=gnu11"), S8("-std=gnu17"), S8("-std=gnu23"),
        S8("-std=c99"),   S8("-std=c11"),   S8("-std=c17"),   S8("-std=c23"),
    };
    String8 dialect_versions[] = {
        S8("-DEXPECTED_STDC_VERSION=199901L"), S8("-DEXPECTED_STDC_VERSION=201112L"), S8("-DEXPECTED_STDC_VERSION=201710L"),
        S8("-DEXPECTED_STDC_VERSION=202311L"), S8("-DEXPECTED_STDC_VERSION=199901L"), S8("-DEXPECTED_STDC_VERSION=201112L"),
        S8("-DEXPECTED_STDC_VERSION=201710L"), S8("-DEXPECTED_STDC_VERSION=202311L"),
    };
    for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialect_flags); dialect_index += 1)
    {
        TemporalArena dialect_temporary = arena_begin_temporal(arguments->arena);
        Arena* dialect_arena = dialect_temporary.arena;
#if BUSTER_IOS
        String8 dialect_command_line[] = {
            S8("-fsyntax-only"), dialect_flags[dialect_index], dialect_versions[dialect_index],
            dialect_index < 4 ? S8("-DEXPECTED_GNU=1") : S8("-DEXPECTED_GNU=0"), S8("tests/basic_c_dialect.c"),
        };
#else
        String8 dialect_object_path =
            buster_test_temporary_path(dialect_arena, S8("buster-c-dialect"), string_format(dialect_arena, S8("-{u32}.o"), dialect_index));
        String8 dialect_command_line[] = {
            S8("-c"), dialect_flags[dialect_index], dialect_versions[dialect_index], dialect_index < 4 ? S8("-DEXPECTED_GNU=1") : S8("-DEXPECTED_GNU=0"),
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
    // The AArch64 i128 fixture keeps results in caller-provided storage so
    // both halves are observable without depending on the still-evolving
    // direct i128 return ABI. Compile it through every allocator mode: the
    // canonical emitter owns these operations while machine modes may elect
    // their documented per-function fallback.
    {
        String8 i128_allocator_flags[] = {
            S8("-fregister-allocator=none"),
            S8("-fregister-allocator=mir-stack"),
            S8("-fregister-allocator=fast"),
            S8("-fregister-allocator=quality"),
        };
        String8 zig_executable = executable_resolve_in_path(arguments->arena, S8("zig"));
        String8 qemu_aarch64_executable = executable_resolve_in_path(arguments->arena, S8("qemu-aarch64"));
        bool can_run_aarch64_i128 = zig_executable.length != 0 && qemu_aarch64_executable.length != 0;
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(i128_allocator_flags); allocator_index += 1)
        {
            TemporalArena i128_temporary = arena_begin_temporal(c_object_arena);
            String8 i128_object_path = buster_test_temporary_path(
                i128_temporary.arena, S8("buster-c-aarch64-i128"), string_format(i128_temporary.arena, S8("-{u32}.o"), allocator_index));
            String8 i128_command_line[] = {
                S8("-c"),
                S8("-O0"),
                S8("-target"),
                S8("aarch64-unknown-linux-gnu"),
                i128_allocator_flags[allocator_index],
                S8("-o"),
                i128_object_path,
                S8("tests/basic_c_aarch64_i128.c"),
            };
            CompilerDriverResult i128_result = compiler_driver_execute_invocation(
                i128_temporary.arena, compiler_driver_parse_arguments(i128_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(i128_command_line)));
            BUSTER_TEST(arguments, i128_result.error == COMPILER_DRIVER_ERROR_NONE);
            BUSTER_TEST(arguments, i128_result.has_object);
            FileMapRead i128_map = file_map_read(i128_temporary.arena, i128_object_path, (FileReadOptions){0});
            BUSTER_TEST(arguments, i128_map.bytes.length != 0);
            file_map_unmap(i128_map);
            if (can_run_aarch64_i128 && i128_result.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 i128_executable_path = buster_test_temporary_path(
                    i128_temporary.arena, S8("buster-c-aarch64-i128-run"), string_format(i128_temporary.arena, S8("-{u32}"), allocator_index));
                String8 i128_link_arguments[] = {
                    zig_executable,
                    S8("cc"),
                    S8("-target"),
                    S8("aarch64-linux-musl"),
                    S8("-static"),
                    i128_object_path,
                    S8("-o"),
                    i128_executable_path,
                };
                ProcessSpawnResult i128_link_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(i128_link_arguments), (SliceString8){0},
                                                                        (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, i128_link_spawn.handle != 0);
                bool i128_linked = false;
                if (i128_link_spawn.handle)
                {
                    i128_linked = os_process_wait_sync(i128_temporary.arena, i128_link_spawn).result == PROCESS_RESULT_SUCCESS;
                }
                BUSTER_TEST(arguments, i128_linked);
                if (i128_linked)
                {
                    String8 i128_run_arguments[] = {qemu_aarch64_executable, i128_executable_path};
                    ProcessSpawnResult i128_run_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(i128_run_arguments), (SliceString8){0},
                                                                          (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                    BUSTER_TEST(arguments, i128_run_spawn.handle != 0);
                    if (i128_run_spawn.handle)
                    {
                        ProcessWaitResult i128_run_wait = os_process_wait_sync(i128_temporary.arena, i128_run_spawn);
                        BUSTER_TEST(arguments, i128_run_wait.result == PROCESS_RESULT_SUCCESS);
                    }
                }
            }
            scratch_end(i128_temporary);
        }
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
    {
        // -fPIC as a code model, read back out of the object it produces.
        // Every allocator has to make the same four decisions, because the
        // musl harness gates the dynamic probe's transcript under each of
        // them: the two interposable data symbols through the GOT, the
        // static one still rip-relative, the direct call through the PLT and
        // the interposable function's address through the GOT as well.
        String8 pic_model_modes[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(pic_model_modes); mode_index += 1)
        {
            TemporalArena pic_model_temporary = arena_begin_temporal(c_object_arena);
            Arena* pic_model_arena = pic_model_temporary.arena;
            String8 pic_model_object_path = buster_test_temporary_path(pic_model_arena, S8("buster-c-pic-model"), S8(".o"));
            String8 pic_model_allocator = string_format(pic_model_arena, S8("-fregister-allocator={S8}"), pic_model_modes[mode_index]);
            String8 pic_model_command_line[] = {
                S8("-c"),  S8("-target"), S8("x86_64-unknown-linux-gnu"), pic_model_allocator, S8("-fPIC"),
                S8("-o"), pic_model_object_path, S8("tests/basic_c_pic_model.c"),
            };
            CompilerDriverResult pic_model = compiler_driver_execute_invocation(
                pic_model_arena,
                compiler_driver_parse_arguments(pic_model_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pic_model_command_line)));
            if (pic_model.error != COMPILER_DRIVER_ERROR_NONE && pic_model.diagnostic.length)
            {
                arguments->show(arguments, S8("-fPIC code model driver error: {S8}\n"), pic_model.diagnostic);
            }
            BUSTER_TEST(arguments, pic_model.error == COMPILER_DRIVER_ERROR_NONE && pic_model.has_object);
            // The driver's own object, before it is written: R_X86_64_PLT32
            // reads back as PC32, which is what it means once a static link
            // has bound the callee, so the call form is only inspectable
            // here.
            ObjectFile pic_model_object = pic_model.object;
            bool global_through_got = false;
            bool external_through_got = false;
            bool static_direct = false;
            bool call_through_plt = false;
            bool function_address_through_got = false;
            for (u32 relocation_index = 0; relocation_index < pic_model_object.relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = pic_model_object.relocations + relocation_index;
                if (relocation->section != OBJECT_SECTION_TEXT || relocation->symbol >= pic_model_object.symbol_count)
                {
                    continue;
                }
                String8 name = pic_model_object.symbols[relocation->symbol].name;
                bool got = relocation->kind == OBJECT_RELOCATION_X86_64_GOTPCREL;
                global_through_got = global_through_got || (got && string_equal(name, S8("buster_pic_model_global")));
                external_through_got = external_through_got || (got && string_equal(name, S8("buster_pic_model_external")));
                function_address_through_got = function_address_through_got || (got && string_equal(name, S8("buster_pic_model_callee")));
                static_direct = static_direct || (relocation->kind == OBJECT_RELOCATION_X86_64_PC32 &&
                                                  string_equal(name, S8("buster_pic_model_static")));
                call_through_plt = call_through_plt || (relocation->kind == OBJECT_RELOCATION_X86_64_PLT32 &&
                                                        string_equal(name, S8("buster_pic_model_bump")));
            }
            BUSTER_TEST(arguments, global_through_got && external_through_got && function_address_through_got && static_direct && call_through_plt);
            // The same fixture without the flag: nothing goes through the GOT
            // or the PLT, which is what makes the checks above the flag's own
            // rather than the fixture's.
            String8 pic_model_default_path = buster_test_temporary_path(pic_model_arena, S8("buster-c-pic-model-default"), S8(".o"));
            String8 pic_model_default_command_line[] = {
                S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"), pic_model_allocator,
                S8("-o"), pic_model_default_path, S8("tests/basic_c_pic_model.c"),
            };
            CompilerDriverResult pic_model_default = compiler_driver_execute_invocation(
                pic_model_arena,
                compiler_driver_parse_arguments(pic_model_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pic_model_default_command_line)));
            BUSTER_TEST(arguments, pic_model_default.error == COMPILER_DRIVER_ERROR_NONE && pic_model_default.has_object);
            bool default_indirect_found = false;
            for (u32 relocation_index = 0; relocation_index < pic_model_default.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocationKind kind = pic_model_default.object.relocations[relocation_index].kind;
                default_indirect_found =
                    default_indirect_found || kind == OBJECT_RELOCATION_X86_64_GOTPCREL || kind == OBJECT_RELOCATION_X86_64_PLT32;
            }
            BUSTER_TEST(arguments, !default_indirect_found);
            scratch_end(pic_model_temporary);
        }
    }
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    {
        // Keep one real external-compiler fixture in the driver suite.  The
        // non-PIC form exercises clang's R_X86_64_32S; the -fPIC form is the
        // REX_GOTPCRELX family, which this reader takes as one GOT kind and
        // this linker resolves the way it resolves its own -fPIC output --
        // by relaxing each load back into the address it would have computed,
        // because a static image binds every name in it.  Keep debug sections
        // out of this fixture: clang's newer .debug_addr/.debug_str_offsets
        // sections are outside this object's intentionally narrow
        // debug-section model.
        TemporalArena pic_temporary = arena_begin_temporal(c_object_arena);
        Arena* pic_arena = pic_temporary.arena;
        String8 no_pic_object_path = buster_test_temporary_path(pic_arena, S8("buster-c-clang-no-pic"), S8(".o"));
        String8 pic_object_path = buster_test_temporary_path(pic_arena, S8("buster-c-clang-pic"), S8(".o"));
        String8 no_pic_compile_arguments[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-nostdinc"), S8("-fno-pic"), S8("-g0"),
            S8("-c"), S8("-o"), no_pic_object_path, S8("tests/basic_c_pic.c"),
        };
        String8 pic_compile_arguments[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-nostdinc"), S8("-fPIC"), S8("-g0"),
            S8("-c"), S8("-o"), pic_object_path, S8("tests/basic_c_pic.c"),
        };
        ProcessSpawnResult no_pic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(no_pic_compile_arguments), (SliceString8){0},
                                                           (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
        BUSTER_TEST(arguments, no_pic_spawn.handle != 0);
        bool no_pic_compiled = false;
        if (no_pic_spawn.handle)
        {
            no_pic_compiled = os_process_wait_sync(pic_arena, no_pic_spawn).result == PROCESS_RESULT_SUCCESS;
        }
        BUSTER_TEST(arguments, no_pic_compiled);
        ProcessSpawnResult pic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(pic_compile_arguments), (SliceString8){0},
                                                        (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
        BUSTER_TEST(arguments, pic_spawn.handle != 0);
        bool pic_compiled = false;
        if (pic_spawn.handle)
        {
            pic_compiled = os_process_wait_sync(pic_arena, pic_spawn).result == PROCESS_RESULT_SUCCESS;
        }
        BUSTER_TEST(arguments, pic_compiled);
        if (no_pic_compiled && pic_compiled)
        {
            Target elf_target = {
                .cpu_arch = CPU_ARCH_X86_64,
                .os = OPERATING_SYSTEM_LINUX,
            };
            FileMapRead no_pic_map = file_map_read(pic_arena, no_pic_object_path, (FileReadOptions){0});
            ObjectFile no_pic = object_read(pic_arena, no_pic_map.bytes, elf_target);
            bool signed_absolute_found = false;
            for (u32 relocation_index = 0; relocation_index < no_pic.relocation_count; relocation_index += 1)
            {
                signed_absolute_found = signed_absolute_found || no_pic.relocations[relocation_index].kind == OBJECT_RELOCATION_X86_64_ABSOLUTE32S;
            }
            BUSTER_TEST(arguments, no_pic.error == OBJECT_ERROR_NONE && signed_absolute_found);
            file_map_unmap(no_pic_map);
            FileMapRead pic_map = file_map_read(pic_arena, pic_object_path, (FileReadOptions){0});
            ObjectFile pic = object_read(pic_arena, pic_map.bytes, elf_target);
            bool got_indirect_found = false;
            for (u32 relocation_index = 0; relocation_index < pic.relocation_count; relocation_index += 1)
            {
                got_indirect_found = got_indirect_found || pic.relocations[relocation_index].kind == OBJECT_RELOCATION_X86_64_GOTPCREL;
            }
            BUSTER_TEST(arguments, pic.error == OBJECT_ERROR_NONE && got_indirect_found);
            file_map_unmap(pic_map);
            String8 no_pic_link_output = buster_test_temporary_path(pic_arena, S8("buster-c-clang-no-pic"), S8(""));
            String8 no_pic_link_arguments[] = {S8("-o"), no_pic_link_output, no_pic_object_path};
            CompilerDriverResult no_pic_link = compiler_driver_execute_invocation(
                pic_arena, compiler_driver_parse_arguments(pic_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(no_pic_link_arguments)));
            BUSTER_TEST(arguments, no_pic_link.error == COMPILER_DRIVER_ERROR_NONE);
            String8 pic_link_output = buster_test_temporary_path(pic_arena, S8("buster-c-clang-pic"), S8(""));
            String8 pic_link_arguments[] = {S8("-o"), pic_link_output, pic_object_path};
            CompilerDriverResult pic_link = compiler_driver_execute_invocation(
                pic_arena, compiler_driver_parse_arguments(pic_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pic_link_arguments)));
            if (pic_link.error != COMPILER_DRIVER_ERROR_NONE && pic_link.diagnostic.length)
            {
                arguments->show(arguments, S8("clang -fPIC object link error: {S8}\n"), pic_link.diagnostic);
            }
            BUSTER_TEST(arguments, pic_link.error == COMPILER_DRIVER_ERROR_NONE && pic_link.native_link.executable.length != 0);
        }
        scratch_end(pic_temporary);
    }
#endif
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
    // A plain case label is folded in its own type and used to reach dispatch
    // with those bits: `case -1` on a `long long` switch was the immediate
    // 0xffffffff, which matches nothing and collides with `case 4294967295`.
    // Only running the program says whether the dispatch immediates and the
    // controlling value speak the same type, so the fixture returns a
    // per-shape code rather than being inspected as IR.
    String8 c_switch_case_label_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-switch-case-label"),
#if BUSTER_WINDOWS
                                                                              S8(".exe"));
#else
                                                                              S8(""));
#endif
    String8 c_switch_case_label_command_line[] = {
        S8("-std=gnu23"),
        S8("-o"),
        c_switch_case_label_executable_path,
        S8("tests/basic_c_switch_case_label.c"),
    };
    CompilerDriverResult c_switch_case_label = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_switch_case_label_command_line)));
    BUSTER_TEST(arguments, c_switch_case_label.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_switch_case_label.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_switch_case_label_run_arguments[] = {
            c_switch_case_label_executable_path,
        };
        ProcessSpawnResult c_switch_case_label_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_switch_case_label_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_switch_case_label_spawn.handle != 0);
        if (c_switch_case_label_spawn.handle)
        {
            ProcessWaitResult c_switch_case_label_wait = os_process_wait_sync(arguments->arena, c_switch_case_label_spawn);
            BUSTER_TEST(arguments, c_switch_case_label_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // The DoomGeneric shapes. Each one is a construct that compiling upstream
    // DoomGeneric unmodified rejected or lowered wrongly, and two of them --
    // the unused block-scope `extern` object and the pointer table that
    // relocates against another unit -- are checked by the link succeeding and
    // by the run finding the pointers, so this fixture is compiled from two
    // sources and executed rather than inspected.
    String8 c_doom_shapes_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-doom-shapes"),
#if BUSTER_WINDOWS
                                                                        S8(".exe"));
#else
                                                                        S8(""));
#endif
    String8 c_doom_shapes_command_line[] = {
        S8("-o"),
        c_doom_shapes_executable_path,
        S8("tests/basic_c_doom_shapes.c"),
        S8("tests/basic_c_doom_shapes_import.c"),
    };
    CompilerDriverResult c_doom_shapes = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_doom_shapes_command_line)));
    BUSTER_TEST(arguments, c_doom_shapes.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_doom_shapes.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_doom_shapes_run_arguments[] = {
            c_doom_shapes_executable_path,
        };
        ProcessSpawnResult c_doom_shapes_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_doom_shapes_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_doom_shapes_spawn.handle != 0);
        if (c_doom_shapes_spawn.handle)
        {
            ProcessWaitResult c_doom_shapes_wait = os_process_wait_sync(arguments->arena, c_doom_shapes_spawn);
            BUSTER_TEST(arguments, c_doom_shapes_wait.result == PROCESS_RESULT_SUCCESS);
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
    // The narrow end of the same question, which the host answers for its own
    // convention: single-lane vectors, sub-eightbyte vectors, and 128-bit
    // integers crossing calls in both directions. The Windows shapes are what
    // the fixture was written for -- the wine block below runs it at three
    // models -- but every convention has an answer here worth pinning, and the
    // exit code names which family disagreed.
    String8 c_narrow_abi_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-win64-narrow-abi"),
#if BUSTER_WINDOWS
                                                                      S8(".exe"));
#else
                                                                      S8(""));
#endif
    String8 c_narrow_abi_command_line[] = {
        S8("-o"),
        c_narrow_abi_executable_path,
        S8("tests/basic_c_win64_narrow_abi.c"),
    };
    CompilerDriverResult c_narrow_abi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_narrow_abi_command_line)));
    BUSTER_TEST(arguments, c_narrow_abi.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_narrow_abi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_narrow_abi_run_arguments[] = {
            c_narrow_abi_executable_path,
        };
        ProcessSpawnResult c_narrow_abi_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_narrow_abi_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_narrow_abi_spawn.handle != 0);
        if (c_narrow_abi_spawn.handle)
        {
            ProcessWaitResult c_narrow_abi_wait = os_process_wait_sync(arguments->arena, c_narrow_abi_spawn);
            BUSTER_TEST(arguments, c_narrow_abi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // sizeof over an inline aggregate definition must be rejected, never
    // folded from the expression-type prediction's int guess (see the
    // fixture's header); the assertion is only that no object is produced.
    String8 c_sizeof_anonymous_path = buster_test_temporary_path(arguments->arena, S8("buster-c-sizeof-anonymous"), S8(""));
    String8 c_sizeof_anonymous_command_line[] = {
        S8("-o"),
        c_sizeof_anonymous_path,
        S8("tests/basic_c_sizeof_anonymous_aggregate.c"),
    };
    CompilerDriverResult c_sizeof_anonymous = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_sizeof_anonymous_command_line)));
    BUSTER_TEST(arguments, c_sizeof_anonymous.error != COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, !c_sizeof_anonymous.has_object);
    // 256-bit vector arguments, once through the default pipeline and once
    // through the canonical emitter, whose VMOVDQU move is the shape the
    // default pipeline only reaches through per-function fallback.
    for (u32 ymm_mode = 0; ymm_mode < 2; ymm_mode += 1)
    {
        String8 c_ymm_vector_path = buster_test_temporary_path(
            arguments->arena, ymm_mode ? S8("buster-c-vector-ymm-canonical") : S8("buster-c-vector-ymm"), S8(""));
        String8 c_ymm_vector_command_line[] = {
            ymm_mode ? S8("-fno-register-allocator") : S8("-fregister-allocator=fast"),
            S8("-o"),
            c_ymm_vector_path,
            S8("tests/basic_c_vector_argument_ymm.c"),
        };
        CompilerDriverResult c_ymm_vector = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_ymm_vector_command_line)));
        BUSTER_TEST(arguments, c_ymm_vector.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_ymm_vector.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 c_ymm_vector_arguments[] = {
                c_ymm_vector_path,
            };
            ProcessSpawnResult c_ymm_vector_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_ymm_vector_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, c_ymm_vector_spawn.handle != 0);
            if (c_ymm_vector_spawn.handle)
            {
                ProcessWaitResult c_ymm_vector_wait = os_process_wait_sync(arguments->arena, c_ymm_vector_spawn);
                BUSTER_TEST(arguments, c_ymm_vector_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    // Vector arguments narrower than a vector register, and a vector spilled
    // to the stack ahead of a register argument. Both shapes were silently
    // wrong on AArch64 and no fixture executed them: the canonical emitter had
    // no sized transfer for one- and two-byte parts and left the parameter
    // slot zero, and the machine callee's stack read-backs could land on an
    // argument register whose own capture had not run yet. Run through both
    // pipelines, like the ymm fixture above.
    for (u32 short_vector_mode = 0; short_vector_mode < 2; short_vector_mode += 1)
    {
        String8 c_short_vector_path = buster_test_temporary_path(
            arguments->arena, short_vector_mode ? S8("buster-c-vector-short-canonical") : S8("buster-c-vector-short"), S8(""));
        String8 c_short_vector_command_line[] = {
            short_vector_mode ? S8("-fno-register-allocator") : S8("-fregister-allocator=fast"),
            S8("-o"),
            c_short_vector_path,
            S8("tests/basic_c_vector_argument_short.c"),
        };
        CompilerDriverResult c_short_vector = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_short_vector_command_line)));
        BUSTER_TEST(arguments, c_short_vector.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_short_vector.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 c_short_vector_arguments[] = {
                c_short_vector_path,
            };
            ProcessSpawnResult c_short_vector_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_short_vector_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, c_short_vector_spawn.handle != 0);
            if (c_short_vector_spawn.handle)
            {
                ProcessWaitResult c_short_vector_wait = os_process_wait_sync(arguments->arena, c_short_vector_spawn);
                BUSTER_TEST(arguments, c_short_vector_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    // The same fixture again at pinned CPU models, compile-only. The rows
    // above take the host's model, so they cannot promise the metadata
    // resolve keeps carrying the exact shapes a 32-byte part asks for at a
    // given model: one ymm VMOVDQU where the model tops out at ymm (haswell),
    // and the same ymm move on a model that also owns zmm (znver5). SysV
    // returns the part in YMM0 directly; Win64 classifies the same result
    // direct since the widening in ir_classify_abi_value, so both targets
    // must reach an object at both models through both pipelines.
    {
        String8 c_ymm_model_targets[] = {S8("x86_64-linux"), S8("x86_64-pc-windows-msvc")};
        String8 c_ymm_model_names[] = {S8("haswell"), S8("znver5")};
        String8 c_ymm_model_flags[] = {S8("-march=haswell"), S8("-march=znver5")};
        for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(c_ymm_model_targets) * BUSTER_ARRAY_LENGTH(c_ymm_model_names) * 2; row += 1)
        {
            u32 target_index = row >> 2;
            u32 model_index = (row >> 1) & 1;
            u32 canonical = row & 1;
            TemporalArena c_ymm_model_temporary = scratch_begin(&arguments->arena, 1);
            String8 c_ymm_model_path = buster_test_temporary_path(
                c_ymm_model_temporary.arena, S8("buster-c-vector-ymm-model"),
                string_format(c_ymm_model_temporary.arena, S8("-{S8}-{S8}-{u32}.o"), c_ymm_model_targets[target_index], c_ymm_model_names[model_index],
                              canonical));
            String8 c_ymm_model_command_line[] = {
                canonical ? S8("-fno-register-allocator") : S8("-fregister-allocator=fast"),
                S8("-c"),
                S8("-target"),
                c_ymm_model_targets[target_index],
                c_ymm_model_flags[model_index],
                S8("-o"),
                c_ymm_model_path,
                S8("tests/basic_c_vector_argument_ymm.c"),
            };
            CompilerDriverResult c_ymm_model = compiler_driver_execute_invocation(
                c_ymm_model_temporary.arena,
                compiler_driver_parse_arguments(c_ymm_model_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_ymm_model_command_line)));
            BUSTER_TEST(arguments, c_ymm_model.error == COMPILER_DRIVER_ERROR_NONE);
            BUSTER_TEST(arguments, c_ymm_model.has_object);
            scratch_end(c_ymm_model_temporary);
        }
    }
    // Vectors past 64 bytes at pinned CPU models, compile-only and
    // Windows-only (no other convention accepts these signature types). The
    // piece count of a wide argument and the direct-versus-hidden-pointer
    // answer for a wide result both follow the model -- a 128-byte vector is
    // eight references and a hidden-pointer result on nehalem, four and
    // ymm0-3 on haswell, two and zmm0-1 on znver5 -- so each model must
    // reach an object through both pipelines. The wine block below runs the
    // same fixture at the pinned models.
    {
        String8 c_wide_model_flags[] = {S8("-march=nehalem"), S8("-march=haswell"), S8("-march=znver5")};
        for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(c_wide_model_flags) * 2; row += 1)
        {
            u32 model_index = row >> 1;
            u32 canonical = row & 1;
            TemporalArena c_wide_model_temporary = scratch_begin(&arguments->arena, 1);
            String8 c_wide_model_path = buster_test_temporary_path(
                c_wide_model_temporary.arena, S8("buster-c-vector-wide-model"),
                string_format(c_wide_model_temporary.arena, S8("-{u32}-{u32}.o"), model_index, canonical));
            String8 c_wide_model_command_line[] = {
                canonical ? S8("-fno-register-allocator") : S8("-fregister-allocator=fast"),
                S8("-c"),
                S8("-target"),
                S8("x86_64-pc-windows-msvc"),
                c_wide_model_flags[model_index],
                S8("-o"),
                c_wide_model_path,
                S8("tests/basic_c_vector_argument_wide.c"),
            };
            CompilerDriverResult c_wide_model = compiler_driver_execute_invocation(
                c_wide_model_temporary.arena,
                compiler_driver_parse_arguments(c_wide_model_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_wide_model_command_line)));
            BUSTER_TEST(arguments, c_wide_model.error == COMPILER_DRIVER_ERROR_NONE);
            BUSTER_TEST(arguments, c_wide_model.has_object);
            scratch_end(c_wide_model_temporary);
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
    // Stringification reproduces the argument's own white space, so the
    // fixture's expectations are byte comparisons against what Clang and GCC
    // produce for the same source; it returns the number of the case that
    // disagreed.
    String8 stringify_spacing_path = buster_test_temporary_path(arguments->arena, S8("buster-c-stringify-spacing"),
#if BUSTER_WINDOWS
                                                                S8(".exe"));
#else
                                                                S8(""));
#endif
    String8 stringify_spacing_command_line[] = {
        S8("-o"),
        stringify_spacing_path,
        S8("tests/basic_c_stringify_spacing.c"),
    };
    CompilerDriverResult stringify_spacing_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stringify_spacing_command_line)));
    BUSTER_TEST(arguments, stringify_spacing_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (stringify_spacing_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 stringify_spacing_run_arguments[] = {
            stringify_spacing_path,
        };
        ProcessSpawnResult stringify_spacing_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(stringify_spacing_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, stringify_spacing_spawn.handle != 0);
        if (stringify_spacing_spawn.handle)
        {
            ProcessWaitResult stringify_spacing_wait = os_process_wait_sync(arguments->arena, stringify_spacing_spawn);
            BUSTER_TEST(arguments, stringify_spacing_wait.result == PROCESS_RESULT_SUCCESS);
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
    // A requested library the search path does not hold anywhere must fail
    // the link the way ld does, not become a blind DT_NEEDED the loader
    // faults on later.  This is what every autoconf `AC_SEARCH_LIBS` probe
    // rests on: configure links against `-lsocket`, `-lnsl` and friends and
    // reads the failed link as the library not existing; a driver that
    // "succeeds" poisons LIBS for the rest of the configure run, and every
    // runtime probe after it exits 127 loading libsocket.so.
    String8 c_missing_library_path = buster_test_temporary_path(arguments->arena, S8("buster-c-missing-library"), S8(""));
    String8 c_missing_library_command_line[] = {
        S8("-o"),
        c_missing_library_path,
        S8("-lbuster-no-such-library"),
        S8("tests/basic_c_dynamic_library.c"),
    };
    CompilerDriverResult c_missing_library = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_missing_library_command_line)));
    BUSTER_TEST(arguments, c_missing_library.error == COMPILER_DRIVER_ERROR_LINK);
    BUSTER_STRING_TEST(arguments, c_missing_library.diagnostic, S8("cannot find -lbuster-no-such-library"));
    // And the symbol-level twin: a strong reference no linked library
    // exports must fail the link like ld's "undefined reference", not become
    // a .dynsym entry the loader faults on.  This is the AC_CHECK_FUNC shape
    // -- `char fdwalk(); return fdwalk();` -- that made CPython's configure
    // find fdwalk, fork1, plock, rtpspawn and _getpty on Linux.
    String8 c_undefined_reference_source_path = buster_test_temporary_path(arguments->arena, S8("buster-c-undefined-reference"), S8(".c"));
    BUSTER_TEST(arguments, file_write(c_undefined_reference_source_path,
                                      BUSTER_SLICE_TO_BYTE_SLICE(S8("char buster_no_such_function();\nint main(void) { return buster_no_such_function(); }\n"))));
    String8 c_undefined_reference_path = buster_test_temporary_path(arguments->arena, S8("buster-c-undefined-reference"), S8(""));
    String8 c_undefined_reference_command_line[] = {
        S8("-o"),
        c_undefined_reference_path,
        c_undefined_reference_source_path,
    };
    CompilerDriverResult c_undefined_reference = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_undefined_reference_command_line)));
    BUSTER_TEST(arguments, c_undefined_reference.error == COMPILER_DRIVER_ERROR_LINK);
    BUSTER_TEST(arguments, string_ends_with_sequence(c_undefined_reference.diagnostic, S8("unresolved symbol: buster_no_such_function")));
    // A hosted dynamic link must also resolve an imported function used as a
    // data initializer.  This is the relocation shape cJSON uses for its
    // global allocator hooks (R_X86_64_64 -> the generated PLT entry).
    String8 c_function_pointer_path = buster_test_temporary_path(arguments->arena, S8("buster-c-function-pointer"), S8(""));
    String8 c_function_pointer_command_line[] = {
        S8("-o"), c_function_pointer_path, S8("tests/basic_c_function_pointer.c"),
    };
    CompilerDriverResult c_function_pointer = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_function_pointer_command_line)));
    BUSTER_TEST(arguments, c_function_pointer.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_function_pointer.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_function_pointer_run_arguments[] = {c_function_pointer_path};
        ProcessSpawnResult c_function_pointer_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_function_pointer_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){.use_process_environment = true});
        BUSTER_TEST(arguments, c_function_pointer_spawn.handle != 0);
        if (c_function_pointer_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_function_pointer_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
    // Three CPython-found lowering shapes, each compiled and run with its
    // answers checked: a flexible array member initialized at static
    // storage (dictobject's empty keys), `++*s++` (dtoa's digit strip), and
    // `__typeof__` over an address-of member base (crossinterp's Py_CLEAR).
    String8 c_cpython_shape_fixtures[] = {
        S8("tests/basic_c_flexible_array_static_initializer.c"),
        S8("tests/basic_c_prefix_update_stepping_pointer.c"),
        S8("tests/basic_c_typeof_address_member.c"),
        S8("tests/basic_c_parenthesized_string_initializer.c"),
        S8("tests/basic_c_cast_away_const_store.c"),
        S8("tests/basic_c_extern_aligned_definition.c"),
        S8("tests/basic_c_macro_comment_bitfields.c"),
        S8("tests/basic_c_typeof_comma_base.c"),
        S8("tests/basic_c_conditional_signed_arithmetic.c"),
        S8("tests/basic_c_transparent_union.c"),
        S8("tests/basic_c_static_assert_shadowed_typedef.c"),
        S8("tests/basic_c_cast_deref_conditional.c"),
        S8("tests/basic_c_typeof_update_operand.c"),
#if BUSTER_CPU_ARCH_X86_64
        // <emmintrin.h> refuses to be included anywhere else.
        S8("tests/basic_c_spin_pause_builtin.c"),
#endif
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        // %fs:0 is glibc's thread pointer; no other platform keeps one there.
        S8("tests/basic_c_asm_memory_operand_no_load.c"),
#endif
        S8("tests/basic_c_static_pointer_subtraction.c"),
        S8("tests/basic_c_static_local_member_address.c"),
        S8("tests/basic_c_tied_operand_reused_source.c"),
        S8("tests/basic_c_union_designator_merge.c"),
        S8("tests/basic_c_date_time_predefines.c"),
        S8("tests/basic_c_plain_char_literal_sign.c"),
        S8("tests/basic_c_char_limits.c"),
        S8("tests/basic_c_explicit_allocator_sticks.c"),
    };
    // Each iteration compiles in-process; the module arena is never rewound,
    // so the loop's allocation lives in its own scratch or an unrelated
    // later invocation runs the reservation dry.
    for (u32 shape_index = 0; shape_index < BUSTER_ARRAY_LENGTH(c_cpython_shape_fixtures); shape_index += 1)
    {
        TemporalArena c_shape_temporary = scratch_begin(&arguments->arena, 1);
        String8 c_shape_path = buster_test_temporary_path(c_shape_temporary.arena, S8("buster-c-cpython-shape"),
                                                          string_format(c_shape_temporary.arena,
#if BUSTER_WINDOWS
                                                                        S8("-{u32}.exe"),
#else
                                                                        S8("-{u32}"),
#endif
                                                                        shape_index));
        String8 c_shape_command_line[] = {
            S8("-o"),
            c_shape_path,
            c_cpython_shape_fixtures[shape_index],
        };
        CompilerDriverResult c_shape = compiler_driver_execute_invocation(
            c_shape_temporary.arena, compiler_driver_parse_arguments(c_shape_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_shape_command_line)));
        if (c_shape.error != COMPILER_DRIVER_ERROR_NONE)
        {
            // Name the fixture and the driver's own words: a bare assertion
            // line says only that some shape failed, which is what made the
            // first non-Linux CI failure of this loop undiagnosable.
            arguments->show(arguments, S8("CPython shape fixture {S8} failed: {S8}\n"), c_cpython_shape_fixtures[shape_index], c_shape.diagnostic);
        }
        BUSTER_TEST(arguments, c_shape.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_shape.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 c_shape_arguments[] = {
                c_shape_path,
            };
            ProcessSpawnResult c_shape_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_shape_arguments), (SliceString8){0}, (SliceString8){0},
                                                                (ProcessSpawnOptions){
                                                                    .use_process_environment = true,
                                                                });
            BUSTER_TEST(arguments, c_shape_spawn.handle != 0);
            if (c_shape_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(c_shape_temporary.arena, c_shape_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(c_shape_temporary);
    }
    // The <float.h> predefine vocabulary, pinned as static initializers and
    // compared against literal spellings so a predefine folding to the wrong
    // bits fails at run time rather than compiling quietly.
    String8 c_float_limits_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-limits"),
#if BUSTER_WINDOWS
                                                             S8(".exe"));
#else
                                                             S8(""));
#endif
    String8 c_float_limits_command_line[] = {
        S8("-o"),
        c_float_limits_path,
        S8("tests/basic_c_float_limits.c"),
    };
    CompilerDriverResult c_float_limits = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_limits_command_line)));
    BUSTER_TEST(arguments, c_float_limits.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_limits.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_float_limits_arguments[] = {
            c_float_limits_path,
        };
        ProcessSpawnResult c_float_limits_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_limits_arguments), (SliceString8){0},
                                                                   (SliceString8){0},
                                                                   (ProcessSpawnOptions){
                                                                       .use_process_environment = true,
                                                                   });
        BUSTER_TEST(arguments, c_float_limits_spawn.handle != 0);
        if (c_float_limits_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_float_limits_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // `__typeof__` over a member read through a cast base -- CPython's
    // Py_SETREF writes `_Py_TYPEOF(((propertyobject *) new)->prop_name)` --
    // with the swap's answers checked.
    String8 c_typeof_cast_member_path = buster_test_temporary_path(arguments->arena, S8("buster-c-typeof-cast-member"),
#if BUSTER_WINDOWS
                                                                   S8(".exe"));
#else
                                                                   S8(""));
#endif
    String8 c_typeof_cast_member_command_line[] = {
        S8("-o"),
        c_typeof_cast_member_path,
        S8("tests/basic_c_typeof_cast_member.c"),
    };
    CompilerDriverResult c_typeof_cast_member = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_typeof_cast_member_command_line)));
    BUSTER_TEST(arguments, c_typeof_cast_member.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_typeof_cast_member.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_typeof_cast_member_arguments[] = {
            c_typeof_cast_member_path,
        };
        ProcessSpawnResult c_typeof_cast_member_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_typeof_cast_member_arguments),
                                                                         (SliceString8){0}, (SliceString8){0},
                                                                         (ProcessSpawnOptions){
                                                                             .use_process_environment = true,
                                                                         });
        BUSTER_TEST(arguments, c_typeof_cast_member_spawn.handle != 0);
        if (c_typeof_cast_member_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_typeof_cast_member_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A call whose callee is a member read off another call's result --
    // `Py_TYPE(self)->tp_free(self)`, how CPython frees every object -- with
    // the answers checked so a callee read off the wrong base returns the
    // wrong number rather than compiling quietly.
    String8 c_call_member_call_path = buster_test_temporary_path(arguments->arena, S8("buster-c-call-member-call"),
#if BUSTER_WINDOWS
                                                                 S8(".exe"));
#else
                                                                 S8(""));
#endif
    String8 c_call_member_call_command_line[] = {
        S8("-o"),
        c_call_member_call_path,
        S8("tests/basic_c_call_member_call.c"),
    };
    CompilerDriverResult c_call_member_call = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_call_member_call_command_line)));
    BUSTER_TEST(arguments, c_call_member_call.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_call_member_call.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_call_member_call_arguments[] = {
            c_call_member_call_path,
        };
        ProcessSpawnResult c_call_member_call_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_call_member_call_arguments), (SliceString8){0},
                                                                       (SliceString8){0},
                                                                       (ProcessSpawnOptions){
                                                                           .use_process_environment = true,
                                                                       });
        BUSTER_TEST(arguments, c_call_member_call_spawn.handle != 0);
        if (c_call_member_call_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_call_member_call_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // C11 6.7.6.3p8: a parameter declared with function type adjusts to a
    // pointer to that function, in both spellings.  CPython's pegen passes
    // its grammar rules as `RES_TYPE (func)(Parser *)`.
    String8 c_function_type_parameter_path = buster_test_temporary_path(arguments->arena, S8("buster-c-function-type-parameter"),
#if BUSTER_WINDOWS
                                                                        S8(".exe"));
#else
                                                                        S8(""));
#endif
    String8 c_function_type_parameter_command_line[] = {
        S8("-o"),
        c_function_type_parameter_path,
        S8("tests/basic_c_function_type_parameter.c"),
    };
    CompilerDriverResult c_function_type_parameter = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_function_type_parameter_command_line)));
    BUSTER_TEST(arguments, c_function_type_parameter.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_function_type_parameter.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_function_type_parameter_arguments[] = {
            c_function_type_parameter_path,
        };
        ProcessSpawnResult c_function_type_parameter_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_function_type_parameter_arguments),
                                                                              (SliceString8){0}, (SliceString8){0},
                                                                              (ProcessSpawnOptions){
                                                                                  .use_process_environment = true,
                                                                              });
        BUSTER_TEST(arguments, c_function_type_parameter_spawn.handle != 0);
        if (c_function_type_parameter_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_function_type_parameter_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A block-scope tag definition is its own type: two sibling functions
    // defining one tag are two layouts, an inner definition shadows the file
    // scope's, and the file scope keeps its own answer after the function
    // closes.  Sizes are asserted in every direction, so a resolver that
    // aliases tags by name across scopes fails at run time rather than
    // linking a wrong layout silently.
    String8 c_block_scope_tag_path = buster_test_temporary_path(arguments->arena, S8("buster-c-block-scope-tag"),
#if BUSTER_WINDOWS
                                                                S8(".exe"));
#else
                                                                S8(""));
#endif
    String8 c_block_scope_tag_command_line[] = {
        S8("-o"),
        c_block_scope_tag_path,
        S8("tests/basic_c_block_scope_tag.c"),
    };
    CompilerDriverResult c_block_scope_tag = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_block_scope_tag_command_line)));
    BUSTER_TEST(arguments, c_block_scope_tag.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_block_scope_tag.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_block_scope_tag_arguments[] = {
            c_block_scope_tag_path,
        };
        ProcessSpawnResult c_block_scope_tag_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_block_scope_tag_arguments), (SliceString8){0},
                                                                      (SliceString8){0},
                                                                      (ProcessSpawnOptions){
                                                                          .use_process_environment = true,
                                                                      });
        BUSTER_TEST(arguments, c_block_scope_tag_spawn.handle != 0);
        if (c_block_scope_tag_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_block_scope_tag_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A block-scope tag definition's suffix attribute list --
    // `struct S { ... } __attribute__((__packed__));` -- with and without a
    // declarator after the list, sizes asserted so a parse that skips the
    // list without applying it fails too.  clang's xmmintrin.h wraps its
    // unaligned loads in exactly this shape inside every intrinsic body.
    String8 c_local_suffix_attribute_path = buster_test_temporary_path(arguments->arena, S8("buster-c-local-suffix-attribute"),
#if BUSTER_WINDOWS
                                                                       S8(".exe"));
#else
                                                                       S8(""));
#endif
    String8 c_local_suffix_attribute_command_line[] = {
        S8("-o"),
        c_local_suffix_attribute_path,
        S8("tests/basic_c_local_struct_suffix_attribute.c"),
    };
    CompilerDriverResult c_local_suffix_attribute = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_local_suffix_attribute_command_line)));
    BUSTER_TEST(arguments, c_local_suffix_attribute.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_local_suffix_attribute.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_local_suffix_attribute_arguments[] = {
            c_local_suffix_attribute_path,
        };
        ProcessSpawnResult c_local_suffix_attribute_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_local_suffix_attribute_arguments),
                                                                             (SliceString8){0}, (SliceString8){0},
                                                                             (ProcessSpawnOptions){
                                                                                 .use_process_environment = true,
                                                                             });
        BUSTER_TEST(arguments, c_local_suffix_attribute_spawn.handle != 0);
        if (c_local_suffix_attribute_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_local_suffix_attribute_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // GNU's `, ## __VA_ARGS__` comma-deletion idiom, both ways and through a
    // forwarding macro layer: empty varargs delete the comma, present ones
    // are placed with no paste at all.  A real paste of `,` against the
    // first argument token refused every non-empty call, which is how
    // CPython's Parser/pegen.c stopped compiling.
    String8 c_comma_paste_path = buster_test_temporary_path(arguments->arena, S8("buster-c-variadic-comma-paste"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
    String8 c_comma_paste_command_line[] = {
        S8("-o"),
        c_comma_paste_path,
        S8("tests/basic_c_variadic_comma_paste.c"),
    };
    CompilerDriverResult c_comma_paste = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_comma_paste_command_line)));
    BUSTER_TEST(arguments, c_comma_paste.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_comma_paste.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_comma_paste_arguments[] = {
            c_comma_paste_path,
        };
        ProcessSpawnResult c_comma_paste_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_comma_paste_arguments), (SliceString8){0},
                                                                  (SliceString8){0},
                                                                  (ProcessSpawnOptions){
                                                                      .use_process_environment = true,
                                                                  });
        BUSTER_TEST(arguments, c_comma_paste_spawn.handle != 0);
        if (c_comma_paste_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_comma_paste_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
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
    // `_Atomic` over an aggregate is a *wider* type than its operand, because
    // the promotion pads it up to the next power of two (#731), so the bitcode
    // writer has to wrap the operand rather than hand back its id: an LLVM
    // module that stopped at the operand sizes the object short of what the
    // native object gives it (#767). The fixture holds the three positions a
    // type is built for -- a file-scope object, a member, a block-scope object
    // -- plus one atomic type that needs no padding and therefore still is its
    // operand's type. It runs natively as well, which pins the sizes the
    // bitcode has to agree with.
    TemporalArena atomic_bitcode_temporary = arena_begin_temporal(arguments->arena);
    String8 atomic_bitcode_output = buster_test_temporary_path(atomic_bitcode_temporary.arena, S8("buster-c-atomic-bitcode"), S8(".bc"));
    String8 atomic_bitcode_command_line[] = {
        S8("-emit-llvm"), S8("-c"), S8("-o"), atomic_bitcode_output, S8("tests/basic_c_atomic_bitcode.c"),
    };
    CompilerDriverResult atomic_bitcode = compiler_driver_execute_invocation(
        atomic_bitcode_temporary.arena,
        compiler_driver_parse_arguments(atomic_bitcode_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_bitcode_command_line)));
    if (atomic_bitcode.error != COMPILER_DRIVER_ERROR_NONE && atomic_bitcode.diagnostic.length)
    {
        arguments->show(arguments, S8("Atomic bitcode driver error: {S8}\n"), atomic_bitcode.diagnostic);
    }
    BUSTER_TEST(arguments, atomic_bitcode.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, atomic_bitcode.has_llvm_bitcode && atomic_bitcode.llvm_bitcode.success);
    BUSTER_TEST(arguments, atomic_bitcode.llvm_bitcode.bytes.length >= 4 && atomic_bitcode.llvm_bitcode.bytes.pointer[0] == 'B' &&
                               atomic_bitcode.llvm_bitcode.bytes.pointer[1] == 'C' && atomic_bitcode.llvm_bitcode.bytes.pointer[2] == 0xc0 &&
                               atomic_bitcode.llvm_bitcode.bytes.pointer[3] == 0xde);
    BUSTER_TEST(arguments, file_read(atomic_bitcode_temporary.arena, atomic_bitcode_output, (FileReadOptions){0}).length ==
                               atomic_bitcode.llvm_bitcode.bytes.length);
    String8 atomic_bitcode_native_path = buster_test_temporary_path(atomic_bitcode_temporary.arena, S8("buster-c-atomic-bitcode-native"), S8(""));
    String8 atomic_bitcode_native_command_line[] = {
        S8("-o"), atomic_bitcode_native_path, S8("tests/basic_c_atomic_bitcode.c"),
    };
    CompilerDriverResult atomic_bitcode_native = compiler_driver_execute_invocation(
        atomic_bitcode_temporary.arena,
        compiler_driver_parse_arguments(atomic_bitcode_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_bitcode_native_command_line)));
    BUSTER_TEST(arguments, atomic_bitcode_native.error == COMPILER_DRIVER_ERROR_NONE);
    if (atomic_bitcode_native.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 atomic_bitcode_native_arguments[] = {atomic_bitcode_native_path};
        ProcessSpawnResult atomic_bitcode_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_bitcode_native_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){.use_process_environment = true});
        BUSTER_TEST(arguments, atomic_bitcode_spawn.handle != 0);
        if (atomic_bitcode_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(atomic_bitcode_temporary.arena, atomic_bitcode_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(atomic_bitcode_temporary);
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
    // A sizeof operand is unevaluated but still an expression: a member chain
    // that reaches a resolved aggregate with no member of that name must be
    // refused, not handed to the type prediction as an int-sized guess.  The
    // guess is what autoconf's AC_CHECK_MEMBER fallback probe reads --
    // `if (sizeof ac_aggr.st_birthtime)` -- so it defined
    // HAVE_STRUCT_STAT_ST_BIRTHTIME, ST_FLAGS and ST_GEN on Linux for
    // CPython.  The fixture keeps the valid dot and arrow spellings beside
    // the missing one, so a diagnostic that reached past the missing name
    // changes the count here rather than passing.
    String8 c_sizeof_missing_member_path = buster_test_temporary_path(arguments->arena, S8("buster-c-sizeof-missing-member"), S8(".o"));
    String8 c_sizeof_missing_member_command_line[] = {
        S8("-c"),
        S8("-g0"),
        S8("-o"),
        c_sizeof_missing_member_path,
        S8("tests/basic_c_sizeof_missing_member.c"),
    };
    CompilerDriverResult c_sizeof_missing_member = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_sizeof_missing_member_command_line)));
    BUSTER_TEST(arguments, c_sizeof_missing_member.error == COMPILER_DRIVER_ERROR_ANALYSIS);
    BUSTER_TEST(arguments, c_sizeof_missing_member.tokenizer_error_count == 0 && c_sizeof_missing_member.parser_diagnostic_count == 0 &&
                           c_sizeof_missing_member.analysis_diagnostic_count == 1 && !c_sizeof_missing_member.has_object);
    BUSTER_TEST(arguments, string_ends_with_sequence(c_sizeof_missing_member.diagnostic,
                                                     S8("type 'stat_like' has no member named 'st_birthtime' (2 fields available)")));
    // Its sibling: `sizeof ((T))` is a type name where an expression is
    // required, which autoconf's AC_CHECK_TYPE compiles and requires to
    // fail.  The fixture keeps the one-pair type form and a many-pair
    // expression form beside the refused shape.
    String8 c_sizeof_paren_type_path = buster_test_temporary_path(arguments->arena, S8("buster-c-sizeof-paren-type"), S8(".o"));
    String8 c_sizeof_paren_type_command_line[] = {
        S8("-c"),
        S8("-g0"),
        S8("-o"),
        c_sizeof_paren_type_path,
        S8("tests/basic_c_sizeof_parenthesized_type.c"),
    };
    CompilerDriverResult c_sizeof_paren_type = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_sizeof_paren_type_command_line)));
    BUSTER_TEST(arguments, c_sizeof_paren_type.error == COMPILER_DRIVER_ERROR_ANALYSIS);
    BUSTER_TEST(arguments, c_sizeof_paren_type.tokenizer_error_count == 0 && c_sizeof_paren_type.parser_diagnostic_count == 0 &&
                           c_sizeof_paren_type.analysis_diagnostic_count == 1 && !c_sizeof_paren_type.has_object);
    BUSTER_TEST(arguments, string_ends_with_sequence(c_sizeof_paren_type.diagnostic,
                                                     S8("a parenthesized type name is not an expression; the type form takes exactly one pair of parentheses")));
    // A named bit-field of zero width is not a declaration C has: the zero
    // width belongs to the unnamed spelling, which declares no member and only
    // moves the next one to its type's boundary.  Accepted, it laid out a
    // member that occupies no bits and could still be assigned and read back,
    // which is a layout no other compiler on the target produces.  The fixture
    // carries both the literal and the constant-expression width -- the parse
    // folds only the literal one -- plus the unnamed spelling that stays
    // legal, so a diagnostic that reached too far fails this test rather than
    // passing it.  One report is issued per aggregate, so the two rejected
    // definitions are two diagnostics and the first is the one reported.
    String8 c_invalid_bit_field_width_path = buster_test_temporary_path(arguments->arena, S8("buster-c-invalid-bit-field-width"),
#if BUSTER_WINDOWS
                                                                        S8(".exe"));
#else
                                                                        S8(""));
#endif
    String8 c_invalid_bit_field_width_command_line[] = {
        S8("-g0"),
        S8("-o"),
        c_invalid_bit_field_width_path,
        S8("tests/basic_c_invalid_bit_field_width.c"),
    };
    CompilerDriverResult c_invalid_bit_field_width = compiler_driver_execute_invocation(
        arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_invalid_bit_field_width_command_line)));
    BUSTER_TEST(arguments, c_invalid_bit_field_width.error == COMPILER_DRIVER_ERROR_ANALYSIS);
    BUSTER_TEST(arguments, c_invalid_bit_field_width.tokenizer_error_count == 0 && c_invalid_bit_field_width.parser_diagnostic_count == 0 &&
                           c_invalid_bit_field_width.analysis_diagnostic_count == 2 && !c_invalid_bit_field_width.has_object);
    BUSTER_TEST(arguments, string_starts_with_sequence(c_invalid_bit_field_width.diagnostic,
                                                       S8("tests/basic_c_invalid_bit_field_width.c:17:9: named bit-field 'b' has zero width")));
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
        // What an inline-assembly operand class refuses, each case named where
        // it fails rather than left to the emitter, because "invalid IR" is not
        // what a source-level constraint is wrong about.  The four classes
        // themselves are positive fixtures now --
        // tests/basic_c_asm_{sse_output,sse_input,x87_output,x87_clobber}.c,
        // each run below -- so what is pinned here is the boundary around them:
        // a value the class cannot carry, a target that has no such register
        // file, and the stack shapes the x87 emitter's push/pop model does not
        // hold for.
        struct
        {
            String8 target;
            String8 source;
            String8 diagnostic;
        } refused_asm_operands[] = {
            {S8("x86_64-unknown-linux-gnu"), S8("int sse_integer(int value) { __asm__(\"sqrtsd %1, %0\" : \"=x\"(value) : \"x\"(value)); return value; }\n"),
             S8("an asm operand in the SSE register class must be a float or a double")},
            {S8("x86_64-unknown-linux-gnu"),
             S8("long double sse_long_double(long double value) { __asm__(\"sqrtsd %1, %0\" : \"=x\"(value) : \"x\"(value)); return value; }\n"),
             S8("an asm operand in the SSE register class must be a float or a double")},
            {S8("aarch64-unknown-linux-gnu"), S8("double sse_target(double value) { __asm__(\"fsqrt %d0, %d1\" : \"=x\"(value) : \"x\"(value)); return value; }\n"),
             S8("unsupported asm constraint for target")},
            {S8("x86_64-unknown-linux-gnu"), S8("double x87_double(double value) { __asm__(\"fsqrt\" : \"+t\"(value)); return value; }\n"),
             S8("an asm operand on the x87 register stack must be a long double")},
            {S8("aarch64-unknown-linux-gnu"), S8("double x87_target(double value) { __asm__(\"fsqrt\" : \"+t\"(value)); return value; }\n"),
             S8("unsupported asm constraint for target")},
            {S8("x86_64-unknown-linux-gnu"),
             S8("long double x87_below_alone(long double x, long double y) { __asm__(\"fprem\" : \"+r\"(x) : \"u\"(y)); return x; }\n"),
             S8("an asm operand in x87 st(1) requires one in st(0)")},
            {S8("x86_64-unknown-linux-gnu"),
             S8("long double x87_twice(long double x, long double y) { __asm__(\"fprem\" : \"+t\"(x) : \"t\"(y)); return x; }\n"),
             S8("asm names an x87 stack position more than once")},
            {S8("x86_64-unknown-linux-gnu"),
             S8("long double x87_popped_output(long double x) { __asm__(\"fsqrt\" : \"+t\"(x) : : \"st\"); return x; }\n"),
             S8("an asm that clobbers st must take exactly one x87 input and no output")},
            {S8("x86_64-unknown-linux-gnu"), S8("int x87_bare_clobber(int value) { __asm__(\"nop\" : \"+r\"(value) : : \"st\"); return value; }\n"),
             S8("an asm that clobbers st must take exactly one x87 input and no output")},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused_asm_operands); index += 1)
        {
            String8 refused_source_path = buster_test_temporary_path(arguments->arena, S8("buster-refused-asm-operand"),
                                                                     string_format(arguments->arena, S8("-{u32}.c"), index));
            BUSTER_TEST(arguments, file_write(refused_source_path, BUSTER_SLICE_TO_BYTE_SLICE(refused_asm_operands[index].source)));
            String8 refused_command_line[] = {
                S8("-c"),
                S8("-target"),
                refused_asm_operands[index].target,
                S8("-o"),
                buster_test_temporary_path(arguments->arena, S8("buster-refused-asm-operand"), string_format(arguments->arena, S8("-{u32}.o"), index)),
                refused_source_path,
            };
            CompilerDriverResult refused = compiler_driver_execute_invocation(
                arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(refused_command_line)));
            BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_ANALYSIS);
            BUSTER_TEST(arguments, refused.tokenizer_error_count == 0 && refused.parser_diagnostic_count == 0 &&
                                   refused.analysis_diagnostic_count == 1 && !refused.has_object);
            BUSTER_TEST(arguments, string_ends_with_sequence(refused.diagnostic, refused_asm_operands[index].diagnostic));
        }
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
        // A resolved local-exec sequence is the TPREL_HI12 add (shifted
        // form, usually #0) followed by the TPREL_LO12 add with a nonzero
        // resolved immediate, both on one register; the register is the
        // canonical emitter's x9 or whatever the machine path allocated,
        // so the probe matches the pair's form rather than one fixed word.
        bool found_thread_pointer_add = false;
        for (u64 byte_index = 0; byte_index + 8 <= executable.length; byte_index += 4)
        {
            u32 high_word = 0;
            u32 low_word = 0;
            memcpy(&high_word, executable.pointer + byte_index, sizeof(high_word));
            memcpy(&low_word, executable.pointer + byte_index + 4, sizeof(low_word));
            u32 destination = high_word & 31u;
            if ((high_word & 0xffc00000u) == 0x91400000u && ((high_word >> 5) & 31u) == destination &&
                (low_word & 0xffc00000u) == 0x91000000u && ((low_word >> 5) & 31u) == destination && (low_word & 31u) == destination &&
                ((low_word >> 10) & 0xfffu) != 0)
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
    // The Zig test/c_abi suite port (tests/c_abi.h documents the pairing):
    // both sides compile with the compiler under test and exchange every
    // supported ABI shape in both directions; any mismatch faults the
    // process, so a clean exit is the whole contract.
    String8 c_abi_path = buster_test_temporary_path(arguments->arena, S8("buster-c-abi"),
#if BUSTER_WINDOWS
                                                    S8(".exe"));
#else
                                                    S8(""));
#endif
    String8 c_abi_command_line[] = {
        S8("-o"), c_abi_path, S8("tests/c_abi_main.c"), S8("tests/c_abi_main_generated.c"), S8("tests/c_abi_cfuncs.c"),
    };
    // The three fixtures total tens of thousands of lines; a dedicated arena
    // keeps their compile out of the shared test reservation.
    Arena* c_abi_arena = arena_create((ArenaCreation){0});
    CompilerDriverResult c_abi = compiler_driver_execute_invocation(
        c_abi_arena, compiler_driver_parse_arguments(c_abi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_abi_command_line)));
    BUSTER_TEST(arguments, c_abi.error == COMPILER_DRIVER_ERROR_NONE);
    CompilerDriverError c_abi_error = c_abi.error;
    BUSTER_TEST(arguments, arena_destroy(c_abi_arena, 1));
    if (c_abi_error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_abi_arguments[] = {
            c_abi_path,
        };
        ProcessSpawnResult c_abi_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_abi_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .use_process_environment = true,
                                                          });
        BUSTER_TEST(arguments, c_abi_spawn.handle != 0);
        if (c_abi_spawn.handle)
        {
            ProcessWaitResult c_abi_wait = os_process_wait_sync(arguments->arena, c_abi_spawn);
            BUSTER_TEST(arguments, c_abi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // AAPCS64's sixteen-byte INTEGER pair needs both halves of the boundary:
    // C.8 rounds an odd X cursor to the next even register, while a pair that
    // reaches the stack rounds NSAA to an even eightbyte.  Build the same
    // freestanding fixture through every allocator; when qemu-aarch64 is
    // installed, execute each static image so the result also checks the
    // machine/canonical fallback boundary against the actual AArch64 ABI.
    String8 aarch64_i128_allocators[] = {
        S8("none"),
        S8("mir-stack"),
        S8("fast"),
        S8("quality"),
    };
    bool aarch64_i128_qemu_available = false;
#if !BUSTER_WINDOWS
    {
        String8 qemu_probe_arguments[] = {S8("qemu-aarch64"), S8("--version")};
        ProcessSpawnResult qemu_probe = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(qemu_probe_arguments), (SliceString8){0}, (SliceString8){0},
                                                         (ProcessSpawnOptions){
                                                             .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                             .use_process_environment = true,
                                                         });
        if (qemu_probe.handle)
        {
            aarch64_i128_qemu_available = os_process_wait_sync(arguments->arena, qemu_probe).result == PROCESS_RESULT_SUCCESS;
        }
    }
#endif
    for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(aarch64_i128_allocators); allocator_index += 1)
    {
        String8 aarch64_i128_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128"),
                                       string_format(arguments->arena, S8("-{S8}.elf"), aarch64_i128_allocators[allocator_index]));
        String8 aarch64_i128_command_line[] = {
            S8("-target"),
            S8("aarch64-unknown-linux-gnu"),
            string_format(arguments->arena, S8("-fregister-allocator={S8}"), aarch64_i128_allocators[allocator_index]),
            S8("-o"),
            aarch64_i128_path,
            S8("tests/basic_c_aarch64_i128_abi.c"),
        };
        Arena* aarch64_i128_arena = arena_create((ArenaCreation){0});
        CompilerDriverResult aarch64_i128 = compiler_driver_execute_invocation(
            aarch64_i128_arena, compiler_driver_parse_arguments(aarch64_i128_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(aarch64_i128_command_line)));
        BUSTER_TEST(arguments, aarch64_i128.error == COMPILER_DRIVER_ERROR_NONE);
        CompilerDriverError aarch64_i128_error = aarch64_i128.error;
        BUSTER_TEST(arguments, arena_destroy(aarch64_i128_arena, 1));
        if (aarch64_i128_error == COMPILER_DRIVER_ERROR_NONE && aarch64_i128_qemu_available)
        {
            String8 qemu_arguments[] = {S8("qemu-aarch64"), aarch64_i128_path};
            ProcessSpawnResult qemu_run = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(qemu_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
            BUSTER_TEST(arguments, qemu_run.handle != 0);
            if (qemu_run.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, qemu_run).result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    // The 128-bit binary subset both machine selectors carry (#811). The x86-64
    // one refused every operation in it -- add, subtract, the bitwise trio, two
    // of the three shift directions, the i128-typed constants and the ordered
    // comparisons -- while the AArch64 one selected them all, so the fixture is
    // built for both targets and run on both: natively here, and under
    // qemu-aarch64 where that is installed.
    //
    // The multiply came later, on the x86-64 side only, and it lives in a
    // function of its own in the fixture so the AArch64 lane can still pin
    // everything else at zero -- see the per-target expectation below.
    //
    // The fallback count is pinned because running alone proves nothing: the
    // canonical emitter answers identically, and a selector that stopped
    // firing would silently retire the coverage.
    String8 i128_binary_allocators[] = {
        S8("none"),
        S8("mir-stack"),
        S8("fast"),
        S8("quality"),
    };
    String8 i128_binary_targets[] = {
        S8("x86_64-unknown-linux-gnu"),
        S8("aarch64-unknown-linux-gnu"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(i128_binary_targets); target_index += 1)
    {
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(i128_binary_allocators); allocator_index += 1)
        {
            TemporalArena i128_binary_temporary = scratch_begin(&arguments->arena, 1);
            String8 i128_binary_path = buster_test_temporary_path(
                i128_binary_temporary.arena, S8("buster-c-i128-binary"),
                string_format(i128_binary_temporary.arena, S8("-{u32}-{S8}.elf"), target_index, i128_binary_allocators[allocator_index]));
            String8 i128_binary_command_line[] = {
                S8("-target"),
                i128_binary_targets[target_index],
                string_format(i128_binary_temporary.arena, S8("-fregister-allocator={S8}"), i128_binary_allocators[allocator_index]),
                S8("-o"),
                i128_binary_path,
                S8("tests/basic_c_x86_64_i128_binary.c"),
            };
            CompilerDriverResult i128_binary = compiler_driver_execute_invocation(
                i128_binary_temporary.arena,
                compiler_driver_parse_arguments(i128_binary_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(i128_binary_command_line)));
            BUSTER_TEST(arguments, i128_binary.error == COMPILER_DRIVER_ERROR_NONE);
            // NONE is the canonical emitter and reports no fallbacks by
            // definition. Under the machine modes x86-64 carries the whole
            // file; AArch64 carries all of it but `multiply_checks`, which is
            // why the multiply has a function of its own -- it needs a UMULH
            // row and that mnemonic has no generated form id yet (#810). The
            // day it lands this expectation becomes an unconditional zero.
            bool machine_mode = allocator_index != 0;
            u32 expected_i128_fallbacks = target_index == 1 && machine_mode ? 1u : 0u;
            BUSTER_TEST(arguments, i128_binary.codegen_statistics.fallback_function_count == expected_i128_fallbacks);
            bool native_x64 = target_index == 0 && BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64;
            bool emulated_a64 = target_index == 1 && aarch64_i128_qemu_available;
            if (i128_binary.error == COMPILER_DRIVER_ERROR_NONE && (native_x64 || emulated_a64))
            {
                String8 native_arguments[] = {i128_binary_path};
                String8 emulated_arguments[] = {S8("qemu-aarch64"), i128_binary_path};
                SliceString8 run_arguments = native_x64 ? (SliceString8)BUSTER_ARRAY_TO_SLICE(native_arguments)
                                                        : (SliceString8)BUSTER_ARRAY_TO_SLICE(emulated_arguments);
                ProcessSpawnResult i128_binary_run =
                    os_process_spawn(run_arguments, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, i128_binary_run.handle != 0);
                if (i128_binary_run.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(i128_binary_temporary.arena, i128_binary_run).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(i128_binary_temporary);
        }
    }
    // Two of the machine census's small-class tail (#813), which happen to be
    // the two that were not selector gaps at all.
    //
    // `basic_c_reversed_subscript.c`: `z[b] = 5` with a variable index recorded
    // the *place* of `z` as the INDEX's index operand where the normal spelling
    // records the value loaded out of it -- the swap that puts the pointer back
    // in the base position moved a place into a position that wants an rvalue.
    // Both machine selectors refused it and the canonical emitter loaded
    // through it, so the program was right and the census was not; the bitcode
    // writer emitted an invalid getelementptr for the same shape.
    //
    // `basic_c_lowered_alignment_frame.c`: a member whose type's alignment was
    // lowered sits at an offset its own size does not divide, and the AArch64
    // encoder's scaled unsigned immediate form cannot address that. It failed
    // closed instead of taking the scratch-register path it already takes for
    // an out-of-range offset, so a row that had selected came back as an
    // encode-stage fallback.
    //
    // Both pin the fallback count, because both produced correct answers
    // through the canonical emitter the whole time: only the count says the
    // machine path carries them now. Both build for both targets and run
    // wherever there is something to run them on.
    String8 census_tail_paths[] = {
        S8("tests/basic_c_reversed_subscript.c"),
        S8("tests/basic_c_lowered_alignment_frame.c"),
    };
    String8 census_tail_names[] = {
        S8("buster-c-reversed-subscript"),
        S8("buster-c-lowered-alignment-frame"),
    };
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(census_tail_paths); fixture_index += 1)
    {
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(i128_binary_targets); target_index += 1)
        {
            for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(i128_binary_allocators); allocator_index += 1)
            {
                TemporalArena census_tail_temporary = scratch_begin(&arguments->arena, 1);
                String8 census_tail_path = buster_test_temporary_path(
                    census_tail_temporary.arena, census_tail_names[fixture_index],
                    string_format(census_tail_temporary.arena, S8("-{u32}-{S8}.elf"), target_index, i128_binary_allocators[allocator_index]));
                String8 census_tail_command_line[] = {
                    S8("-target"),
                    i128_binary_targets[target_index],
                    string_format(census_tail_temporary.arena, S8("-fregister-allocator={S8}"), i128_binary_allocators[allocator_index]),
                    S8("-o"),
                    census_tail_path,
                    census_tail_paths[fixture_index],
                };
                CompilerDriverResult census_tail = compiler_driver_execute_invocation(
                    census_tail_temporary.arena,
                    compiler_driver_parse_arguments(census_tail_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(census_tail_command_line)));
                BUSTER_TEST(arguments, census_tail.error == COMPILER_DRIVER_ERROR_NONE);
                BUSTER_TEST(arguments, census_tail.codegen_statistics.fallback_function_count == 0u);
                bool census_native_x64 = target_index == 0 && BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64;
                bool census_emulated_a64 = target_index == 1 && aarch64_i128_qemu_available;
                if (census_tail.error == COMPILER_DRIVER_ERROR_NONE && (census_native_x64 || census_emulated_a64))
                {
                    String8 census_native_arguments[] = {census_tail_path};
                    String8 census_emulated_arguments[] = {S8("qemu-aarch64"), census_tail_path};
                    SliceString8 census_run_arguments = census_native_x64 ? (SliceString8)BUSTER_ARRAY_TO_SLICE(census_native_arguments)
                                                                         : (SliceString8)BUSTER_ARRAY_TO_SLICE(census_emulated_arguments);
                    ProcessSpawnResult census_tail_run = os_process_spawn(census_run_arguments, (SliceString8){0}, (SliceString8){0},
                                                                          (ProcessSpawnOptions){.use_process_environment = true});
                    BUSTER_TEST(arguments, census_tail_run.handle != 0);
                    if (census_tail_run.handle)
                    {
                        BUSTER_TEST(arguments, os_process_wait_sync(census_tail_temporary.arena, census_tail_run).result == PROCESS_RESULT_SUCCESS);
                    }
                }
                scratch_end(census_tail_temporary);
            }
        }
    }
    // The System V x86-64 side of the same stack boundary: a sixteen-aligned
    // argument's stack home is rounded up to a sixteen-aligned offset, and
    // the machine placement must count the same padding eightbytes the
    // canonical emitter and clang do — its callee used to read a stacked
    // __int128 one eightbyte early. The fixture's callees stay inside the
    // machine subset by forwarding their halves to the one checker that owns
    // the __int128 shifts, and the census pin below fails if lowering changes
    // ever push them to the canonical fallback, which would silently retire
    // this coverage. On an x86-64 Linux host each image also runs.
    String8 x64_i128_stack_allocators[] = {
        S8("none"),
        S8("mir-stack"),
        S8("fast"),
        S8("quality"),
    };
    for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(x64_i128_stack_allocators); allocator_index += 1)
    {
        String8 x64_i128_stack_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-x64-i128-stack"),
                                       string_format(arguments->arena, S8("-{S8}.elf"), x64_i128_stack_allocators[allocator_index]));
        String8 x64_i128_stack_command_line[] = {
            S8("-target"),
            S8("x86_64-unknown-linux-gnu"),
            string_format(arguments->arena, S8("-fregister-allocator={S8}"), x64_i128_stack_allocators[allocator_index]),
            S8("-o"),
            x64_i128_stack_path,
            S8("tests/basic_c_x86_64_i128_stack_abi.c"),
        };
        Arena* x64_i128_stack_arena = arena_create((ArenaCreation){0});
        CompilerDriverResult x64_i128_stack = compiler_driver_execute_invocation(
            x64_i128_stack_arena, compiler_driver_parse_arguments(x64_i128_stack_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(x64_i128_stack_command_line)));
        BUSTER_TEST(arguments, x64_i128_stack.error == COMPILER_DRIVER_ERROR_NONE);
        // NONE reports no fallbacks by definition; under the machine modes
        // exactly one function is left, and the four boundary callees remain
        // machine-selected. The count was two until the x86-64 selector picked
        // up the rest of the i128 binary subset (#811) and the checker's shifts
        // and comparisons started selecting; the one left refuses at a CALL,
        // not at an i128 operation.
        BUSTER_TEST(arguments, x64_i128_stack.codegen_statistics.fallback_function_count == (allocator_index == 0 ? 0u : 1u));
        CompilerDriverError x64_i128_stack_error = x64_i128_stack.error;
        BUSTER_TEST(arguments, arena_destroy(x64_i128_stack_arena, 1));
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        if (x64_i128_stack_error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 x64_i128_stack_run_arguments[] = {x64_i128_stack_path};
            ProcessSpawnResult x64_i128_stack_run =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(x64_i128_stack_run_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, x64_i128_stack_run.handle != 0);
            if (x64_i128_stack_run.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, x64_i128_stack_run).result == PROCESS_RESULT_SUCCESS);
            }
        }
#else
        BUSTER_UNUSED(x64_i128_stack_error);
#endif
    }
#if !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    // Pair the boundary translation units with clang in both directions.  A
    // Buster caller linked to a clang callee checks incoming placement and a
    // clang caller linked to a Buster callee checks outgoing placement and
    // bare pair results.  The tiny AArch64 _start object keeps this a static,
    // libc-free image that qemu can execute.
    bool aarch64_i128_clang_available = false;
    {
        String8 clang_probe_arguments[] = {S8("clang"), S8("--version")};
        ProcessSpawnResult clang_probe = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_probe_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                              .use_process_environment = true,
                                                          });
        if (clang_probe.handle)
        {
            aarch64_i128_clang_available = os_process_wait_sync(arguments->arena, clang_probe).result == PROCESS_RESULT_SUCCESS;
        }
    }
    if (aarch64_i128_clang_available)
    {
        String8 clang_callee_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128-clang-callee"), S8(".o"));
        String8 clang_caller_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128-clang-caller"), S8(".o"));
        String8 clang_start_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128-clang-start"), S8(".o"));
        String8 clang_compile_callee[] = {
            S8("clang"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-ffreestanding"), S8("-fno-builtin"), S8("-fno-stack-protector"),
            S8("-g0"), S8("-c"), S8("-o"), clang_callee_path, S8("tests/basic_c_aarch64_i128_callee.c"),
        };
        String8 clang_compile_caller[] = {
            S8("clang"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-ffreestanding"), S8("-fno-builtin"), S8("-fno-stack-protector"),
            S8("-g0"), S8("-c"), S8("-o"), clang_caller_path, S8("tests/basic_c_aarch64_i128_caller.c"),
        };
        String8 clang_compile_start[] = {
            S8("clang"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-g0"), S8("-c"), S8("-o"), clang_start_path,
            S8("tests/basic_c_aarch64_i128_start.S"),
        };
        ProcessSpawnOptions clang_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
        };
        ProcessSpawnResult clang_callee_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_compile_callee), (SliceString8){0}, (SliceString8){0}, clang_options);
        ProcessSpawnResult clang_caller_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_compile_caller), (SliceString8){0}, (SliceString8){0}, clang_options);
        ProcessSpawnResult clang_start_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_compile_start), (SliceString8){0}, (SliceString8){0}, clang_options);
        bool clang_callee_compiled = clang_callee_spawn.handle && os_process_wait_sync(arguments->arena, clang_callee_spawn).result == PROCESS_RESULT_SUCCESS;
        bool clang_caller_compiled = clang_caller_spawn.handle && os_process_wait_sync(arguments->arena, clang_caller_spawn).result == PROCESS_RESULT_SUCCESS;
        bool clang_start_compiled = clang_start_spawn.handle && os_process_wait_sync(arguments->arena, clang_start_spawn).result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST(arguments, clang_callee_compiled && clang_caller_compiled && clang_start_compiled);
        if (clang_callee_compiled && clang_caller_compiled && clang_start_compiled)
        {
            String8 buster_callee_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128-buster-callee"), S8(".o"));
            String8 buster_caller_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128-buster-caller"), S8(".o"));
            String8 buster_callee_command[] = {
                S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-fregister-allocator=none"), S8("-o"), buster_callee_path,
                S8("tests/basic_c_aarch64_i128_callee.c"),
            };
            String8 buster_caller_command[] = {
                S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-fregister-allocator=none"), S8("-o"), buster_caller_path,
                S8("tests/basic_c_aarch64_i128_caller.c"),
            };
            Arena* aarch64_i128_pair_arena = arena_create((ArenaCreation){0});
            CompilerDriverResult buster_callee = compiler_driver_execute_invocation(
                aarch64_i128_pair_arena, compiler_driver_parse_arguments(aarch64_i128_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_callee_command)));
            CompilerDriverResult buster_caller = compiler_driver_execute_invocation(
                aarch64_i128_pair_arena, compiler_driver_parse_arguments(aarch64_i128_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_caller_command)));
            BUSTER_TEST(arguments, buster_callee.error == COMPILER_DRIVER_ERROR_NONE && buster_caller.error == COMPILER_DRIVER_ERROR_NONE);
            CompilerDriverError buster_callee_error = buster_callee.error;
            CompilerDriverError buster_caller_error = buster_caller.error;
            BUSTER_TEST(arguments, arena_destroy(aarch64_i128_pair_arena, 1));
            if (buster_callee_error == COMPILER_DRIVER_ERROR_NONE && buster_caller_error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 buster_caller_clang_callee_path =
                    buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128-pair-bc"), S8(".elf"));
                String8 clang_caller_buster_callee_path =
                    buster_test_temporary_path(arguments->arena, S8("buster-c-aarch64-i128-pair-cb"), S8(".elf"));
                String8 buster_caller_clang_callee_command[] = {
                    S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), buster_caller_clang_callee_path, buster_caller_path, clang_callee_path,
                    clang_start_path,
                };
                String8 clang_caller_buster_callee_command[] = {
                    S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), clang_caller_buster_callee_path, clang_caller_path, buster_callee_path,
                    clang_start_path,
                };
                Arena* aarch64_i128_link_arena = arena_create((ArenaCreation){0});
                CompilerDriverResult buster_caller_clang_callee = compiler_driver_execute_invocation(
                    aarch64_i128_link_arena,
                    compiler_driver_parse_arguments(aarch64_i128_link_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_caller_clang_callee_command)));
                CompilerDriverResult clang_caller_buster_callee = compiler_driver_execute_invocation(
                    aarch64_i128_link_arena,
                    compiler_driver_parse_arguments(aarch64_i128_link_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_caller_buster_callee_command)));
                BUSTER_TEST(arguments, buster_caller_clang_callee.error == COMPILER_DRIVER_ERROR_NONE &&
                                           clang_caller_buster_callee.error == COMPILER_DRIVER_ERROR_NONE);
                CompilerDriverError buster_caller_clang_callee_error = buster_caller_clang_callee.error;
                CompilerDriverError clang_caller_buster_callee_error = clang_caller_buster_callee.error;
                BUSTER_TEST(arguments, arena_destroy(aarch64_i128_link_arena, 1));
                if (aarch64_i128_qemu_available && buster_caller_clang_callee_error == COMPILER_DRIVER_ERROR_NONE &&
                    clang_caller_buster_callee_error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 buster_caller_clang_callee_arguments[] = {S8("qemu-aarch64"), buster_caller_clang_callee_path};
                    String8 clang_caller_buster_callee_arguments[] = {S8("qemu-aarch64"), clang_caller_buster_callee_path};
                    ProcessSpawnResult buster_caller_clang_callee_spawn =
                        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(buster_caller_clang_callee_arguments), (SliceString8){0}, (SliceString8){0},
                                         (ProcessSpawnOptions){.use_process_environment = true});
                    ProcessSpawnResult clang_caller_buster_callee_spawn =
                        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_caller_buster_callee_arguments), (SliceString8){0}, (SliceString8){0},
                                         (ProcessSpawnOptions){.use_process_environment = true});
                    bool buster_caller_clang_callee_ran =
                        buster_caller_clang_callee_spawn.handle &&
                        os_process_wait_sync(arguments->arena, buster_caller_clang_callee_spawn).result == PROCESS_RESULT_SUCCESS;
                    bool clang_caller_buster_callee_ran =
                        clang_caller_buster_callee_spawn.handle &&
                        os_process_wait_sync(arguments->arena, clang_caller_buster_callee_spawn).result == PROCESS_RESULT_SUCCESS;
                    BUSTER_TEST(arguments, buster_caller_clang_callee_ran && clang_caller_buster_callee_ran);
                }
            }
        }
    }
#endif
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
    // Braced vector initializers: locals, statics, partial init zero-fill,
    // vectors nested in structs/arrays, and compound literals all lower
    // through the aggregate slot walks rather than the scalar conversion
    // path, so the fixture runs the full lane checks natively and must
    // still produce objects for every cross target.
    String8 c_vector_initializer_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-initializer"),
#if BUSTER_WINDOWS
                                                                   S8(".exe"));
#else
                                                                   S8(""));
#endif
    String8 c_vector_initializer_command_line[] = {
        S8("-o"),
        c_vector_initializer_path,
        S8("tests/basic_c_vector_initializer.c"),
    };
    CompilerDriverResult c_vector_initializer = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_initializer_command_line)));
    BUSTER_TEST(arguments, c_vector_initializer.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_vector_initializer.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_vector_initializer_arguments[] = {
            c_vector_initializer_path,
        };
        ProcessSpawnResult c_vector_initializer_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_initializer_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_vector_initializer_spawn.handle != 0);
        if (c_vector_initializer_spawn.handle)
        {
            ProcessWaitResult c_vector_initializer_wait = os_process_wait_sync(arguments->arena, c_vector_initializer_spawn);
            BUSTER_TEST(arguments, c_vector_initializer_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_vector_cross_targets); target_index += 1)
    {
        String8 c_vector_initializer_cross_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-initializer-cross"),
                                                                             string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_vector_initializer_cross_command_line[] = {
            S8("-c"), S8("-target"), c_vector_cross_targets[target_index], S8("-o"), c_vector_initializer_cross_path,
            S8("tests/basic_c_vector_initializer.c"),
        };
        CompilerDriverResult c_vector_initializer_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_initializer_cross_command_line)));
        BUSTER_TEST(arguments, c_vector_initializer_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_vector_initializer_cross.has_object);
    }
    // Incomplete extern arrays: `extern char pad[]` completed later in the
    // same unit (spelled bound, braced inference, string inference, either
    // declaration order) lowers with the composite type, and one never
    // completed lowers as an import — the two-file link resolves it against
    // the companion definition unit.
    String8 c_extern_incomplete_path = buster_test_temporary_path(arguments->arena, S8("buster-c-extern-incomplete-array"),
#if BUSTER_WINDOWS
                                                                  S8(".exe"));
#else
                                                                  S8(""));
#endif
    String8 c_extern_incomplete_command_line[] = {
        S8("-o"),
        c_extern_incomplete_path,
        S8("tests/basic_c_extern_incomplete_array.c"),
        S8("tests/basic_c_extern_incomplete_array_def.c"),
    };
    CompilerDriverResult c_extern_incomplete = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_extern_incomplete_command_line)));
    BUSTER_TEST(arguments, c_extern_incomplete.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_extern_incomplete.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_extern_incomplete_arguments[] = {
            c_extern_incomplete_path,
        };
        ProcessSpawnResult c_extern_incomplete_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_extern_incomplete_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_extern_incomplete_spawn.handle != 0);
        if (c_extern_incomplete_spawn.handle)
        {
            ProcessWaitResult c_extern_incomplete_wait = os_process_wait_sync(arguments->arena, c_extern_incomplete_spawn);
            BUSTER_TEST(arguments, c_extern_incomplete_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A C99 `static` array bound on a parameter is a bound qualifier, not a
    // storage class, so it must not make the function it belongs to internal.
    // Only a two-file link can see that: an internal definition the same unit
    // calls still works, and one nothing in its unit calls is dropped before
    // it reaches the object. The link itself succeeds either way -- the
    // missing definitions become dynamic imports -- so the run is the gate.
    String8 c_static_array_parameter_path = buster_test_temporary_path(arguments->arena, S8("buster-c-static-array-parameter"),
#if BUSTER_WINDOWS
                                                                       S8(".exe"));
#else
                                                                       S8(""));
#endif
    String8 c_static_array_parameter_command_line[] = {
        S8("-o"),
        c_static_array_parameter_path,
        S8("tests/basic_c_static_array_parameter.c"),
        S8("tests/basic_c_static_array_parameter_def.c"),
    };
    CompilerDriverResult c_static_array_parameter = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_array_parameter_command_line)));
    BUSTER_TEST(arguments, c_static_array_parameter.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_static_array_parameter.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_static_array_parameter_arguments[] = {
            c_static_array_parameter_path,
        };
        ProcessSpawnResult c_static_array_parameter_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_array_parameter_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_static_array_parameter_spawn.handle != 0);
        if (c_static_array_parameter_spawn.handle)
        {
            ProcessWaitResult c_static_array_parameter_wait = os_process_wait_sync(arguments->arena, c_static_array_parameter_spawn);
            BUSTER_TEST(arguments, c_static_array_parameter_wait.result == PROCESS_RESULT_SUCCESS);
        }
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
    // Every cross row above stops at an object file, and a calling-convention
    // defect is the one kind that survives that: a vector returned in the wrong
    // register assembles, links, and only disagrees when something calls it.
    // Where wine is installed the Windows target can be run for real instead.
    // The build takes the host's own model, so the code is what this machine
    // executes and the 512-bit body is compiled in exactly when the host has
    // the registers for it -- the default model the loop above uses would
    // preprocess that body away. Wine is optional: a host without it skips the
    // block, and a Windows host already runs these fixtures natively.
#if !BUSTER_WINDOWS
    if (target_native.cpu_arch == CPU_ARCH_X86_64)
    {
        ProcessSpawnOptions wine_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = 1,
        };
        String8 wine_probe_arguments[] = {
            S8("wine"),
            S8("--version"),
        };
        ProcessSpawnResult wine_probe =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(wine_probe_arguments), (SliceString8){0}, (SliceString8){0}, wine_options);
        bool wine_available = wine_probe.handle && os_process_wait_sync(arguments->arena, wine_probe).result == PROCESS_RESULT_SUCCESS;
        String8 wine_fixtures[] = {
            S8("tests/basic_c_vector.c"),
            S8("tests/basic_c_simd.c"),
            S8("tests/basic_c_wide_vector_argument.c"),
            // Returns 32-byte vectors by value, which Win64 now classifies as
            // a direct YMM0 result the way clang and MSVC do; the run proves
            // the callee's registers and the caller's expectations agree.
            S8("tests/basic_c_vector_argument_ymm.c"),
            // The other end of the Win64 width range: single-lane vectors
            // riding their element's register, sub-eightbyte vectors
            // returning in XMM0, and __int128 passed by reference and
            // returned in XMM0. These families used to disagree with clang
            // in both directions and were only visible when the two sides of
            // a call came from different compilers.
            S8("tests/basic_c_win64_narrow_abi.c"),
            // Vectors past 64 bytes: pieced indirect arguments and the
            // model-dependent direct-or-hidden-pointer result, at the host's
            // model here and at the pinned sub-AVX-512 models below.
            S8("tests/basic_c_vector_argument_wide.c"),
            // The PE entry stub calls the initializers itself, before the
            // argv machinery runs; nothing else in the image would.
            S8("tests/basic_c_constructor.c"),
        };
        for (u32 fixture_index = 0; wine_available && fixture_index < BUSTER_ARRAY_LENGTH(wine_fixtures); fixture_index += 1)
        {
            // Each -g build and link is a full pipeline run on the invocation
            // arena; releasing it per fixture keeps the block's footprint at
            // one build instead of the fixture count.
            TemporalArena wine_temporary = scratch_begin(&arguments->arena, 1);
            String8 wine_executable_path = buster_test_temporary_path(wine_temporary.arena, S8("buster-c-windows-run"),
                                                                      string_format(wine_temporary.arena, S8("-{u32}.exe"), fixture_index));
            String8 wine_build_command_line[] = {
                S8("-g"), S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-march=native"), S8("-o"), wine_executable_path, wine_fixtures[fixture_index],
            };
            CompilerDriverResult wine_build = compiler_driver_execute_invocation(
                wine_temporary.arena, compiler_driver_parse_arguments(wine_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wine_build_command_line)));
            BUSTER_TEST(arguments, wine_build.error == COMPILER_DRIVER_ERROR_NONE);
            if (wine_build.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 wine_run_arguments[] = {
                    S8("wine"),
                    wine_executable_path,
                };
                ProcessSpawnResult wine_run =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(wine_run_arguments), (SliceString8){0}, (SliceString8){0}, wine_options);
                BUSTER_TEST(arguments, wine_run.handle != 0);
                if (wine_run.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(wine_temporary.arena, wine_run).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(wine_temporary);
        }
        // The generated corpus is intentionally a single FAST lane in CI.
        // The full MIR_STACK/NONE/QUALITY matrix was run manually while
        // isolating the ABI defect; existing allocator-focused tests cover
        // those modes without multiplying this expensive generated compile.
        String8 wine_c_abi_allocator = S8("fast");
        String8 wine_c_abi_sources[] = {
            S8("tests/c_abi_main.c"),
            S8("tests/c_abi_main_generated.c"),
            S8("tests/c_abi_cfuncs.c"),
        };
        // Keep the objects on disk after their compile arena is released.  The
        // differential block below reuses these exact objects for both mixed
        // directions, so each Buster translation unit is built once.
        String8 wine_c_abi_buster_objects[3] = {0};
        bool wine_c_abi_buster_ready = false;
        if (wine_available)
        {
            Arena* wine_c_abi_arena = arena_create((ArenaCreation){
                .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
            });
            BUSTER_TEST(arguments, wine_c_abi_arena != 0);
            if (!wine_c_abi_arena)
            {
                wine_available = false;
            }
            String8 wine_c_abi_path = buster_test_temporary_path(
                arguments->arena, S8("buster-c-abi-windows"), S8("-fast.exe"));
            bool buster_objects_ready = wine_c_abi_arena != 0;
            for (u32 source_index = 0; wine_c_abi_arena && source_index < BUSTER_ARRAY_LENGTH(wine_c_abi_sources); source_index += 1)
            {
                wine_c_abi_buster_objects[source_index] = buster_test_temporary_path(
                    arguments->arena, S8("buster-c-abi-buster"),
                    string_format(arguments->arena, S8("-fast-{u32}.obj"), source_index));
                String8 wine_c_abi_compile_command_line[] = {
                    S8("-c"),
                    S8("-g0"),
                    S8("-target"),
                    S8("x86_64-pc-windows-msvc"),
                    S8("-march=native"),
                    S8("-fregister-allocator=fast"),
                    S8("-o"),
                    wine_c_abi_buster_objects[source_index],
                    wine_c_abi_sources[source_index],
                };
                // Keep the allocator value attached to its option; the
                // argument parser accepts this single spelling.
                wine_c_abi_compile_command_line[5] = string_format(
                    wine_c_abi_arena, S8("-fregister-allocator={S8}"), wine_c_abi_allocator);
                CompilerDriverResult wine_c_abi_compile = compiler_driver_execute_invocation(
                    wine_c_abi_arena,
                    compiler_driver_parse_arguments(wine_c_abi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wine_c_abi_compile_command_line)));
                BUSTER_TEST(arguments, wine_c_abi_compile.error == COMPILER_DRIVER_ERROR_NONE && wine_c_abi_compile.has_object);
                buster_objects_ready &= wine_c_abi_compile.error == COMPILER_DRIVER_ERROR_NONE && wine_c_abi_compile.has_object;
            }
            wine_c_abi_buster_ready = buster_objects_ready;
            BUSTER_TEST(arguments, wine_c_abi_buster_ready);
            if (buster_objects_ready)
            {
                String8 wine_c_abi_link_command_line[] = {
                    S8("-target"),
                    S8("x86_64-pc-windows-msvc"),
                    S8("-march=native"),
                    S8("-o"),
                    wine_c_abi_path,
                    wine_c_abi_buster_objects[0],
                    wine_c_abi_buster_objects[1],
                    wine_c_abi_buster_objects[2],
                };
                CompilerDriverResult wine_c_abi_link = compiler_driver_execute_invocation(
                    wine_c_abi_arena,
                    compiler_driver_parse_arguments(wine_c_abi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wine_c_abi_link_command_line)));
                BUSTER_TEST(arguments, wine_c_abi_link.error == COMPILER_DRIVER_ERROR_NONE);
                if (wine_c_abi_link.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 wine_c_abi_run_arguments[] = {S8("wine"), wine_c_abi_path};
                    ProcessSpawnResult wine_c_abi_run =
                        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(wine_c_abi_run_arguments), (SliceString8){0}, (SliceString8){0}, wine_options);
                    BUSTER_TEST(arguments, wine_c_abi_run.handle != 0);
                    if (wine_c_abi_run.handle)
                    {
                        BUSTER_TEST(arguments, os_process_wait_sync(wine_c_abi_arena, wine_c_abi_run).result == PROCESS_RESULT_SUCCESS);
                    }
                }
            }
            if (wine_c_abi_arena)
            {
                BUSTER_TEST(arguments, arena_destroy(wine_c_abi_arena, 1));
            }
        }
#if defined(BUSTER_HOST_C_COMPILER)
        // Differential rows keep one side in the host Clang object writer and
        // the other in buster's Win64 codegen.  The generated cfuncs fixture
        // carries freestanding memcpy/memset helpers, so this lane has no CRT
        // dependency and tests the ABI plus the `_fltused` runtime policy.
        // A standalone lld-link reproduction must pass /stack:0x800000:
        // lld's 1 MiB default overflows the wide-vector fixture, while the
        // Buster PE linker reserves 8 MiB.  The driver links below use the
        // latter policy directly, so this is not an ABI mismatch.
        bool wine_clang_available = wine_available;
        String8 wine_clang_probe_arguments[] = {S8(BUSTER_HOST_C_COMPILER), S8("--version")};
        if (wine_clang_available)
        {
            ProcessSpawnResult wine_clang_probe = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(wine_clang_probe_arguments), (SliceString8){0},
                                                                    (SliceString8){0}, wine_options);
            wine_clang_available = wine_clang_probe.handle && os_process_wait_sync(arguments->arena, wine_clang_probe).result == PROCESS_RESULT_SUCCESS;
        }
        if (wine_clang_available)
        {
            TemporalArena wine_mixed_temporary = scratch_begin(&arguments->arena, 1);
            Arena* wine_mixed_arena = wine_mixed_temporary.arena;
            String8 wine_clang_sources[] = {
                S8("tests/c_abi_main.c"),
                S8("tests/c_abi_main_generated.c"),
                S8("tests/c_abi_cfuncs.c"),
            };
            String8 wine_clang_objects[3] = {0};
            for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(wine_clang_sources); source_index += 1)
            {
                wine_clang_objects[source_index] =
                    buster_test_temporary_path(wine_mixed_arena, S8("buster-c-abi-clang"), string_format(wine_mixed_arena, S8("-{u32}.obj"), source_index));
                String8 wine_clang_compile_arguments[] = {
                    S8(BUSTER_HOST_C_COMPILER),
                    S8("-target"),
                    S8("x86_64-pc-windows-msvc"),
                    S8("-march=native"),
                    S8("-O0"),
                    S8("-g0"),
                    S8("-c"),
                    S8("-o"),
                    wine_clang_objects[source_index],
                    wine_clang_sources[source_index],
                };
                ProcessSpawnResult wine_clang_compile =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(wine_clang_compile_arguments), (SliceString8){0}, (SliceString8){0}, wine_options);
                BUSTER_TEST(arguments, wine_clang_compile.handle != 0);
                if (!wine_clang_compile.handle || os_process_wait_sync(wine_mixed_arena, wine_clang_compile).result != PROCESS_RESULT_SUCCESS)
                {
                    wine_clang_available = false;
                    break;
                }
            }
            if (wine_c_abi_buster_ready && wine_clang_available)
            {
                Arena* mode_arena = arena_create((ArenaCreation){
                    .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
                });
                BUSTER_TEST(arguments, mode_arena != 0);
                if (mode_arena)
                {
                    String8 mixed_executables[] = {
                        buster_test_temporary_path(mode_arena, S8("buster-c-abi-mixed-clang-main"), S8(".exe")),
                        buster_test_temporary_path(mode_arena, S8("buster-c-abi-mixed-buster-main"), S8(".exe")),
                    };
                    String8 mixed_link_commands[][8] = {
                        {
                            S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-march=native"), S8("-o"), mixed_executables[0],
                            wine_clang_objects[0], wine_clang_objects[1], wine_c_abi_buster_objects[2],
                        },
                        {
                            S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-march=native"), S8("-o"), mixed_executables[1],
                            wine_c_abi_buster_objects[0], wine_c_abi_buster_objects[1], wine_clang_objects[2],
                        },
                    };
                    for (u32 direction = 0; direction < BUSTER_ARRAY_LENGTH(mixed_executables); direction += 1)
                    {
                        CompilerDriverResult mixed_link = compiler_driver_execute_invocation(
                            mode_arena, compiler_driver_parse_arguments(mode_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(mixed_link_commands[direction])));
                        BUSTER_TEST(arguments, mixed_link.error == COMPILER_DRIVER_ERROR_NONE);
                        if (mixed_link.error == COMPILER_DRIVER_ERROR_NONE)
                        {
                            String8 mixed_run_arguments[] = {S8("wine"), mixed_executables[direction]};
                            ProcessSpawnResult mixed_run = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(mixed_run_arguments), (SliceString8){0},
                                                                             (SliceString8){0}, wine_options);
                            BUSTER_TEST(arguments, mixed_run.handle != 0);
                            if (mixed_run.handle)
                            {
                                BUSTER_TEST(arguments, os_process_wait_sync(mode_arena, mixed_run).result == PROCESS_RESULT_SUCCESS);
                            }
                        }
                    }
                    BUSTER_TEST(arguments, arena_destroy(mode_arena, 1));
                }
            }
            scratch_end(wine_mixed_temporary);
        }
#endif
        // The wide fixture again at pinned sub-AVX-512 models, run under
        // wine: the eight- and four-piece argument straddles and the
        // hidden-pointer wide result only exist below the host's likely
        // model, so the native row cannot reach them. nehalem needs SSE4.2
        // and haswell AVX2, both of which every runner the suite executes on
        // provides -- the -march=native rows above already assume at least
        // as much.
        String8 wine_wide_models[] = {S8("-march=nehalem"), S8("-march=haswell")};
        for (u32 model_index = 0; wine_available && model_index < BUSTER_ARRAY_LENGTH(wine_wide_models); model_index += 1)
        {
            TemporalArena wine_wide_temporary = scratch_begin(&arguments->arena, 1);
            String8 wine_wide_path = buster_test_temporary_path(wine_wide_temporary.arena, S8("buster-c-windows-wide"),
                                                                string_format(wine_wide_temporary.arena, S8("-{u32}.exe"), model_index));
            String8 wine_wide_command_line[] = {
                S8("-g"),
                S8("-target"),
                S8("x86_64-pc-windows-msvc"),
                wine_wide_models[model_index],
                S8("-o"),
                wine_wide_path,
                S8("tests/basic_c_vector_argument_wide.c"),
            };
            CompilerDriverResult wine_wide_build = compiler_driver_execute_invocation(
                wine_wide_temporary.arena,
                compiler_driver_parse_arguments(wine_wide_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wine_wide_command_line)));
            BUSTER_TEST(arguments, wine_wide_build.error == COMPILER_DRIVER_ERROR_NONE);
            if (wine_wide_build.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 wine_wide_run_arguments[] = {
                    S8("wine"),
                    wine_wide_path,
                };
                ProcessSpawnResult wine_wide_run =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(wine_wide_run_arguments), (SliceString8){0}, (SliceString8){0}, wine_options);
                BUSTER_TEST(arguments, wine_wide_run.handle != 0);
                if (wine_wide_run.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(wine_wide_temporary.arena, wine_wide_run).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(wine_wide_temporary);
        }
    }
#endif

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
    // A conditional assignment yields the assigned member's type.  Keep the
    // cJSON_SetBoolValue shape inline with its comparison so type prediction
    // cannot retain the pointer type of the object operand.
    String8 c_conditional_assignment_path = buster_test_temporary_path(arguments->arena, S8("buster-c-conditional-assignment"), S8(""));
    String8 c_conditional_assignment_command_line[] = {
        S8("-o"),
        c_conditional_assignment_path,
        S8("tests/basic_c_conditional_assignment.c"),
    };
    CompilerDriverResult c_conditional_assignment = compiler_driver_execute_invocation(
        arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conditional_assignment_command_line)));
    BUSTER_TEST(arguments, c_conditional_assignment.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_conditional_assignment.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_conditional_assignment_arguments[] = {
            c_conditional_assignment_path,
        };
        ProcessSpawnResult c_conditional_assignment_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_conditional_assignment_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_conditional_assignment_spawn.handle != 0);
        if (c_conditional_assignment_spawn.handle)
        {
            ProcessWaitResult c_conditional_assignment_wait = os_process_wait_sync(arguments->arena, c_conditional_assignment_spawn);
            BUSTER_TEST(arguments, c_conditional_assignment_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A qualified typedef'd enum keeps the same declaration identity across
    // a function prototype and definition.  Unity relies on this exact
    // redeclaration shape for UnityPrintNumberByStyle.
    String8 c_enum_prototype_path = buster_test_temporary_path(arguments->arena, S8("buster-c-enum-prototype"), S8(""));
    String8 c_enum_prototype_command_line[] = {
        S8("-o"),
        c_enum_prototype_path,
        S8("tests/basic_c_enum_prototype.c"),
    };
    CompilerDriverResult c_enum_prototype = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_enum_prototype_command_line)));
    BUSTER_TEST(arguments, c_enum_prototype.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_enum_prototype.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_enum_prototype_arguments[] = {
            c_enum_prototype_path,
        };
        ProcessSpawnResult c_enum_prototype_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_enum_prototype_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_enum_prototype_spawn.handle != 0);
        if (c_enum_prototype_spawn.handle)
        {
            ProcessWaitResult c_enum_prototype_wait = os_process_wait_sync(arguments->arena, c_enum_prototype_spawn);
            BUSTER_TEST(arguments, c_enum_prototype_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // An array of aggregates decays to a pointer for `->` access.  Keep both
    // the field assignment and the value read as a call argument here: the
    // original lowering loaded the whole array and then asked field access to
    // treat that ARRAY value as a POINTER.
    String8 c_array_arrow_path = buster_test_temporary_path(arguments->arena, S8("buster-c-array-arrow"), S8(""));
    String8 c_array_arrow_command_line[] = {
        S8("-o"),
        c_array_arrow_path,
        S8("tests/basic_c_array_arrow.c"),
    };
    CompilerDriverResult c_array_arrow = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_array_arrow_command_line)));
    BUSTER_TEST(arguments, c_array_arrow.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_array_arrow.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_array_arrow_arguments[] = {
            c_array_arrow_path,
        };
        ProcessSpawnResult c_array_arrow_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_array_arrow_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_array_arrow_spawn.handle != 0);
        if (c_array_arrow_spawn.handle)
        {
            ProcessWaitResult c_array_arrow_wait = os_process_wait_sync(arguments->arena, c_array_arrow_spawn);
            BUSTER_TEST(arguments, c_array_arrow_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Explicit casts apply the same array-to-pointer decay as ordinary
    // expressions.  Exercise both pointer and integer destinations so the
    // cast lowering cannot retain the source ARRAY type.
    String8 c_array_cast_path = buster_test_temporary_path(arguments->arena, S8("buster-c-array-cast"), S8(""));
    String8 c_array_cast_command_line[] = {
        S8("-o"),
        c_array_cast_path,
        S8("tests/basic_c_array_cast.c"),
    };
    CompilerDriverResult c_array_cast = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_array_cast_command_line)));
    BUSTER_TEST(arguments, c_array_cast.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_array_cast.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_array_cast_arguments[] = {
            c_array_cast_path,
        };
        ProcessSpawnResult c_array_cast_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_array_cast_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_array_cast_spawn.handle != 0);
        if (c_array_cast_spawn.handle)
        {
            ProcessWaitResult c_array_cast_wait = os_process_wait_sync(arguments->arena, c_array_cast_spawn);
            BUSTER_TEST(arguments, c_array_cast_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Prefix updates over a dereferenced pointer are full expressions, not
    // the identifier-only assignment fast path.  Exercise both directions
    // at runtime so the produced place and its updated value are observable.
    String8 c_prefix_increment_path = buster_test_temporary_path(arguments->arena, S8("buster-c-prefix-increment"), S8(""));
    String8 c_prefix_increment_command_line[] = {
        S8("-o"),
        c_prefix_increment_path,
        S8("tests/basic_c_prefix_increment.c"),
    };
    CompilerDriverResult c_prefix_increment = compiler_driver_execute_invocation(
        arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_prefix_increment_command_line)));
    BUSTER_TEST(arguments, c_prefix_increment.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_prefix_increment.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_prefix_increment_arguments[] = {
            c_prefix_increment_path,
        };
        ProcessSpawnResult c_prefix_increment_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_prefix_increment_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_prefix_increment_spawn.handle != 0);
        if (c_prefix_increment_spawn.handle)
        {
            ProcessWaitResult c_prefix_increment_wait = os_process_wait_sync(arguments->arena, c_prefix_increment_spawn);
            BUSTER_TEST(arguments, c_prefix_increment_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // zlib's public tests combine several C frontend edge shapes in one
    // expression: indirect calls through (*fp)(...), nested subscript
    // postfix updates, comma-heavy pointer updates, gzgetc's conditional and
    // character-literal macro, and glibc's __extension__ statement expression.
    String8 c_zlib_regressions_path = buster_test_temporary_path(arguments->arena, S8("buster-c-zlib-regressions"), S8(""));
    String8 c_zlib_regressions_command_line[] = {
        S8("-o"),
        c_zlib_regressions_path,
        S8("tests/basic_c_zlib_regressions.c"),
    };
    CompilerDriverResult c_zlib_regressions = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_zlib_regressions_command_line)));
    BUSTER_TEST(arguments, c_zlib_regressions.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_zlib_regressions.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_zlib_regressions_arguments[] = {c_zlib_regressions_path};
        ProcessSpawnResult c_zlib_regressions_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_zlib_regressions_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){.use_process_environment = true});
        BUSTER_TEST(arguments, c_zlib_regressions_spawn.handle != 0);
        if (c_zlib_regressions_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_zlib_regressions_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // The stb compatibility harness reduced three independent frontend
    // failures to these small runtime shapes: address-of an array/aggregate
    // PLACE, a postfix update through a parenthesized pointer-arithmetic
    // index, and a comma expression retained inside a conditional arm.
    String8 c_stb_regressions_path = buster_test_temporary_path(arguments->arena, S8("buster-c-stb-regressions"), S8(""));
    String8 c_stb_regressions_command_line[] = {
        S8("-o"),
        c_stb_regressions_path,
        S8("tests/basic_c_stb_regressions.c"),
    };
    CompilerDriverResult c_stb_regressions = compiler_driver_execute_invocation(
        arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_stb_regressions_command_line)));
    BUSTER_TEST(arguments, c_stb_regressions.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_stb_regressions.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_stb_regressions_arguments[] = {c_stb_regressions_path};
        ProcessSpawnResult c_stb_regressions_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_stb_regressions_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){.use_process_environment = true});
        BUSTER_TEST(arguments, c_stb_regressions_spawn.handle != 0);
        if (c_stb_regressions_spawn.handle)
        {
            BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, c_stb_regressions_spawn).result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Hosted math headers spell NAN and the classification helpers through
    // compiler builtins.  Keep the quiet-NaN value and finite isinf result
    // observable at runtime so these are not merely accepted identifiers.
    String8 c_builtin_math_path = buster_test_temporary_path(arguments->arena, S8("buster-c-builtin-math"), S8(""));
    String8 c_builtin_math_command_line[] = {
        S8("-o"),
        c_builtin_math_path,
        S8("tests/basic_c_builtin_math.c"),
    };
    CompilerDriverResult c_builtin_math = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_builtin_math_command_line)));
    BUSTER_TEST(arguments, c_builtin_math.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_builtin_math.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_builtin_math_arguments[] = {
            c_builtin_math_path,
        };
        ProcessSpawnResult c_builtin_math_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_builtin_math_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_builtin_math_spawn.handle != 0);
        if (c_builtin_math_spawn.handle)
        {
            ProcessWaitResult c_builtin_math_wait = os_process_wait_sync(arguments->arena, c_builtin_math_spawn);
            BUSTER_TEST(arguments, c_builtin_math_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // yyjson compatibility reduced four independent frontend failures to
    // these focused runtime fixtures: brace-elided nested aggregates,
    // __builtin_inff/isfinite, decimal precision macros, and narrow frame
    // loads after pointer-written scalar values.
    // Keep each source separate so a future regression identifies the exact
    // language or builtin contract that broke.
    String8 c_yyjson_regression_paths[] = {
        S8("tests/basic_c_nested_aggregate_scalar.c"),
        S8("tests/basic_c_builtin_inff.c"),
        S8("tests/basic_c_decimal_digits.c"),
        S8("tests/basic_c_narrow_frame_load.c"),
    };
    String8 c_yyjson_regression_names[] = {
        S8("buster-c-nested-aggregate-scalar"),
        S8("buster-c-builtin-inff"),
        S8("buster-c-decimal-digits"),
        S8("buster-c-narrow-frame-load"),
    };
    for (u64 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_yyjson_regression_paths); fixture_index += 1)
    {
        String8 fixture_path = buster_test_temporary_path(arguments->arena, c_yyjson_regression_names[fixture_index], S8(""));
        String8 fixture_command_line[] = {S8("-o"), fixture_path, c_yyjson_regression_paths[fixture_index]};
        CompilerDriverResult fixture = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
        BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
        if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 fixture_arguments[] = {fixture_path};
            ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, fixture_spawn.handle != 0);
            if (fixture_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(arguments->arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    // The narrow-load fixture also guards the machine lowering itself.  A
    // direct branch must normalize the byte written through the escaped
    // pointer before testing the frame value; checking the x86 assembly keeps
    // this regression deterministic even on a host where a fresh stack happens
    // to contain zeroes above the byte.
    String8 narrow_frame_assembly_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_narrow_frame_load.c"),
    };
    CompilerDriverResult narrow_frame_assembly = compiler_driver_execute_invocation(
        arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(narrow_frame_assembly_command_line)));
    BUSTER_TEST(arguments, narrow_frame_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    u64 narrow_call = string_first_sequence(narrow_frame_assembly.output, S8("call \"write_narrow_values\"\n"));
    BUSTER_TEST(arguments, narrow_call != BUSTER_STRING_NO_MATCH);
    if (narrow_call != BUSTER_STRING_NO_MATCH)
    {
        String8 after_call = string_slice(narrow_frame_assembly.output, narrow_call, narrow_frame_assembly.output.length);
        u64 normalize = string_first_sequence(after_call, S8("movzx "));
        u64 branch_test = string_first_sequence(after_call, S8("test "));
        BUSTER_TEST(arguments, normalize != BUSTER_STRING_NO_MATCH && branch_test != BUSTER_STRING_NO_MATCH && normalize < branch_test);
    }
    // LZ4 compatibility reduced six independent frontend and lowering
    // failures to these focused runtime fixtures: the __builtin_mem* family,
    // a case label standing as a control statement's substatement, a call to
    // a noreturn callee ending control flow, the address of an array lvalue,
    // a block-local typedef shadowing an outer one, and a goto into a loop
    // body whose end-of-iteration stack restore had no matching save.
    // Keep each source separate so a future regression identifies the exact
    // language or lowering contract that broke, and run each one under every
    // register allocator, because three of the six are lowering rather than
    // parsing defects.  The seventh joined them later: `noreturn` spelled on a
    // function pointer type or typedef rather than on a declaration, which the
    // assembly assertion below is what actually gates -- running it only proves
    // the bodies lower.
    String8 c_lz4_regression_paths[] = {
        S8("tests/basic_c_builtin_memory.c"),
        S8("tests/basic_c_case_substatement.c"),
        S8("tests/basic_c_noreturn_call.c"),
        S8("tests/basic_c_noreturn_type.c"),
        S8("tests/basic_c_address_of_array.c"),
        S8("tests/basic_c_local_typedef_scope.c"),
        S8("tests/basic_c_goto_into_loop.c"),
    };
    String8 c_lz4_regression_names[] = {
        S8("buster-c-builtin-memory"),
        S8("buster-c-case-substatement"),
        S8("buster-c-noreturn-call"),
        S8("buster-c-noreturn-type"),
        S8("buster-c-address-of-array"),
        S8("buster-c-local-typedef-scope"),
        S8("buster-c-goto-into-loop"),
    };
    String8 c_lz4_regression_allocators[] = {
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=none"),
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=quality"),
    };
    for (u64 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_paths); fixture_index += 1)
    {
        for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
        {
            // Each fixture compiles into a scratch arena rather than the
            // module's own: this table runs one whole in-process compilation
            // per fixture per allocator, and the module arena is never
            // rewound, so accumulating them there spends its reservation and
            // the next unrelated invocation is the one that fails.
            TemporalArena fixture_temporary = scratch_begin(&arguments->arena, 1);
            String8 fixture_path = buster_test_temporary_path(fixture_temporary.arena, c_lz4_regression_names[fixture_index], S8(""));
            String8 fixture_command_line[] = {
                c_lz4_regression_allocators[allocator_index], S8("-o"), fixture_path, c_lz4_regression_paths[fixture_index],
            };
            CompilerDriverResult fixture = compiler_driver_execute_invocation(
                fixture_temporary.arena, compiler_driver_parse_arguments(fixture_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
            BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
            if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 fixture_arguments[] = {fixture_path};
                ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, fixture_spawn.handle != 0);
                if (fixture_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(fixture_temporary.arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(fixture_temporary);
        }
    }
    // The musl compatibility inventory reduced four independent frontend
    // singletons to these fixtures: a variable-length array declared in a
    // comma-separated declarator list, a pointer to a variably modified array
    // and the partially subscripted array that decays out of one, an inline
    // aggregate definition sized in a file-scope initializer, and a control
    // statement standing as another one's unbraced substatement.  Each source
    // is separate so a regression names the contract that broke, and each runs
    // under every register allocator because three of the four are lowering
    // rather than parsing defects.
    String8 c_musl_singleton_paths[] = {
        S8("tests/basic_c_vla_declarator_list.c"),
        S8("tests/basic_c_pointer_to_vla.c"),
        S8("tests/basic_c_inline_aggregate_sizeof.c"),
        S8("tests/basic_c_nested_control_substatement.c"),
    };
    String8 c_musl_singleton_names[] = {
        S8("buster-c-vla-declarator-list"),
        S8("buster-c-pointer-to-vla"),
        S8("buster-c-inline-aggregate-sizeof"),
        S8("buster-c-nested-control-substatement"),
    };
    for (u64 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_musl_singleton_paths); fixture_index += 1)
    {
        for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
        {
            // Scratch-arena compilation for the reason the table above gives:
            // the module arena is never rewound, and one whole in-process
            // compilation per fixture per allocator would spend it.
            TemporalArena fixture_temporary = scratch_begin(&arguments->arena, 1);
            String8 fixture_path = buster_test_temporary_path(fixture_temporary.arena, c_musl_singleton_names[fixture_index], S8(""));
            String8 fixture_command_line[] = {
                c_lz4_regression_allocators[allocator_index], S8("-o"), fixture_path, c_musl_singleton_paths[fixture_index],
            };
            CompilerDriverResult fixture = compiler_driver_execute_invocation(
                fixture_temporary.arena, compiler_driver_parse_arguments(fixture_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
            BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
            if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 fixture_arguments[] = {fixture_path};
                ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, fixture_spawn.handle != 0);
                if (fixture_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(fixture_temporary.arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(fixture_temporary);
        }
    }
    // A call through a noreturn function pointer type ends control flow, and
    // nothing after it in the block is emitted.  The defect that motivated the
    // fixture only ever produced dead code, so running the program cannot see
    // it: every shape in the fixture calls `must_not_be_reached` from a spot
    // its noreturn call dominates, and the whole assertion is that the name
    // reaches the assembly nowhere.  Clang's own -S output for the same file
    // does not mention it either.
    String8 noreturn_type_assembly_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_noreturn_type.c"),
    };
    CompilerDriverResult noreturn_type_assembly = compiler_driver_execute_invocation(
        arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(noreturn_type_assembly_command_line)));
    BUSTER_TEST(arguments, noreturn_type_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(noreturn_type_assembly.output, S8("must_not_be_reached")) == BUSTER_STRING_NO_MATCH);
    // The noreturn fixture also guards which declarations the marker is read
    // from, and only the emitted code shows that: a call to a noreturn callee
    // is followed by the terminator alone, while a call that returns reaches
    // the store of the fall-off-the-end return value first.  The x86 backend
    // emits both terminators as raw bytes -- 0x0f 0x0b is ud2 -- so what
    // stands between the call and those bytes is what tells the two apart.
    String8 noreturn_assembly_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_noreturn_call.c"),
    };
    CompilerDriverResult noreturn_assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(noreturn_assembly_command_line)));
    BUSTER_TEST(arguments, noreturn_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    // The marker has to reach a call through the declarator that owns it,
    // through the specifiers every declarator of a list shares, through a
    // later declaration of the same function, and through its definition.
    String8 noreturn_terminated_calls[] = {
        S8("call \"list_noreturn\"\n"),
        S8("call \"list_specifier_first\"\n"),
        S8("call \"list_specifier_second\"\n"),
        S8("call \"redeclared_noreturn\"\n"),
        S8("call \"defined_noreturn\"\n"),
    };
    for (u64 call_index = 0; call_index < BUSTER_ARRAY_LENGTH(noreturn_terminated_calls); call_index += 1)
    {
        u64 noreturn_call = string_first_sequence(noreturn_assembly.output, noreturn_terminated_calls[call_index]);
        BUSTER_TEST(arguments, noreturn_call != BUSTER_STRING_NO_MATCH);
        if (noreturn_call != BUSTER_STRING_NO_MATCH)
        {
            String8 after_call = string_slice(noreturn_assembly.output, noreturn_call + noreturn_terminated_calls[call_index].length,
                                              noreturn_assembly.output.length);
            BUSTER_TEST(arguments, string_starts_with_sequence(after_call, S8("\t.byte 0x0f, 0x0b")));
        }
    }
    // The declarator beside list_noreturn carries no marker of its own, so its
    // own call falls through.  Scanning the whole declarator list marked it
    // too, and the ud2 planted after this call is what the fixture then
    // executed at run time.
    u64 sibling_body = string_first_sequence(noreturn_assembly.output, S8("through_list_sibling:\n"));
    BUSTER_TEST(arguments, sibling_body != BUSTER_STRING_NO_MATCH);
    if (sibling_body != BUSTER_STRING_NO_MATCH)
    {
        String8 sibling_assembly = string_slice(noreturn_assembly.output, sibling_body, noreturn_assembly.output.length);
        u64 sibling_call = string_first_sequence(sibling_assembly, S8("call \"list_returns\"\n"));
        BUSTER_TEST(arguments, sibling_call != BUSTER_STRING_NO_MATCH);
        if (sibling_call != BUSTER_STRING_NO_MATCH)
        {
            String8 after_sibling_call = string_slice(sibling_assembly, sibling_call + S8("call \"list_returns\"\n").length, sibling_assembly.length);
            BUSTER_TEST(arguments, string_starts_with_sequence(after_sibling_call, S8("\tmov eax, 0x0")));
        }
    }
    // SQLite compatibility reduced its own set of frontend, lowering and
    // linker failures to these fixtures: declarators that return function
    // pointers and the type names that cast to them, case labels inside a
    // block of a switch body, the value of an update on a narrow object, the
    // address of an indexed pointer, a braced string literal in an array of
    // pointers, calls through a postfix chain, and the glibc exit-handler
    // stubs that live outside libc.so.6.  They run under every register
    // allocator for the same reason the LZ4 table does: several of them are
    // lowering rather than parsing defects.
    String8 c_sqlite_regression_paths[] = {
        S8("tests/basic_c_function_pointer_declarators.c"),
        S8("tests/basic_c_switch_case_blocks.c"),
        S8("tests/basic_c_narrow_place_update.c"),
        S8("tests/basic_c_pointer_index_address.c"),
        S8("tests/basic_c_pointer_array_initializers.c"),
        S8("tests/basic_c_indirect_call_targets.c"),
        S8("tests/basic_c_atexit_handler.c"),
    };
    String8 c_sqlite_regression_names[] = {
        S8("buster-c-function-pointer-declarators"),
        S8("buster-c-switch-case-blocks"),
        S8("buster-c-narrow-place-update"),
        S8("buster-c-pointer-index-address"),
        S8("buster-c-pointer-array-initializers"),
        S8("buster-c-indirect-call-targets"),
        S8("buster-c-atexit-handler"),
    };
    for (u64 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_sqlite_regression_paths); fixture_index += 1)
    {
        for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
        {
            TemporalArena fixture_temporary = scratch_begin(&arguments->arena, 1);
            String8 fixture_path = buster_test_temporary_path(fixture_temporary.arena, c_sqlite_regression_names[fixture_index], S8(""));
            String8 fixture_command_line[] = {
                c_lz4_regression_allocators[allocator_index], S8("-o"), fixture_path, c_sqlite_regression_paths[fixture_index],
            };
            CompilerDriverResult fixture = compiler_driver_execute_invocation(
                fixture_temporary.arena, compiler_driver_parse_arguments(fixture_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
            BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
            if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 fixture_arguments[] = {fixture_path};
                ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, fixture_spawn.handle != 0);
                if (fixture_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(fixture_temporary.arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(fixture_temporary);
        }
    }
    // #792: on a PE target that fixture used to be unlinkable.  `atexit` and
    // `at_quick_exit` are not ucrtbase.dll exports -- UCRT keeps them in its
    // import library as one call apiece to `_crt_atexit` and
    // `_crt_at_quick_exit` -- and this driver imports from the DLL alone, so
    // the names resolved nowhere.  link_windows_libc_runtime_object supplies
    // them, and two contracts hold on every host.  The link never fails on
    // `atexit` again: a machine with a readable ucrtbase.dll (a Windows host,
    // or a Linux one with `-L` on a directory holding it) links the program
    // outright, and a machine without one fails on the `_crt_` import the
    // stub introduced instead, which is the same wall every other libc call
    // hits there.  And an image that registers no handler carries no `_crt_`
    // import at all, which is what the archive-member selection buys over
    // adding the stubs to every executable the way `_fltused` is added.
    String8 c_windows_exit_targets[] = {
        S8("x86_64-pc-windows-msvc"),
        S8("aarch64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_windows_exit_targets); target_index += 1)
    {
        TemporalArena exit_temporary = scratch_begin(&arguments->arena, 1);
        String8 windows_exit_path =
            buster_test_temporary_path(exit_temporary.arena, S8("buster-c-windows-atexit"), string_format(exit_temporary.arena, S8("-{u32}.exe"), target_index));
        String8 windows_exit_command_line[] = {
            S8("-target"), c_windows_exit_targets[target_index], S8("-o"), windows_exit_path, S8("tests/basic_c_atexit_handler.c"),
        };
        CompilerDriverResult windows_exit = compiler_driver_execute_invocation(
            exit_temporary.arena, compiler_driver_parse_arguments(exit_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_exit_command_line)));
        BUSTER_TEST(arguments, windows_exit.error == COMPILER_DRIVER_ERROR_NONE ||
                                   string_starts_with_sequence(windows_exit.native_link.symbol, S8("_crt_")));
        if (windows_exit.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(windows_exit.native_link.executable, S8("_crt_atexit")));
        }
        String8 windows_handlerless_path = buster_test_temporary_path(exit_temporary.arena, S8("buster-c-windows-no-atexit"),
                                                                     string_format(exit_temporary.arena, S8("-{u32}.exe"), target_index));
        String8 windows_handlerless_command_line[] = {
            S8("-target"), c_windows_exit_targets[target_index], S8("-o"), windows_handlerless_path, S8("tests/basic_c_vector.c"),
        };
        CompilerDriverResult windows_handlerless = compiler_driver_execute_invocation(
            exit_temporary.arena, compiler_driver_parse_arguments(exit_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_handlerless_command_line)));
        BUSTER_TEST(arguments, windows_handlerless.error == COMPILER_DRIVER_ERROR_NONE);
        if (windows_handlerless.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, !compiler_driver_bytes_contain(windows_handlerless.native_link.executable, S8("_crt_atexit")));
            BUSTER_TEST(arguments, !compiler_driver_bytes_contain(windows_handlerless.native_link.executable, S8("_crt_at_quick_exit")));
        }
        scratch_end(exit_temporary);
    }
    // The object side of the same fixture, checked for a fixed cross target so
    // every host runs it: the array is what an external linker consumes.  This
    // model has one section per kind, so the converter sorts a translation
    // unit's whole array into one section -- 101 then 150 then the one written
    // without a priority -- and records each entry's priority beside it; the
    // ELF writer then splits that back into the `.init_array.NNNNN` sections
    // `ld` orders across translation units by.  Both halves are checked below.
    {
        String8 constructor_object_command_line[] = {
            S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_constructor.c"),
        };
        CompilerDriverResult constructor_object = compiler_driver_execute_invocation(
            arguments->arena,
            compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constructor_object_command_line)));
        BUSTER_TEST(arguments, constructor_object.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, constructor_object.has_object);
        if (constructor_object.has_object)
        {
            ObjectFile* initializer_object = &constructor_object.object;
            BUSTER_TEST(arguments, initializer_object->sections[OBJECT_SECTION_INIT_ARRAY].data.length == 3 * OBJECT_INITIALIZER_ENTRY_SIZE);
            BUSTER_TEST(arguments, initializer_object->sections[OBJECT_SECTION_FINI_ARRAY].data.length == OBJECT_INITIALIZER_ENTRY_SIZE);
            String8 initializer_expected_names[] = {
                S8("with_earlier_priority"), S8("with_later_priority"), S8("without_priority"), S8("at_exit"),
            };
            u32 initializer_expected_sections[] = {
                OBJECT_SECTION_INIT_ARRAY, OBJECT_SECTION_INIT_ARRAY, OBJECT_SECTION_INIT_ARRAY, OBJECT_SECTION_FINI_ARRAY,
            };
            u64 initializer_expected_offsets[] = {0, OBJECT_INITIALIZER_ENTRY_SIZE, 2 * OBJECT_INITIALIZER_ENTRY_SIZE, 0};
            u32 initializer_found = 0;
            for (u32 relocation_index = 0; relocation_index < initializer_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation relocation = initializer_object->relocations[relocation_index];
                if (relocation.section != OBJECT_SECTION_INIT_ARRAY && relocation.section != OBJECT_SECTION_FINI_ARRAY)
                {
                    continue;
                }
                BUSTER_TEST(arguments, initializer_found < BUSTER_ARRAY_LENGTH(initializer_expected_names));
                if (initializer_found >= BUSTER_ARRAY_LENGTH(initializer_expected_names))
                {
                    break;
                }
                BUSTER_TEST(arguments, relocation.kind == OBJECT_RELOCATION_ABSOLUTE64);
                BUSTER_TEST(arguments, relocation.addend == 0);
                BUSTER_TEST(arguments, relocation.section == initializer_expected_sections[initializer_found]);
                BUSTER_TEST(arguments, relocation.offset == initializer_expected_offsets[initializer_found]);
                BUSTER_TEST(arguments, relocation.symbol < initializer_object->symbol_count);
                if (relocation.symbol < initializer_object->symbol_count)
                {
                    BUSTER_TEST(arguments, string_equal(initializer_object->symbols[relocation.symbol].name, initializer_expected_names[initializer_found]));
                    // A registered function is reachable by definition, so the
                    // unused-static elimination has to have kept its body.
                    BUSTER_TEST(arguments, initializer_object->symbols[relocation.symbol].section == OBJECT_SECTION_TEXT);
                    BUSTER_TEST(arguments, !initializer_object->symbols[relocation.symbol].global);
                }
                initializer_found += 1;
            }
            BUSTER_TEST(arguments, initializer_found == BUSTER_ARRAY_LENGTH(initializer_expected_names));
            // And the written ELF, which is what an external linker actually
            // reads (issue 782).  `ld` orders the arrays by section name --
            // every `.init_array.NNNNN` ahead of the unsuffixed `.init_array`,
            // ascending -- so the sorted slot order above only settles the
            // order inside one translation unit; the split into one section
            // per priority group is what settles it against another's.  The
            // model still has one section per kind: the split lives in the ELF
            // writer, so it is only observable here, on the bytes.
            ObjectArtifact initializer_artifact = object_write(arguments->arena, initializer_object, OBJECT_FORMAT_ELF64);
            BUSTER_TEST(arguments, initializer_artifact.error == OBJECT_ERROR_NONE);
            String8 initializer_group_names[] = {
                S8(".init_array.00101"), S8(".init_array.00150"), S8(".init_array"), S8(".fini_array"),
            };
            String8 initializer_group_symbols[] = {
                S8("with_earlier_priority"), S8("with_later_priority"), S8("without_priority"), S8("at_exit"),
            };
            for (u64 group_index = 0; group_index < BUSTER_ARRAY_LENGTH(initializer_group_names); group_index += 1)
            {
                u64 group_offset = 0;
                u64 group_size = 0;
                u64 group_address = 0;
                BUSTER_TEST(arguments,
                            compiler_driver_test_elf_section_find(initializer_artifact.bytes, initializer_group_names[group_index], &group_offset, &group_size,
                                                                  &group_address));
                // One entry each: the two suffixed sections hold exactly the
                // constructor written with that priority, and the unsuffixed
                // ones hold only what named none.
                BUSTER_TEST(arguments, group_size == OBJECT_INITIALIZER_ENTRY_SIZE);
                BUSTER_TEST(arguments, string_equal(compiler_driver_test_elf_initializer_symbol(arguments->arena, initializer_artifact.bytes,
                                                                                               initializer_group_names[group_index]),
                                                    initializer_group_symbols[group_index]));
            }
        }
    }
    // __attribute__((constructor)) and ((destructor)) (issue 771).  The
    // fixture is every part of the contract at once: a constructor that
    // writes a global `main` reads, two more with priorities that pin the
    // order against the one written without, and a destructor `main` must not
    // yet have seen.  It runs under all four allocators because the array is
    // filled by the object writer and called by the linker, and a definition
    // the allocator path rejected would drop out of both.
    //
    // Apple is included: the Mach-O writer synthesizes no entry stub, but it
    // does not need one -- it keeps `__DATA,__mod_init_func` and gives it the
    // section type dyld dispatches on, so the loader calls the entries before
    // it enters `main` (issue 779).  It is only the constructor half that
    // this proves, on any target: the destructor here is one `main` must not
    // yet have seen, which holds whether or not it ever runs.  The fixture
    // that observes a destructor running is basic_c_destructor_exit.c below.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
    {
        TemporalArena constructor_temporary = scratch_begin(&arguments->arena, 1);
        String8 constructor_path = buster_test_temporary_path(constructor_temporary.arena, S8("buster-c-constructor"), S8(""));
        String8 constructor_command_line[] = {
            c_lz4_regression_allocators[allocator_index], S8("-o"), constructor_path, S8("tests/basic_c_constructor.c"),
        };
        CompilerDriverResult constructor_build = compiler_driver_execute_invocation(
            constructor_temporary.arena,
            compiler_driver_parse_arguments(constructor_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constructor_command_line)));
        BUSTER_TEST(arguments, constructor_build.error == COMPILER_DRIVER_ERROR_NONE);
        if (constructor_build.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 constructor_arguments[] = {constructor_path};
            ProcessSpawnResult constructor_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(constructor_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, constructor_spawn.handle != 0);
            if (constructor_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(constructor_temporary.arena, constructor_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(constructor_temporary);
    }
    // The same fixture compiled to an object first and linked from it, which
    // is the one path where the array reaches the linker through a format
    // reader instead of straight from the module.  The ELF writer splits the
    // array into one `.init_array.NNNNN` section per priority group and leaves
    // the unsuffixed one at its fixed kind index, ahead of them (issue 782),
    // so the reader has to merge those back in `ld`'s order rather than in
    // section header order: a header-order merge runs `without_priority`
    // first, which is exit status 2 out of the fixture rather than a link that
    // fails.
    {
        TemporalArena constructor_object_temporary = scratch_begin(&arguments->arena, 1);
        String8 constructor_object_path = buster_test_temporary_path(constructor_object_temporary.arena, S8("buster-c-constructor-object"), S8(".o"));
        String8 constructor_image_path = buster_test_temporary_path(constructor_object_temporary.arena, S8("buster-c-constructor-from-object"), S8(""));
        String8 constructor_compile_command_line[] = {
            S8("-c"), S8("-o"), constructor_object_path, S8("tests/basic_c_constructor.c"),
        };
        CompilerDriverResult constructor_compile = compiler_driver_execute_invocation(
            constructor_object_temporary.arena,
            compiler_driver_parse_arguments(constructor_object_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constructor_compile_command_line)));
        BUSTER_TEST(arguments, constructor_compile.error == COMPILER_DRIVER_ERROR_NONE);
        if (constructor_compile.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 constructor_link_command_line[] = {
                S8("-o"), constructor_image_path, constructor_object_path,
            };
            CompilerDriverResult constructor_link = compiler_driver_execute_invocation(
                constructor_object_temporary.arena,
                compiler_driver_parse_arguments(constructor_object_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constructor_link_command_line)));
            BUSTER_TEST(arguments, constructor_link.error == COMPILER_DRIVER_ERROR_NONE);
            if (constructor_link.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 constructor_image_arguments[] = {constructor_image_path};
                ProcessSpawnResult constructor_image_spawn =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(constructor_image_arguments), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, constructor_image_spawn.handle != 0);
                if (constructor_image_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(constructor_object_temporary.arena, constructor_image_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
        scratch_end(constructor_object_temporary);
    }
    // The other way a program ends: `main` calls `exit` instead of returning,
    // and the destructor still has to run (issue 781).  The entry stub cannot
    // call it where `main` came back, because it never comes back, so the
    // hosted stubs register a runner with the C runtime and this is what
    // proves the registration landed -- the fixture reports its own verdict as
    // the process status, and the status a skipped destructor would leave is
    // the `exit(1)` `main` asked for.  It also pins the order against handlers
    // the program registered itself, which is only right if the runner was
    // registered before the constructors ran.
    //
    // The PE image is reached by a Windows host running this same loop rather
    // than by the wine block further up -- the fixture calls `exit`, and a
    // cross host has no ucrtbase.dll to resolve that against.
    //
    // Apple is included, and was excluded until issue 798 measured why it
    // failed: dyld does not run a main executable's `__mod_term_func`, so the
    // Mach-O writer registers the walk with `atexit` from an initializer slot
    // it prepends ahead of the program's constructors.  That gives Apple the
    // same order the entry-stub writers produce, which is what the expected
    // sequence below is written against -- Clang's own macOS shape registers
    // each destructor as its initializer is reached and would not produce it.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
    {
        TemporalArena destructor_temporary = scratch_begin(&arguments->arena, 1);
        String8 destructor_path = buster_test_temporary_path(destructor_temporary.arena, S8("buster-c-destructor-exit"), S8(""));
        String8 destructor_command_line[] = {
            c_lz4_regression_allocators[allocator_index], S8("-o"), destructor_path, S8("tests/basic_c_destructor_exit.c"),
        };
        CompilerDriverResult destructor_build = compiler_driver_execute_invocation(
            destructor_temporary.arena,
            compiler_driver_parse_arguments(destructor_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(destructor_command_line)));
        BUSTER_TEST(arguments, destructor_build.error == COMPILER_DRIVER_ERROR_NONE);
        if (destructor_build.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 destructor_arguments[] = {destructor_path};
            ProcessSpawnResult destructor_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(destructor_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, destructor_spawn.handle != 0);
            if (destructor_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(destructor_temporary.arena, destructor_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(destructor_temporary);
    }
    // The order across two translation units, which is the half one file
    // cannot show (issue 789).  GNU runs every prioritized constructor before
    // every unprioritized one over the whole program, and the two fixtures
    // interleave: 101 and 150 come from the second unit, 120 from the first,
    // and the two that named no priority run last in link order.  The fixture
    // returns the position that ran out of order, so a linker that
    // concatenated the two arrays exits 2 rather than failing to link.
    {
        TemporalArena order_temporary = scratch_begin(&arguments->arena, 1);
        Arena* order_arena = order_temporary.arena;
        // Compiled in one invocation, which is the route every host can run:
        // the priorities reach the linker straight from the converter, so no
        // object format has to be able to spell them.
        String8 order_source_image_path = buster_test_temporary_path(order_arena, S8("buster-c-constructor-order-source"), S8(""));
        String8 order_source_command[] = {
            S8("-o"), order_source_image_path, S8("tests/basic_c_constructor_order.c"), S8("tests/basic_c_constructor_order_second.c"),
        };
        CompilerDriverResult order_source = compiler_driver_execute_invocation(
            order_arena, compiler_driver_parse_arguments(order_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(order_source_command)));
        BUSTER_TEST(arguments, order_source.error == COMPILER_DRIVER_ERROR_NONE);
        if (order_source.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 order_source_arguments[] = {order_source_image_path};
            ProcessSpawnResult order_source_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(order_source_arguments), (SliceString8){0},
                                                                     (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, order_source_spawn.handle != 0);
            if (order_source_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(order_arena, order_source_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
#if !BUSTER_APPLE && !BUSTER_IOS
        // The same two units through `-c` and a second invocation, where the
        // priorities have to survive a relocatable object.  Two of the three
        // formats can state one, each in its own linker's convention -- ELF as
        // `.init_array.NNNNN`, COFF in the lexicographically ordered
        // `.CRT$XC*` group -- so this half runs everywhere but Mach-O, whose
        // section name has no room past `__mod_init_func` and whose platform
        // has no other carrier (issue 795).  That format still keeps the
        // arrays across a round trip, so what it loses is the cross-object
        // *priority*, not the constructors.
        String8 order_first_object = buster_test_temporary_path(order_arena, S8("buster-c-constructor-order-first"), S8(".o"));
        String8 order_second_object = buster_test_temporary_path(order_arena, S8("buster-c-constructor-order-second"), S8(".o"));
        String8 order_first_command[] = {
            S8("-c"), S8("-o"), order_first_object, S8("tests/basic_c_constructor_order.c"),
        };
        String8 order_second_command[] = {
            S8("-c"), S8("-o"), order_second_object, S8("tests/basic_c_constructor_order_second.c"),
        };
        CompilerDriverResult order_first = compiler_driver_execute_invocation(
            order_arena, compiler_driver_parse_arguments(order_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(order_first_command)));
        CompilerDriverResult order_second = compiler_driver_execute_invocation(
            order_arena, compiler_driver_parse_arguments(order_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(order_second_command)));
        BUSTER_TEST(arguments, order_first.error == COMPILER_DRIVER_ERROR_NONE && order_second.error == COMPILER_DRIVER_ERROR_NONE);
        // The first unit holds `main` and one of the unprioritized
        // constructors, and it is given to the linker first: the two
        // unprioritized ones run in link order, so the argument order is part
        // of what the fixture asserts.
        if (order_first.error == COMPILER_DRIVER_ERROR_NONE && order_second.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 order_image_path = buster_test_temporary_path(order_arena, S8("buster-c-constructor-order"), S8(""));
            String8 order_link_command[] = {
                S8("-o"), order_image_path, order_first_object, order_second_object,
            };
            CompilerDriverResult order_link = compiler_driver_execute_invocation(
                order_arena, compiler_driver_parse_arguments(order_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(order_link_command)));
            BUSTER_TEST(arguments, order_link.error == COMPILER_DRIVER_ERROR_NONE);
            if (order_link.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 order_image_arguments[] = {order_image_path};
                ProcessSpawnResult order_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(order_image_arguments), (SliceString8){0},
                                                                  (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, order_spawn.handle != 0);
                if (order_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(order_arena, order_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
#endif
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_APPLE && !BUSTER_ANDROID && !BUSTER_IOS
        // The same two units built by the host compiler, which is the cheapest
        // oracle for this: it depends on nothing this object writer does, and
        // it spells a priority group `.init_array.101` where the writer here
        // spells it `.init_array.00101`, so it is also what pins the reader to
        // both forms.  -fno-pic and -g0 for the reasons the clang object
        // fixture above gives.
        String8 host_first_object = buster_test_temporary_path(order_arena, S8("buster-c-constructor-order-host-first"), S8(".o"));
        String8 host_second_object = buster_test_temporary_path(order_arena, S8("buster-c-constructor-order-host-second"), S8(".o"));
        String8 host_first_command[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_first_object,
            S8("tests/basic_c_constructor_order.c"),
        };
        String8 host_second_command[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_second_object,
            S8("tests/basic_c_constructor_order_second.c"),
        };
        ProcessSpawnOptions host_order_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
        };
        ProcessSpawnResult host_first_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_first_command), (SliceString8){0}, (SliceString8){0}, host_order_options);
        ProcessSpawnResult host_second_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_second_command), (SliceString8){0}, (SliceString8){0}, host_order_options);
        bool host_first_compiled = host_first_spawn.handle && os_process_wait_sync(order_arena, host_first_spawn).result == PROCESS_RESULT_SUCCESS;
        bool host_second_compiled = host_second_spawn.handle && os_process_wait_sync(order_arena, host_second_spawn).result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST(arguments, host_first_compiled && host_second_compiled);
        if (host_first_compiled && host_second_compiled)
        {
            String8 host_image_path = buster_test_temporary_path(order_arena, S8("buster-c-constructor-order-host"), S8(""));
            String8 host_link_command[] = {
                S8("-o"), host_image_path, host_first_object, host_second_object,
            };
            CompilerDriverResult host_link = compiler_driver_execute_invocation(
                order_arena, compiler_driver_parse_arguments(order_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link_command)));
            BUSTER_TEST(arguments, host_link.error == COMPILER_DRIVER_ERROR_NONE);
            if (host_link.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 host_image_arguments[] = {host_image_path};
                ProcessSpawnResult host_image_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_image_arguments), (SliceString8){0},
                                                                       (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, host_image_spawn.handle != 0);
                if (host_image_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(order_arena, host_image_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
#endif
        scratch_end(order_temporary);
    }
    // Returning from main is a call to exit (C 5.1.2.2.3), so the linked
    // image's entry point must go through libc rather than the raw exit
    // syscall: the syscall skips stdio flushing and every atexit handler, and
    // a program that only printed and returned produced nothing at all.  This
    // one has to be spawned with stdout captured -- a pipe, so the stream is
    // fully buffered and the flush is the only thing that can produce the
    // bytes -- which is why it is not in the exit-status table above.
    String8 main_return_flush_expected = S8("buffered stdout survives returning from main\nand so does a second buffered write\n");
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
    {
        TemporalArena flush_temporary = scratch_begin(&arguments->arena, 1);
        String8 flush_path = buster_test_temporary_path(flush_temporary.arena, S8("buster-c-main-return-flush"), S8(""));
        String8 flush_command_line[] = {
            c_lz4_regression_allocators[allocator_index], S8("-o"), flush_path, S8("tests/basic_c_main_return_flush.c"),
        };
        CompilerDriverResult flush = compiler_driver_execute_invocation(
            flush_temporary.arena, compiler_driver_parse_arguments(flush_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(flush_command_line)));
        BUSTER_TEST(arguments, flush.error == COMPILER_DRIVER_ERROR_NONE);
        if (flush.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 flush_arguments[] = {flush_path};
            ProcessSpawnResult flush_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(flush_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_OUTPUT, .use_process_environment = true});
            BUSTER_TEST(arguments, flush_spawn.handle != 0);
            if (flush_spawn.handle)
            {
                ProcessWaitResult flush_wait = os_process_wait_sync(flush_temporary.arena, flush_spawn);
                String8 flush_output = (String8){
                    .pointer = (char8*)flush_wait.streams[STANDARD_STREAM_OUTPUT].pointer,
                    .length = flush_wait.streams[STANDARD_STREAM_OUTPUT].length,
                };
                BUSTER_TEST(arguments, flush_wait.result == PROCESS_RESULT_SUCCESS);
                BUSTER_TEST(arguments, string_equal(flush_output, main_return_flush_expected));
            }
        }
        scratch_end(flush_temporary);
    }
    // 80-bit x87 `long double` arithmetic: the conversions, the comparisons,
    // the four operators, and the variadic `%.2Lf` call LZ4IO_toHuman needs.
    // Every check inside the fixture is one a 53-bit significand would fail,
    // so a lowering that quietly computed in double does not pass it.  Run it
    // under every allocator even though only the canonical emitter has the
    // x87 vocabulary: what the other three are being checked for is that a
    // function carrying an f80 falls back to it rather than being selected.
    // The fixture compiles to an empty program wherever `long double` is not
    // the x87 format, so it stays registered on every host.
    String8 c_long_double_allocators[] = {
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=none"),
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=quality"),
    };
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators); allocator_index += 1)
    {
        TemporalArena long_double_temporary = scratch_begin(&arguments->arena, 1);
        String8 long_double_path = buster_test_temporary_path(long_double_temporary.arena, S8("buster-c-long-double-arithmetic"), S8(""));
        String8 long_double_command_line[] = {
            c_long_double_allocators[allocator_index], S8("-o"), long_double_path, S8("tests/basic_c_long_double_arithmetic.c"),
        };
        CompilerDriverResult long_double = compiler_driver_execute_invocation(
            long_double_temporary.arena,
            compiler_driver_parse_arguments(long_double_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(long_double_command_line)));
        BUSTER_TEST(arguments, long_double.error == COMPILER_DRIVER_ERROR_NONE);
        if (long_double.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 long_double_arguments[] = {long_double_path};
            ProcessSpawnResult long_double_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(long_double_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, long_double_spawn.handle != 0);
            if (long_double_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(long_double_temporary.arena, long_double_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(long_double_temporary);
    }
    // Static 80-bit x87 initialization: a folded constant expression and an
    // aggregate of them, which is the pair musl's src/math needs and the
    // shapes a bare-literal path refused.  The fixture compares each object
    // against the bytes Clang emits for the same declaration, so a fold that
    // is one ulp off fails it while a printed decimal would not.  It runs
    // under every allocator for the same reason the arithmetic fixture above
    // does, and compiles to an empty program wherever `long double` is not
    // the x87 format.
    String8 c_long_double_static_allocators[] = {
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=none"),
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=quality"),
    };
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_static_allocators); allocator_index += 1)
    {
        TemporalArena static_initializer_temporary = scratch_begin(&arguments->arena, 1);
        String8 static_initializer_path =
            buster_test_temporary_path(static_initializer_temporary.arena, S8("buster-c-long-double-static-initializer"), S8(""));
        String8 static_initializer_command_line[] = {
            c_long_double_static_allocators[allocator_index], S8("-o"), static_initializer_path,
            S8("tests/basic_c_long_double_static_initializer.c"),
        };
        CompilerDriverResult static_initializer = compiler_driver_execute_invocation(
            static_initializer_temporary.arena,
            compiler_driver_parse_arguments(static_initializer_temporary.arena,
                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(static_initializer_command_line)));
        BUSTER_TEST(arguments, static_initializer.error == COMPILER_DRIVER_ERROR_NONE);
        if (static_initializer.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 static_initializer_arguments[] = {static_initializer_path};
            ProcessSpawnResult static_initializer_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(static_initializer_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, static_initializer_spawn.handle != 0);
            if (static_initializer_spawn.handle)
            {
                BUSTER_TEST(arguments,
                            os_process_wait_sync(static_initializer_temporary.arena, static_initializer_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(static_initializer_temporary);
    }
    // The same static x87 territory where the value stops being finite: an
    // overflowing literal, a zero-over-zero NaN, the infinity algebra and the
    // signed zeros.  libc-test's `long double` tables are built out of musl's
    // `INFINITY` and `NAN` macros, which spell themselves `1e5000f` and
    // `(0.0f/0.0f)` for a compiler without the GNU builtins, so this is what
    // decides whether those tables compile at all.  It compares object bytes
    // rather than values because a NaN is not equal to itself and the two
    // signed zeros are equal to each other.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_static_allocators); allocator_index += 1)
    {
        TemporalArena static_special_temporary = scratch_begin(&arguments->arena, 1);
        String8 static_special_path =
            buster_test_temporary_path(static_special_temporary.arena, S8("buster-c-long-double-static-special"), S8(""));
        String8 static_special_command_line[] = {
            c_long_double_static_allocators[allocator_index], S8("-o"), static_special_path,
            S8("tests/basic_c_long_double_static_special.c"),
        };
        CompilerDriverResult static_special = compiler_driver_execute_invocation(
            static_special_temporary.arena,
            compiler_driver_parse_arguments(static_special_temporary.arena,
                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(static_special_command_line)));
        BUSTER_TEST(arguments, static_special.error == COMPILER_DRIVER_ERROR_NONE);
        if (static_special.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 static_special_arguments[] = {static_special_path};
            ProcessSpawnResult static_special_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(static_special_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, static_special_spawn.handle != 0);
            if (static_special_spawn.handle)
            {
                BUSTER_TEST(arguments,
                            os_process_wait_sync(static_special_temporary.arena, static_special_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(static_special_temporary);
    }
    // An aggregate carrying an 80-bit x87 payload.  musl's `union ldshape`
    // -- a `long double` overlaid with `struct { uint64_t m; uint16_t se; }`
    // -- is what essentially every `long double` routine in musl's src/math
    // reads a value's sign, exponent and mantissa through, and System V's
    // merger algorithm gives INTEGER precedence over x87, so it rides two
    // general-purpose registers rather than going to memory.  The fixture
    // also covers the shapes the merger cannot reconcile (memory whole), a
    // `long double` array as a local and as a global, and the address of an
    // object of incomplete type, which is how musl's src/include/stdio.h
    // reaches `stderr`.  Every allocator runs it for the same reason the
    // arithmetic fixture above does: what the other three are checked for is
    // that a function carrying an f80 falls back to the canonical emitter.
    // The fixture compiles to an empty program wherever `long double` is not
    // the x87 format, so it stays registered on every host.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators); allocator_index += 1)
    {
        TemporalArena aggregate_temporary = scratch_begin(&arguments->arena, 1);
        String8 aggregate_path = buster_test_temporary_path(aggregate_temporary.arena, S8("buster-c-long-double-aggregate"), S8(""));
        String8 aggregate_command_line[] = {
            c_long_double_allocators[allocator_index], S8("-o"), aggregate_path, S8("tests/basic_c_long_double_aggregate.c"),
        };
        CompilerDriverResult aggregate = compiler_driver_execute_invocation(
            aggregate_temporary.arena,
            compiler_driver_parse_arguments(aggregate_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(aggregate_command_line)));
        BUSTER_TEST(arguments, aggregate.error == COMPILER_DRIVER_ERROR_NONE);
        if (aggregate.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 aggregate_arguments[] = {aggregate_path};
            ProcessSpawnResult aggregate_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(aggregate_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, aggregate_spawn.handle != 0);
            if (aggregate_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(aggregate_temporary.arena, aggregate_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(aggregate_temporary);
    }
    // Reading an 80-bit x87 `long double` back out of a `va_list`.  System V
    // sends its X87/X87_UP pair to memory, so the value is never in the
    // register save area: the read realigns the overflow cursor to sixteen,
    // takes the slot, and advances past all of it.  musl's `pop_arg` in
    // src/stdio/vfprintf.c is the shape this covers, and the fixture also
    // pulls one through a `va_list *` and past a `va_copy`.  Each value is
    // compared as the `union ldshape` fields musl reads it through, against
    // constants a 53-bit significand cannot produce, so a wrong answer fails
    // the exit status rather than looking plausible.  Every allocator runs it
    // for the same reason the fixtures above do: what the other three are
    // checked for is that a function carrying an f80 falls back to the
    // canonical emitter.  The fixture compiles to an empty program wherever
    // `long double` is not the x87 format, so it stays registered on every
    // host.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators); allocator_index += 1)
    {
        TemporalArena va_arg_temporary = scratch_begin(&arguments->arena, 1);
        String8 va_arg_path = buster_test_temporary_path(va_arg_temporary.arena, S8("buster-c-va-arg-long-double"), S8(""));
        String8 va_arg_command_line[] = {
            c_long_double_allocators[allocator_index], S8("-o"), va_arg_path, S8("tests/basic_c_va_arg_long_double.c"),
        };
        CompilerDriverResult va_arg = compiler_driver_execute_invocation(
            va_arg_temporary.arena,
            compiler_driver_parse_arguments(va_arg_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(va_arg_command_line)));
        BUSTER_TEST(arguments, va_arg.error == COMPILER_DRIVER_ERROR_NONE);
        if (va_arg.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 va_arg_arguments[] = {va_arg_path};
            ProcessSpawnResult va_arg_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(va_arg_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, va_arg_spawn.handle != 0);
            if (va_arg_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(va_arg_temporary.arena, va_arg_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(va_arg_temporary);
    }
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    // The single translation unit above cannot see an ABI disagreement: a
    // caller and a callee this compiler produced agree with each other
    // whatever they agree on.  Pair the halves with the host compiler in both
    // directions, which is the only thing that pins the classification to the
    // platform's.  Verified by removing the INTEGER-over-x87 merge precedence:
    // the one-file fixture and the Buster/Buster link still pass while both
    // mixed links fail.
    {
        TemporalArena pair_temporary = scratch_begin(&arguments->arena, 1);
        Arena* pair_arena = pair_temporary.arena;
        String8 host_callee_path = buster_test_temporary_path(pair_arena, S8("buster-c-ld-aggregate-host-callee"), S8(".o"));
        String8 host_caller_path = buster_test_temporary_path(pair_arena, S8("buster-c-ld-aggregate-host-caller"), S8(".o"));
        // -fno-pic for the same reason the clang object fixture above uses it:
        // this linker has no GOT model, and -g0 keeps clang's newer debug
        // sections outside the object reader's narrow model.
        String8 host_callee_command[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_callee_path,
            S8("tests/basic_c_long_double_aggregate_callee.c"),
        };
        String8 host_caller_command[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_caller_path,
            S8("tests/basic_c_long_double_aggregate_caller.c"),
        };
        ProcessSpawnOptions host_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
        };
        ProcessSpawnResult host_callee_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_callee_command), (SliceString8){0}, (SliceString8){0}, host_options);
        ProcessSpawnResult host_caller_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_caller_command), (SliceString8){0}, (SliceString8){0}, host_options);
        bool host_callee_compiled = host_callee_spawn.handle && os_process_wait_sync(pair_arena, host_callee_spawn).result == PROCESS_RESULT_SUCCESS;
        bool host_caller_compiled = host_caller_spawn.handle && os_process_wait_sync(pair_arena, host_caller_spawn).result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST(arguments, host_callee_compiled && host_caller_compiled);
        for (u64 allocator_index = 0; host_callee_compiled && host_caller_compiled && allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators);
             allocator_index += 1)
        {
            String8 buster_callee_path = buster_test_temporary_path(pair_arena, S8("buster-c-ld-aggregate-callee"), S8(".o"));
            String8 buster_caller_path = buster_test_temporary_path(pair_arena, S8("buster-c-ld-aggregate-caller"), S8(".o"));
            String8 buster_callee_command[] = {
                c_long_double_allocators[allocator_index], S8("-c"), S8("-o"), buster_callee_path,
                S8("tests/basic_c_long_double_aggregate_callee.c"),
            };
            String8 buster_caller_command[] = {
                c_long_double_allocators[allocator_index], S8("-c"), S8("-o"), buster_caller_path,
                S8("tests/basic_c_long_double_aggregate_caller.c"),
            };
            CompilerDriverResult buster_callee = compiler_driver_execute_invocation(
                pair_arena, compiler_driver_parse_arguments(pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_callee_command)));
            CompilerDriverResult buster_caller = compiler_driver_execute_invocation(
                pair_arena, compiler_driver_parse_arguments(pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_caller_command)));
            BUSTER_TEST(arguments, buster_callee.error == COMPILER_DRIVER_ERROR_NONE && buster_caller.error == COMPILER_DRIVER_ERROR_NONE);
            if (buster_callee.error != COMPILER_DRIVER_ERROR_NONE || buster_caller.error != COMPILER_DRIVER_ERROR_NONE)
            {
                continue;
            }
            String8 mixed_object_pairs[2][2] = {
                {buster_caller_path, host_callee_path},
                {host_caller_path, buster_callee_path},
            };
            for (u64 direction = 0; direction < BUSTER_ARRAY_LENGTH(mixed_object_pairs); direction += 1)
            {
                String8 mixed_path = buster_test_temporary_path(pair_arena, S8("buster-c-ld-aggregate-pair"), S8(""));
                String8 mixed_link_command[] = {
                    S8("-o"), mixed_path, mixed_object_pairs[direction][0], mixed_object_pairs[direction][1],
                };
                CompilerDriverResult mixed_link = compiler_driver_execute_invocation(
                    pair_arena, compiler_driver_parse_arguments(pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(mixed_link_command)));
                BUSTER_TEST(arguments, mixed_link.error == COMPILER_DRIVER_ERROR_NONE);
                if (mixed_link.error != COMPILER_DRIVER_ERROR_NONE)
                {
                    continue;
                }
                String8 mixed_arguments[] = {mixed_path};
                ProcessSpawnResult mixed_spawn =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(mixed_arguments), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, mixed_spawn.handle != 0);
                if (mixed_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(pair_arena, mixed_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
        scratch_end(pair_temporary);
    }
#endif
    // C99 `_Complex`: the layout, the System V and AAPCS64 argument and result
    // shapes, the operators, `__real__`/`__imag__` as values and as places,
    // the conversions, and the anonymous-union compound literal musl's
    // <complex.h> is written with. Every expected value in the fixture came
    // from a Clang build of the same file, so the fixture is that comparison
    // frozen into a program that needs no second compiler at test time. It
    // runs under every allocator because the lowering emits a branch -- the
    // two arms of Smith's algorithm for division -- and because the complex
    // halves live in frame slots that each allocator places differently.
    String8 c_complex_allocators[] = {
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=none"),
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=quality"),
    };
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_complex_allocators); allocator_index += 1)
    {
        TemporalArena complex_temporary = scratch_begin(&arguments->arena, 1);
        String8 complex_path = buster_test_temporary_path(complex_temporary.arena, S8("buster-c-complex-arithmetic"),
                                                          string_format(complex_temporary.arena, S8("-{u32}"), (u32)allocator_index));
        String8 complex_command_line[] = {
            c_complex_allocators[allocator_index], S8("-o"), complex_path, S8("tests/basic_c_complex_arithmetic.c"),
        };
        CompilerDriverResult complex_result = compiler_driver_execute_invocation(
            complex_temporary.arena,
            compiler_driver_parse_arguments(complex_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(complex_command_line)));
        BUSTER_TEST(arguments, complex_result.error == COMPILER_DRIVER_ERROR_NONE);
        if (complex_result.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 complex_arguments[] = {complex_path};
            ProcessSpawnResult complex_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(complex_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, complex_spawn.handle != 0);
            if (complex_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(complex_temporary.arena, complex_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(complex_temporary);
    }
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    // The COMPLEX_X87 result: System V x86-64 hands a `long double _Complex`
    // back on the x87 stack, ST(0) real over ST(1) imaginary, where the
    // identically laid out `struct { long double a, b; }` is returned in
    // memory through a hidden pointer. That is the one place the two-field
    // aggregate model of a complex value is not the ABI, so a single
    // translation unit cannot see a disagreement about it -- a caller and a
    // callee this compiler produced agree with each other whatever they agree
    // on. Pair the halves with the host compiler in both directions, which is
    // what pins the pair to the platform's. Verified by returning the two
    // halves in the other order: the one-file fixture in
    // basic_c_complex_arithmetic.c and the Buster/Buster link still pass while
    // both mixed links fail.
    {
        TemporalArena complex_pair_temporary = scratch_begin(&arguments->arena, 1);
        Arena* complex_pair_arena = complex_pair_temporary.arena;
        String8 host_complex_callee_path = buster_test_temporary_path(complex_pair_arena, S8("buster-c-complex-x87-host-callee"), S8(".o"));
        String8 host_complex_caller_path = buster_test_temporary_path(complex_pair_arena, S8("buster-c-complex-x87-host-caller"), S8(".o"));
        // -fno-pic because this linker has no GOT model, and -g0 to keep
        // clang's newer debug sections outside the object reader's model, the
        // same two reasons the x87 aggregate pair above passes them.
        String8 host_complex_callee_command[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_complex_callee_path,
            S8("tests/basic_c_complex_x87_callee.c"),
        };
        String8 host_complex_caller_command[] = {
            S8(BUSTER_HOST_C_COMPILER), S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_complex_caller_path,
            S8("tests/basic_c_complex_x87_caller.c"),
        };
        ProcessSpawnOptions host_complex_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
        };
        ProcessSpawnResult host_complex_callee_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_complex_callee_command),
                                                                       (SliceString8){0}, (SliceString8){0}, host_complex_options);
        ProcessSpawnResult host_complex_caller_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_complex_caller_command),
                                                                       (SliceString8){0}, (SliceString8){0}, host_complex_options);
        bool host_complex_callee_compiled =
            host_complex_callee_spawn.handle && os_process_wait_sync(complex_pair_arena, host_complex_callee_spawn).result == PROCESS_RESULT_SUCCESS;
        bool host_complex_caller_compiled =
            host_complex_caller_spawn.handle && os_process_wait_sync(complex_pair_arena, host_complex_caller_spawn).result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST(arguments, host_complex_callee_compiled && host_complex_caller_compiled);
        for (u64 allocator_index = 0;
             host_complex_callee_compiled && host_complex_caller_compiled && allocator_index < BUSTER_ARRAY_LENGTH(c_complex_allocators);
             allocator_index += 1)
        {
            String8 buster_complex_callee_path = buster_test_temporary_path(complex_pair_arena, S8("buster-c-complex-x87-callee"), S8(".o"));
            String8 buster_complex_caller_path = buster_test_temporary_path(complex_pair_arena, S8("buster-c-complex-x87-caller"), S8(".o"));
            String8 buster_complex_callee_command[] = {
                c_complex_allocators[allocator_index], S8("-c"), S8("-o"), buster_complex_callee_path,
                S8("tests/basic_c_complex_x87_callee.c"),
            };
            String8 buster_complex_caller_command[] = {
                c_complex_allocators[allocator_index], S8("-c"), S8("-o"), buster_complex_caller_path,
                S8("tests/basic_c_complex_x87_caller.c"),
            };
            CompilerDriverResult buster_complex_callee = compiler_driver_execute_invocation(
                complex_pair_arena,
                compiler_driver_parse_arguments(complex_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_complex_callee_command)));
            CompilerDriverResult buster_complex_caller = compiler_driver_execute_invocation(
                complex_pair_arena,
                compiler_driver_parse_arguments(complex_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_complex_caller_command)));
            BUSTER_TEST(arguments,
                        buster_complex_callee.error == COMPILER_DRIVER_ERROR_NONE && buster_complex_caller.error == COMPILER_DRIVER_ERROR_NONE);
            if (buster_complex_callee.error != COMPILER_DRIVER_ERROR_NONE || buster_complex_caller.error != COMPILER_DRIVER_ERROR_NONE)
            {
                continue;
            }
            String8 complex_object_pairs[2][2] = {
                {buster_complex_caller_path, host_complex_callee_path},
                {host_complex_caller_path, buster_complex_callee_path},
            };
            for (u64 direction = 0; direction < BUSTER_ARRAY_LENGTH(complex_object_pairs); direction += 1)
            {
                String8 complex_mixed_path = buster_test_temporary_path(complex_pair_arena, S8("buster-c-complex-x87-pair"), S8(""));
                String8 complex_mixed_link_command[] = {
                    S8("-o"), complex_mixed_path, complex_object_pairs[direction][0], complex_object_pairs[direction][1],
                };
                CompilerDriverResult complex_mixed_link = compiler_driver_execute_invocation(
                    complex_pair_arena,
                    compiler_driver_parse_arguments(complex_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(complex_mixed_link_command)));
                BUSTER_TEST(arguments, complex_mixed_link.error == COMPILER_DRIVER_ERROR_NONE);
                if (complex_mixed_link.error != COMPILER_DRIVER_ERROR_NONE)
                {
                    continue;
                }
                String8 complex_mixed_arguments[] = {complex_mixed_path};
                ProcessSpawnResult complex_mixed_spawn =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(complex_mixed_arguments), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, complex_mixed_spawn.handle != 0);
                if (complex_mixed_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(complex_pair_arena, complex_mixed_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
        scratch_end(complex_pair_temporary);
    }
#endif
    // `_Atomic` over a struct or union is one integer access of the type's
    // promoted width (#762), so the object's bytes -- the value's and the
    // padding the promotion added -- are lowering's answer rather than the
    // layout's, and every allocator has to give the same one.  The fixture
    // bakes in the bytes Clang writes, so a single translation unit is enough
    // to pin the representation and not merely self-consistency.  `+cx16` is
    // forced on x86-64 because the sixteen-byte half of the fixture is the
    // CMPXCHG16B pair: leaving it to CPU detection would make the coverage
    // depend on which machine ran the suite.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators); allocator_index += 1)
    {
        TemporalArena atomic_aggregate_temporary = scratch_begin(&arguments->arena, 1);
        String8 atomic_aggregate_path = buster_test_temporary_path(atomic_aggregate_temporary.arena, S8("buster-c-atomic-aggregate"), S8(""));
        String8 atomic_aggregate_command_line[] = {
            c_long_double_allocators[allocator_index],
#if BUSTER_CPU_ARCH_X86_64
            S8("-mattr=+cx16"),
#endif
            S8("-o"),
            atomic_aggregate_path,
            S8("tests/basic_c_atomic_aggregate.c"),
        };
        CompilerDriverResult atomic_aggregate = compiler_driver_execute_invocation(
            atomic_aggregate_temporary.arena,
            compiler_driver_parse_arguments(atomic_aggregate_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_aggregate_command_line)));
        BUSTER_TEST(arguments, atomic_aggregate.error == COMPILER_DRIVER_ERROR_NONE);
        if (atomic_aggregate.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 atomic_aggregate_arguments[] = {atomic_aggregate_path};
            ProcessSpawnResult atomic_aggregate_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_aggregate_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, atomic_aggregate_spawn.handle != 0);
            if (atomic_aggregate_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(atomic_aggregate_temporary.arena, atomic_aggregate_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(atomic_aggregate_temporary);
    }
    // How a record holding an atomic member -- and an atomic record -- is
    // *passed*, which is a question of its own and one where the two
    // references disagree (#763).  A qualifier decides nothing about the class
    // here: `_Atomic T` and `volatile T` ride exactly the registers T rides,
    // which is GCC's answer everywhere and Clang's answer on every convention
    // but System V x86-64, where it sends any such record to memory, and on
    // AArch64, where it declines to call an aggregate with an atomic floating
    // member homogeneous.  ir_abi_unqualified_type holds the reasoning and the
    // measurements.  Build both halves with this compiler through every
    // allocator first: that catches a caller and a callee that disagree with
    // each other, which the mixed links below cannot separate from a
    // disagreement with the reference.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators); allocator_index += 1)
    {
        TemporalArena atomic_abi_temporary = scratch_begin(&arguments->arena, 1);
        String8 atomic_abi_path = buster_test_temporary_path(atomic_abi_temporary.arena, S8("buster-c-atomic-abi"), S8(""));
        String8 atomic_abi_command_line[] = {
            c_long_double_allocators[allocator_index], S8("-o"), atomic_abi_path, S8("tests/basic_c_atomic_abi_callee.c"),
            S8("tests/basic_c_atomic_abi_caller.c"),
        };
        CompilerDriverResult atomic_abi = compiler_driver_execute_invocation(
            atomic_abi_temporary.arena,
            compiler_driver_parse_arguments(atomic_abi_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_abi_command_line)));
        BUSTER_TEST(arguments, atomic_abi.error == COMPILER_DRIVER_ERROR_NONE);
        if (atomic_abi.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 atomic_abi_arguments[] = {atomic_abi_path};
            ProcessSpawnResult atomic_abi_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_abi_arguments), (SliceString8){0},
                                                                    (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, atomic_abi_spawn.handle != 0);
            if (atomic_abi_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(atomic_abi_temporary.arena, atomic_abi_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(atomic_abi_temporary);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    // The System V half of the decision, against the reference that shares it.
    // The host compiler cannot stand in here the way it does for the packed
    // pair below: on this convention Clang is the half that disagrees, so a
    // Clang-built object fails this fixture at its first record by design.
    // Look for a real GCC instead, and skip the lane when there is none --
    // `gcc` on macOS is a Clang shim, which its own `--version` says.
    String8 atomic_abi_gcc = executable_resolve_in_path(arguments->arena, S8("gcc"));
    bool atomic_abi_gcc_available = false;
    if (atomic_abi_gcc.length)
    {
        String8 atomic_abi_gcc_probe_arguments[] = {atomic_abi_gcc, S8("--version")};
        ProcessSpawnResult atomic_abi_gcc_probe =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_abi_gcc_probe_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                 .use_process_environment = true,
                             });
        if (atomic_abi_gcc_probe.handle)
        {
            ProcessWaitResult atomic_abi_gcc_wait = os_process_wait_sync(arguments->arena, atomic_abi_gcc_probe);
            String8 atomic_abi_gcc_version = (String8){
                .pointer = (char8*)atomic_abi_gcc_wait.streams[STANDARD_STREAM_OUTPUT].pointer,
                .length = atomic_abi_gcc_wait.streams[STANDARD_STREAM_OUTPUT].length,
            };
            atomic_abi_gcc_available = atomic_abi_gcc_wait.result == PROCESS_RESULT_SUCCESS &&
                                       string_first_sequence(atomic_abi_gcc_version, S8("clang")) == BUSTER_STRING_NO_MATCH;
        }
    }
    if (atomic_abi_gcc_available)
    {
        TemporalArena atomic_abi_pair_temporary = scratch_begin(&arguments->arena, 1);
        Arena* atomic_abi_pair_arena = atomic_abi_pair_temporary.arena;
        String8 gcc_atomic_callee_path = buster_test_temporary_path(atomic_abi_pair_arena, S8("buster-c-atomic-abi-gcc-callee"), S8(".o"));
        String8 gcc_atomic_caller_path = buster_test_temporary_path(atomic_abi_pair_arena, S8("buster-c-atomic-abi-gcc-caller"), S8(".o"));
        // -fno-pic and -g0 for the same reasons the packed pair below uses
        // them: this linker has no GOT model, and the reference's newer debug
        // sections sit outside the object reader's model.
        String8 gcc_atomic_callee_command[] = {
            atomic_abi_gcc, S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), gcc_atomic_callee_path, S8("tests/basic_c_atomic_abi_callee.c"),
        };
        String8 gcc_atomic_caller_command[] = {
            atomic_abi_gcc, S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), gcc_atomic_caller_path, S8("tests/basic_c_atomic_abi_caller.c"),
        };
        ProcessSpawnOptions gcc_atomic_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
        };
        ProcessSpawnResult gcc_atomic_callee_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(gcc_atomic_callee_command), (SliceString8){0}, (SliceString8){0}, gcc_atomic_options);
        ProcessSpawnResult gcc_atomic_caller_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(gcc_atomic_caller_command), (SliceString8){0}, (SliceString8){0}, gcc_atomic_options);
        bool gcc_atomic_callee_compiled =
            gcc_atomic_callee_spawn.handle && os_process_wait_sync(atomic_abi_pair_arena, gcc_atomic_callee_spawn).result == PROCESS_RESULT_SUCCESS;
        bool gcc_atomic_caller_compiled =
            gcc_atomic_caller_spawn.handle && os_process_wait_sync(atomic_abi_pair_arena, gcc_atomic_caller_spawn).result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST(arguments, gcc_atomic_callee_compiled && gcc_atomic_caller_compiled);
        for (u64 allocator_index = 0;
             gcc_atomic_callee_compiled && gcc_atomic_caller_compiled && allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators);
             allocator_index += 1)
        {
            String8 buster_atomic_callee_path = buster_test_temporary_path(atomic_abi_pair_arena, S8("buster-c-atomic-abi-callee"), S8(".o"));
            String8 buster_atomic_caller_path = buster_test_temporary_path(atomic_abi_pair_arena, S8("buster-c-atomic-abi-caller"), S8(".o"));
            String8 buster_atomic_callee_command[] = {
                c_long_double_allocators[allocator_index], S8("-c"), S8("-o"), buster_atomic_callee_path, S8("tests/basic_c_atomic_abi_callee.c"),
            };
            String8 buster_atomic_caller_command[] = {
                c_long_double_allocators[allocator_index], S8("-c"), S8("-o"), buster_atomic_caller_path, S8("tests/basic_c_atomic_abi_caller.c"),
            };
            CompilerDriverResult buster_atomic_callee = compiler_driver_execute_invocation(
                atomic_abi_pair_arena,
                compiler_driver_parse_arguments(atomic_abi_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_atomic_callee_command)));
            CompilerDriverResult buster_atomic_caller = compiler_driver_execute_invocation(
                atomic_abi_pair_arena,
                compiler_driver_parse_arguments(atomic_abi_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_atomic_caller_command)));
            BUSTER_TEST(arguments, buster_atomic_callee.error == COMPILER_DRIVER_ERROR_NONE && buster_atomic_caller.error == COMPILER_DRIVER_ERROR_NONE);
            if (buster_atomic_callee.error != COMPILER_DRIVER_ERROR_NONE || buster_atomic_caller.error != COMPILER_DRIVER_ERROR_NONE)
            {
                continue;
            }
            String8 atomic_object_pairs[2][2] = {
                {buster_atomic_caller_path, gcc_atomic_callee_path},
                {gcc_atomic_caller_path, buster_atomic_callee_path},
            };
            for (u64 direction = 0; direction < BUSTER_ARRAY_LENGTH(atomic_object_pairs); direction += 1)
            {
                String8 atomic_mixed_path = buster_test_temporary_path(atomic_abi_pair_arena, S8("buster-c-atomic-abi-pair"), S8(""));
                String8 atomic_mixed_link_command[] = {
                    S8("-o"), atomic_mixed_path, atomic_object_pairs[direction][0], atomic_object_pairs[direction][1],
                };
                CompilerDriverResult atomic_mixed_link = compiler_driver_execute_invocation(
                    atomic_abi_pair_arena,
                    compiler_driver_parse_arguments(atomic_abi_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_mixed_link_command)));
                BUSTER_TEST(arguments, atomic_mixed_link.error == COMPILER_DRIVER_ERROR_NONE);
                if (atomic_mixed_link.error != COMPILER_DRIVER_ERROR_NONE)
                {
                    continue;
                }
                String8 atomic_mixed_arguments[] = {atomic_mixed_path};
                ProcessSpawnResult atomic_mixed_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_mixed_arguments), (SliceString8){0},
                                                                          (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, atomic_mixed_spawn.handle != 0);
                if (atomic_mixed_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(atomic_abi_pair_arena, atomic_mixed_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
        scratch_end(atomic_abi_pair_temporary);
    }
#endif
#if !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    // The AArch64 half, against Clang, which agrees with us there that such a
    // record rides registers -- its memory rule is System V's alone.  What it
    // does not agree with is the homogeneous float aggregate an atomic member
    // sits in, so that one shape leaves the pair through
    // ATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA and its `volatile` twin, which
    // Clang does keep homogeneous, stays and pins the same mechanism.  The
    // tiny AArch64 `_start` object the __int128 pair above uses keeps this a
    // static, libc-free image qemu can execute.
    if (aarch64_i128_clang_available && aarch64_i128_qemu_available)
    {
        TemporalArena atomic_a64_temporary = scratch_begin(&arguments->arena, 1);
        Arena* atomic_a64_arena = atomic_a64_temporary.arena;
        String8 atomic_a64_reference[] = {S8("-DATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA=1")};
        String8 clang_atomic_callee_path = buster_test_temporary_path(atomic_a64_arena, S8("buster-c-atomic-abi-a64-clang-callee"), S8(".o"));
        String8 clang_atomic_caller_path = buster_test_temporary_path(atomic_a64_arena, S8("buster-c-atomic-abi-a64-clang-caller"), S8(".o"));
        String8 clang_atomic_start_path = buster_test_temporary_path(atomic_a64_arena, S8("buster-c-atomic-abi-a64-clang-start"), S8(".o"));
        String8 clang_atomic_callee_command[] = {
            S8("clang"), S8("-target"), S8("aarch64-unknown-linux-gnu"), atomic_a64_reference[0], S8("-ffreestanding"), S8("-fno-builtin"),
            S8("-fno-stack-protector"), S8("-g0"), S8("-c"), S8("-o"), clang_atomic_callee_path, S8("tests/basic_c_atomic_abi_callee.c"),
        };
        String8 clang_atomic_caller_command[] = {
            S8("clang"), S8("-target"), S8("aarch64-unknown-linux-gnu"), atomic_a64_reference[0], S8("-ffreestanding"), S8("-fno-builtin"),
            S8("-fno-stack-protector"), S8("-g0"), S8("-c"), S8("-o"), clang_atomic_caller_path, S8("tests/basic_c_atomic_abi_caller.c"),
        };
        String8 clang_atomic_start_command[] = {
            S8("clang"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-g0"), S8("-c"), S8("-o"), clang_atomic_start_path,
            S8("tests/basic_c_aarch64_i128_start.S"),
        };
        ProcessSpawnOptions clang_atomic_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
        };
        ProcessSpawnResult clang_atomic_callee_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_atomic_callee_command), (SliceString8){0}, (SliceString8){0}, clang_atomic_options);
        ProcessSpawnResult clang_atomic_caller_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_atomic_caller_command), (SliceString8){0}, (SliceString8){0}, clang_atomic_options);
        ProcessSpawnResult clang_atomic_start_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(clang_atomic_start_command), (SliceString8){0}, (SliceString8){0}, clang_atomic_options);
        bool clang_atomic_callee_compiled =
            clang_atomic_callee_spawn.handle && os_process_wait_sync(atomic_a64_arena, clang_atomic_callee_spawn).result == PROCESS_RESULT_SUCCESS;
        bool clang_atomic_caller_compiled =
            clang_atomic_caller_spawn.handle && os_process_wait_sync(atomic_a64_arena, clang_atomic_caller_spawn).result == PROCESS_RESULT_SUCCESS;
        bool clang_atomic_start_compiled =
            clang_atomic_start_spawn.handle && os_process_wait_sync(atomic_a64_arena, clang_atomic_start_spawn).result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST(arguments, clang_atomic_callee_compiled && clang_atomic_caller_compiled && clang_atomic_start_compiled);
        if (clang_atomic_callee_compiled && clang_atomic_caller_compiled && clang_atomic_start_compiled)
        {
            String8 buster_atomic_a64_callee_path = buster_test_temporary_path(atomic_a64_arena, S8("buster-c-atomic-abi-a64-callee"), S8(".o"));
            String8 buster_atomic_a64_caller_path = buster_test_temporary_path(atomic_a64_arena, S8("buster-c-atomic-abi-a64-caller"), S8(".o"));
            String8 buster_atomic_a64_callee_command[] = {
                S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), atomic_a64_reference[0], S8("-fregister-allocator=none"), S8("-o"),
                buster_atomic_a64_callee_path, S8("tests/basic_c_atomic_abi_callee.c"),
            };
            String8 buster_atomic_a64_caller_command[] = {
                S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), atomic_a64_reference[0], S8("-fregister-allocator=none"), S8("-o"),
                buster_atomic_a64_caller_path, S8("tests/basic_c_atomic_abi_caller.c"),
            };
            CompilerDriverResult buster_atomic_a64_callee = compiler_driver_execute_invocation(
                atomic_a64_arena,
                compiler_driver_parse_arguments(atomic_a64_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_atomic_a64_callee_command)));
            CompilerDriverResult buster_atomic_a64_caller = compiler_driver_execute_invocation(
                atomic_a64_arena,
                compiler_driver_parse_arguments(atomic_a64_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_atomic_a64_caller_command)));
            BUSTER_TEST(arguments,
                        buster_atomic_a64_callee.error == COMPILER_DRIVER_ERROR_NONE && buster_atomic_a64_caller.error == COMPILER_DRIVER_ERROR_NONE);
            if (buster_atomic_a64_callee.error == COMPILER_DRIVER_ERROR_NONE && buster_atomic_a64_caller.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 atomic_a64_pairs[2][2] = {
                    {buster_atomic_a64_caller_path, clang_atomic_callee_path},
                    {clang_atomic_caller_path, buster_atomic_a64_callee_path},
                };
                for (u64 direction = 0; direction < BUSTER_ARRAY_LENGTH(atomic_a64_pairs); direction += 1)
                {
                    String8 atomic_a64_image_path = buster_test_temporary_path(atomic_a64_arena, S8("buster-c-atomic-abi-a64-pair"), S8(".elf"));
                    String8 atomic_a64_link_command[] = {
                        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), atomic_a64_image_path, atomic_a64_pairs[direction][0],
                        atomic_a64_pairs[direction][1], clang_atomic_start_path,
                    };
                    CompilerDriverResult atomic_a64_link = compiler_driver_execute_invocation(
                        atomic_a64_arena, compiler_driver_parse_arguments(atomic_a64_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_a64_link_command)));
                    BUSTER_TEST(arguments, atomic_a64_link.error == COMPILER_DRIVER_ERROR_NONE);
                    if (atomic_a64_link.error != COMPILER_DRIVER_ERROR_NONE)
                    {
                        continue;
                    }
                    String8 atomic_a64_run_arguments[] = {S8("qemu-aarch64"), atomic_a64_image_path};
                    ProcessSpawnResult atomic_a64_run = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_a64_run_arguments), (SliceString8){0},
                                                                          (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                    BUSTER_TEST(arguments, atomic_a64_run.handle != 0);
                    if (atomic_a64_run.handle)
                    {
                        BUSTER_TEST(arguments, os_process_wait_sync(atomic_a64_arena, atomic_a64_run).result == PROCESS_RESULT_SUCCESS);
                    }
                }
            }
        }
        scratch_end(atomic_a64_temporary);
    }
#endif
    // The atomic-aggregate representation fixture again, cross-compiled to
    // AArch64 through every allocator: the machine selector lowers an atomic
    // aggregate access through one ldar/stlr of the place's promoted width,
    // and this is the lane that executes those rows rather than only
    // counting them.  The fixture bakes in the bytes Clang writes, so a
    // machine path that stored residue where the promotion's padding
    // belongs fails here byte by byte.
    for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(aarch64_i128_allocators); allocator_index += 1)
    {
        TemporalArena atomic_a64_aggregate_temporary = scratch_begin(&arguments->arena, 1);
        String8 atomic_a64_aggregate_path =
            buster_test_temporary_path(atomic_a64_aggregate_temporary.arena, S8("buster-c-atomic-aggregate-a64"),
                                       string_format(atomic_a64_aggregate_temporary.arena, S8("-{S8}.elf"), aarch64_i128_allocators[allocator_index]));
        String8 atomic_a64_aggregate_command_line[] = {
            S8("-target"),
            S8("aarch64-unknown-linux-gnu"),
            string_format(atomic_a64_aggregate_temporary.arena, S8("-fregister-allocator={S8}"), aarch64_i128_allocators[allocator_index]),
            S8("-o"),
            atomic_a64_aggregate_path,
            S8("tests/basic_c_atomic_aggregate.c"),
        };
        CompilerDriverResult atomic_a64_aggregate = compiler_driver_execute_invocation(
            atomic_a64_aggregate_temporary.arena,
            compiler_driver_parse_arguments(atomic_a64_aggregate_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_a64_aggregate_command_line)));
        BUSTER_TEST(arguments, atomic_a64_aggregate.error == COMPILER_DRIVER_ERROR_NONE);
        if (atomic_a64_aggregate.error == COMPILER_DRIVER_ERROR_NONE && aarch64_i128_qemu_available)
        {
            String8 atomic_a64_aggregate_run[] = {S8("qemu-aarch64"), atomic_a64_aggregate_path};
            ProcessSpawnResult atomic_a64_aggregate_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_a64_aggregate_run), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, atomic_a64_aggregate_spawn.handle != 0);
            if (atomic_a64_aggregate_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(atomic_a64_aggregate_temporary.arena, atomic_a64_aggregate_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(atomic_a64_aggregate_temporary);
    }
    // The 128-bit integer fixture, cross-compiled to AArch64: its
    // test_atomic_wide walks the whole sixteen-byte atomic vocabulary —
    // store, load, every fetch-op, exchange, and both compare-exchange
    // strengths — which the canonical AArch64 emitter lowers as LDXP/STXP
    // exclusive-pair loops. qemu executes the loops, so a pair whose halves
    // tore, a carry that missed the high half, or a compare that read one
    // half would exit nonzero here rather than only compiling.
    for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(aarch64_i128_allocators); allocator_index += 1)
    {
        TemporalArena int128_a64_temporary = scratch_begin(&arguments->arena, 1);
        String8 int128_a64_path =
            buster_test_temporary_path(int128_a64_temporary.arena, S8("buster-c-int128-a64"),
                                       string_format(int128_a64_temporary.arena, S8("-{S8}.elf"), aarch64_i128_allocators[allocator_index]));
        String8 int128_a64_command_line[] = {
            S8("-target"),
            S8("aarch64-unknown-linux-gnu"),
            string_format(int128_a64_temporary.arena, S8("-fregister-allocator={S8}"), aarch64_i128_allocators[allocator_index]),
            S8("-o"),
            int128_a64_path,
            S8("tests/basic_c_int128.c"),
        };
        CompilerDriverResult int128_a64 = compiler_driver_execute_invocation(
            int128_a64_temporary.arena,
            compiler_driver_parse_arguments(int128_a64_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(int128_a64_command_line)));
        BUSTER_TEST(arguments, int128_a64.error == COMPILER_DRIVER_ERROR_NONE);
        if (int128_a64.error == COMPILER_DRIVER_ERROR_NONE && aarch64_i128_qemu_available)
        {
            String8 int128_a64_run[] = {S8("qemu-aarch64"), int128_a64_path};
            ProcessSpawnResult int128_a64_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(int128_a64_run), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, int128_a64_spawn.handle != 0);
            if (int128_a64_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(int128_a64_temporary.arena, int128_a64_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(int128_a64_temporary);
    }
    // `__attribute__((packed))` and `__attribute__((aligned(N)))` decide
    // object representation, so a compiler that parses and ignores them lays
    // out a different object than every other compiler on the target while
    // agreeing with itself.  The single translation unit covers the four
    // positions the two attributes reach a layout from -- before the tag,
    // after the body, on one member, on an object declarator -- the two
    // declarator positions the shared-specifier one does not cover (after a
    // parenthesized declarator, and at the head of a declarator that is not
    // the first of its list), each on a member and on an object declarator at
    // file and block scope because a separate parser reads each, which
    // declarator of a list an `aligned` attribute belongs to, the typedef
    // position -- where the request belongs to the type the name declares and
    // may lower its alignment as well as raise it -- plus the `#pragma pack`
    // ceiling they share, and runs under every allocator because packed
    // bit-fields are lowering rather than parsing work.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators); allocator_index += 1)
    {
        TemporalArena packed_temporary = scratch_begin(&arguments->arena, 1);
        String8 packed_path = buster_test_temporary_path(packed_temporary.arena, S8("buster-c-packed-layout"), S8(""));
        String8 packed_command_line[] = {
            c_long_double_allocators[allocator_index], S8("-o"), packed_path, S8("tests/basic_c_packed_layout.c"),
        };
        CompilerDriverResult packed = compiler_driver_execute_invocation(
            packed_temporary.arena, compiler_driver_parse_arguments(packed_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(packed_command_line)));
        BUSTER_TEST(arguments, packed.error == COMPILER_DRIVER_ERROR_NONE);
        if (packed.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 packed_arguments[] = {packed_path};
            ProcessSpawnResult packed_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(packed_arguments), (SliceString8){0}, (SliceString8){0},
                                                               (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, packed_spawn.handle != 0);
            if (packed_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(packed_temporary.arena, packed_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(packed_temporary);
    }
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    // The single translation unit above cannot see a layout divergence: a
    // program that ignores both attributes agrees with itself.  Pair the
    // halves with the host compiler in both directions, which is the only
    // thing that pins the layout to the platform's.  Verified by stripping
    // `packed` from the shared header on one side: the one-file fixture and
    // the Buster/Buster link still pass while the mixed link fails.  The
    // typedef shapes were verified the same way, and they pin the argument
    // classification as well as the layout: a lowered alignment leaves a field
    // unaligned for System V's purposes, so the record rides memory rather
    // than a register.  The records holding a bit-field pin the other half of
    // that question (#721) -- a bit-field's bits decide which eightbytes it
    // merges into, never its declared type -- and were verified the same way:
    // against the compiler that asked the declared type, the Buster caller
    // fails at the first record and the Clang caller faults on a hidden
    // pointer that is really an argument, while the one-file fixture and the
    // Buster/Buster link still pass.  The
    // layout rules are target-independent and the one-file fixture runs
    // everywhere; the mixed link is held to the host/target pair the x87 pair
    // above already links objects across, because -fno-pic and this linker's
    // reach are what the pairing depends on rather than the layout.
    {
        TemporalArena packed_pair_temporary = scratch_begin(&arguments->arena, 1);
        Arena* packed_pair_arena = packed_pair_temporary.arena;
        // clang 18 and older classified the lowered-alignment record as
        // INTEGER rather than MEMORY (see basic_c_packed_layout_shapes.h).
        // Probe the host compiler once so both halves of every pair carry its
        // clang major and the convention-sensitive check participates only
        // when the host agrees on the psABI reading; layout answers stay
        // active against every host.
        u64 host_clang_major = 0;
        {
            String8 host_version_command[] = {S8(BUSTER_HOST_C_COMPILER), S8("--version")};
            ProcessSpawnResult host_version_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_version_command), (SliceString8){0}, (SliceString8){0},
                                                                     (ProcessSpawnOptions){
                                                                         .capture = ((u64)1 << STANDARD_STREAM_OUTPUT),
                                                                         .use_process_environment = true,
                                                                     });
            if (host_version_spawn.handle)
            {
                ProcessWaitResult host_version_wait = os_process_wait_sync(packed_pair_arena, host_version_spawn);
                String8 host_version_output = {
                    .pointer = (char8*)host_version_wait.streams[STANDARD_STREAM_OUTPUT].pointer,
                    .length = host_version_wait.streams[STANDARD_STREAM_OUTPUT].length,
                };
                u64 marker = string_first_sequence(host_version_output, S8("clang version "));
                if (host_version_wait.result == PROCESS_RESULT_SUCCESS && marker != BUSTER_STRING_NO_MATCH)
                {
                    for (u64 index = marker + (sizeof("clang version ") - 1); index < host_version_output.length; index += 1)
                    {
                        char8 character = host_version_output.pointer[index];
                        if (character < '0' || character > '9')
                        {
                            break;
                        }
                        host_clang_major = host_clang_major * 10 + (u64)(character - '0');
                    }
                }
            }
        }
        String8 host_clang_major_define = string_format(packed_pair_arena, S8("-DPACKED_LAYOUT_HOST_CLANG_MAJOR={u64}"), host_clang_major);
        String8 host_packed_callee_path = buster_test_temporary_path(packed_pair_arena, S8("buster-c-packed-host-callee"), S8(".o"));
        String8 host_packed_caller_path = buster_test_temporary_path(packed_pair_arena, S8("buster-c-packed-host-caller"), S8(".o"));
        // -fno-pic and -g0 for the same reasons the x87 pair above uses them:
        // this linker has no GOT model, and clang's newer debug sections sit
        // outside the object reader's model.
        String8 host_packed_callee_command[] = {
            S8(BUSTER_HOST_C_COMPILER), host_clang_major_define, S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_packed_callee_path,
            S8("tests/basic_c_packed_layout_callee.c"),
        };
        String8 host_packed_caller_command[] = {
            S8(BUSTER_HOST_C_COMPILER), host_clang_major_define, S8("-fno-pic"), S8("-g0"), S8("-c"), S8("-o"), host_packed_caller_path,
            S8("tests/basic_c_packed_layout_caller.c"),
        };
        ProcessSpawnOptions host_packed_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
        };
        ProcessSpawnResult host_packed_callee_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_packed_callee_command), (SliceString8){0},
                                                                       (SliceString8){0}, host_packed_options);
        ProcessSpawnResult host_packed_caller_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(host_packed_caller_command), (SliceString8){0},
                                                                       (SliceString8){0}, host_packed_options);
        bool host_packed_callee_compiled =
            host_packed_callee_spawn.handle && os_process_wait_sync(packed_pair_arena, host_packed_callee_spawn).result == PROCESS_RESULT_SUCCESS;
        bool host_packed_caller_compiled =
            host_packed_caller_spawn.handle && os_process_wait_sync(packed_pair_arena, host_packed_caller_spawn).result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST(arguments, host_packed_callee_compiled && host_packed_caller_compiled);
        for (u64 allocator_index = 0;
             host_packed_callee_compiled && host_packed_caller_compiled && allocator_index < BUSTER_ARRAY_LENGTH(c_long_double_allocators);
             allocator_index += 1)
        {
            String8 buster_packed_callee_path = buster_test_temporary_path(packed_pair_arena, S8("buster-c-packed-callee"), S8(".o"));
            String8 buster_packed_caller_path = buster_test_temporary_path(packed_pair_arena, S8("buster-c-packed-caller"), S8(".o"));
            String8 buster_packed_callee_command[] = {
                c_long_double_allocators[allocator_index], host_clang_major_define, S8("-c"), S8("-o"), buster_packed_callee_path,
                S8("tests/basic_c_packed_layout_callee.c"),
            };
            String8 buster_packed_caller_command[] = {
                c_long_double_allocators[allocator_index], host_clang_major_define, S8("-c"), S8("-o"), buster_packed_caller_path,
                S8("tests/basic_c_packed_layout_caller.c"),
            };
            CompilerDriverResult buster_packed_callee = compiler_driver_execute_invocation(
                packed_pair_arena, compiler_driver_parse_arguments(packed_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_packed_callee_command)));
            CompilerDriverResult buster_packed_caller = compiler_driver_execute_invocation(
                packed_pair_arena, compiler_driver_parse_arguments(packed_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_packed_caller_command)));
            BUSTER_TEST(arguments, buster_packed_callee.error == COMPILER_DRIVER_ERROR_NONE && buster_packed_caller.error == COMPILER_DRIVER_ERROR_NONE);
            if (buster_packed_callee.error != COMPILER_DRIVER_ERROR_NONE || buster_packed_caller.error != COMPILER_DRIVER_ERROR_NONE)
            {
                continue;
            }
            String8 packed_object_pairs[2][2] = {
                {buster_packed_caller_path, host_packed_callee_path},
                {host_packed_caller_path, buster_packed_callee_path},
            };
            for (u64 direction = 0; direction < BUSTER_ARRAY_LENGTH(packed_object_pairs); direction += 1)
            {
                String8 packed_mixed_path = buster_test_temporary_path(packed_pair_arena, S8("buster-c-packed-pair"), S8(""));
                String8 packed_mixed_link_command[] = {
                    S8("-o"), packed_mixed_path, packed_object_pairs[direction][0], packed_object_pairs[direction][1],
                };
                CompilerDriverResult packed_mixed_link = compiler_driver_execute_invocation(
                    packed_pair_arena, compiler_driver_parse_arguments(packed_pair_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(packed_mixed_link_command)));
                BUSTER_TEST(arguments, packed_mixed_link.error == COMPILER_DRIVER_ERROR_NONE);
                if (packed_mixed_link.error != COMPILER_DRIVER_ERROR_NONE)
                {
                    continue;
                }
                String8 packed_mixed_arguments[] = {packed_mixed_path};
                ProcessSpawnResult packed_mixed_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(packed_mixed_arguments), (SliceString8){0},
                                                                         (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, packed_mixed_spawn.handle != 0);
                if (packed_mixed_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(packed_pair_arena, packed_mixed_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
        scratch_end(packed_pair_temporary);
    }
#endif
    // A loop body that allocates nothing must carry no stack checkpoint at
    // all: the restore is what read a stale frame slot into RSP, and its
    // absence is the property the runtime fixture above cannot observe on a
    // host whose stack happens to hold a benign value there.
    String8 goto_loop_assembly_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_goto_into_loop.c"),
    };
    CompilerDriverResult goto_loop_assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(goto_loop_assembly_command_line)));
    BUSTER_TEST(arguments, goto_loop_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    u64 jump_into_body = string_first_sequence(goto_loop_assembly.output, S8("\njump_into_body:\n"));
    BUSTER_TEST(arguments, jump_into_body != BUSTER_STRING_NO_MATCH);
    if (jump_into_body != BUSTER_STRING_NO_MATCH)
    {
        // The emitter writes every `.size` directive after every function, so
        // the body ends at the next function label rather than at its own size
        // directive.
        String8 body = string_slice(goto_loop_assembly.output, jump_into_body, goto_loop_assembly.output.length);
        u64 body_end = string_first_sequence(string_slice(body, 1, body.length), S8("\n\t.type "));
        body = string_slice(body, 0, body_end != BUSTER_STRING_NO_MATCH ? body_end + 1 : body.length);
        // A stack save is the only thing that reads RSP into another operand;
        // the prologue's `mov rbp, rsp` is the one legitimate occurrence.
        u32 stack_pointer_reads = 0;
        u64 scan = 0;
        bool scanning = true;
        while (scanning)
        {
            u64 read = string_first_sequence(string_slice(body, scan, body.length), S8(", rsp"));
            scanning = read != BUSTER_STRING_NO_MATCH;
            if (scanning)
            {
                stack_pointer_reads += 1;
                scan += read + 1;
            }
        }
        BUSTER_TEST(arguments, stack_pointer_reads == 1);
    }
    // QuickJS compatibility reduced eight more independent failures to these
    // fixtures: an aggregate whose attribute sits between the keyword and the
    // tag, a block-scope enum declarator whose body carries an '=', the short
    // `__attribute` spelling on a function, `_Atomic(T)` as a cast type name,
    // a static initializer naming the object it initializes, the constant
    // float builtins hosted <math.h> hides behind NAN and INFINITY, the
    // frame-address/alloca/signbit builtins, System V bit-field placement
    // with an enum-typed field among narrower ones, a shift whose count is
    // unsigned, and a narrow integer parameter whose register arrives with
    // the caller's leftover high half.  Each source stays separate so a future regression names
    // the exact contract it broke, and each runs under every register
    // allocator because five of the ten are lowering rather than parsing
    // defects.
    String8 c_quickjs_regression_paths[] = {
        S8("tests/basic_c_aggregate_attribute.c"),
        S8("tests/basic_c_local_enum_declarator.c"),
        S8("tests/basic_c_attribute_short_spelling.c"),
        S8("tests/basic_c_atomic_specifier.c"),
        S8("tests/basic_c_self_referential_initializer.c"),
        S8("tests/basic_c_constant_float_builtins.c"),
        S8("tests/basic_c_frame_alloca_signbit.c"),
        S8("tests/basic_c_bit_field_layout.c"),
        S8("tests/basic_c_shift_operand_types.c"),
        S8("tests/basic_c_narrow_argument_abi.c"),
    };
    String8 c_quickjs_regression_names[] = {
        S8("buster-c-aggregate-attribute"),
        S8("buster-c-local-enum-declarator"),
        S8("buster-c-attribute-short-spelling"),
        S8("buster-c-atomic-specifier"),
        S8("buster-c-self-referential-initializer"),
        S8("buster-c-constant-float-builtins"),
        S8("buster-c-frame-alloca-signbit"),
        S8("buster-c-bit-field-layout"),
        S8("buster-c-shift-operand-types"),
        S8("buster-c-narrow-argument-abi"),
    };
    for (u64 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_quickjs_regression_paths); fixture_index += 1)
    {
        for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
        {
            TemporalArena quickjs_temporary = scratch_begin(&arguments->arena, 1);
            String8 fixture_path = buster_test_temporary_path(quickjs_temporary.arena, c_quickjs_regression_names[fixture_index], S8(""));
            String8 fixture_command_line[] = {
                c_lz4_regression_allocators[allocator_index], S8("-o"), fixture_path, c_quickjs_regression_paths[fixture_index],
            };
            CompilerDriverResult fixture = compiler_driver_execute_invocation(
                quickjs_temporary.arena,
                compiler_driver_parse_arguments(quickjs_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
            BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
            if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 fixture_arguments[] = {fixture_path};
                ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, fixture_spawn.handle != 0);
                if (fixture_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(quickjs_temporary.arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(quickjs_temporary);
        }
    }
    // The C23 attribute syntax, which the frontend did not parse at all: an
    // attributed declaration failed to register and the error surfaced later
    // as an unrelated "undeclared identifier".  Two fixtures, because the two
    // halves fail differently.  The first covers every position the grammar
    // allows a sequence in and asserts the decorated declarations, layouts
    // and control flow are unchanged, which is the silent failure mode.  The
    // second covers [[noreturn]], the one attribute buster acts on, and is
    // checked in the assembly below as well as run.  Both run under every
    // register allocator: the statement and label positions are lowering
    // rather than parsing defects.
    String8 c_c23_attribute_paths[] = {
        S8("tests/basic_c_c23_attributes.c"),
        S8("tests/basic_c_c23_noreturn.c"),
    };
    String8 c_c23_attribute_names[] = {
        S8("buster-c-c23-attributes"),
        S8("buster-c-c23-noreturn"),
    };
    for (u64 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_c23_attribute_paths); fixture_index += 1)
    {
        for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
        {
            TemporalArena c23_temporary = scratch_begin(&arguments->arena, 1);
            String8 fixture_path = buster_test_temporary_path(c23_temporary.arena, c_c23_attribute_names[fixture_index], S8(""));
            String8 fixture_command_line[] = {
                c_lz4_regression_allocators[allocator_index], S8("-o"), fixture_path, c_c23_attribute_paths[fixture_index],
            };
            CompilerDriverResult fixture = compiler_driver_execute_invocation(
                c23_temporary.arena, compiler_driver_parse_arguments(c23_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
            BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
            if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 fixture_arguments[] = {fixture_path};
                ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, fixture_spawn.handle != 0);
                if (fixture_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(c23_temporary.arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(c23_temporary);
        }
    }
    // Running the fixture cannot tell whether [[noreturn]] was read: a caller
    // that ignored it still returns normally and still exits 0.  The proof is
    // that the call is followed by a trap rather than by a return sequence,
    // and the fixture supplies the control -- die_marked and die_plain differ
    // in nothing but the attribute.  0x0f 0x0b is ud2, which the emitter
    // writes as raw bytes rather than as a mnemonic.  Pinning an explicit
    // target keeps the assertion deterministic on any host.
    String8 c23_noreturn_assembly_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_c23_noreturn.c"),
    };
    CompilerDriverResult c23_noreturn_assembly = compiler_driver_execute_invocation(
        arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c23_noreturn_assembly_command_line)));
    BUSTER_TEST(arguments, c23_noreturn_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(c23_noreturn_assembly.output, S8("call \"die_marked\"\n\t.byte 0x0f, 0x0b")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(c23_noreturn_assembly.output, S8("call \"die_scoped\"\n\t.byte 0x0f, 0x0b")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(c23_noreturn_assembly.output, S8("call \"die_plain\"\n\tmov eax, 0x0")) != BUSTER_STRING_NO_MATCH);
    // The x86-64 byte rows of XCHG and CMPXCHG (#806). `xchg r/m8, r8` is
    // opcode 0x86 and `cmpxchg r/m8, r8` is 0x0F 0xB0 -- a different metadata
    // form from the 0x87 / 0x0F 0xB1 sibling each shares a recipe with, not an
    // operand-size variant of it -- so a width-1 atomic store or
    // compare-exchange selected on the machine path and then failed to encode.
    // The fallback count is the assertion that says the byte rows are reached:
    // running alone would pass on the canonical emitter too. It is pinned at
    // zero under every allocator, NONE included, because nothing in this
    // fixture has any other reason to refuse.
    String8 c_atomic_byte_allocators[] = {
        S8("none"),
        S8("mir-stack"),
        S8("fast"),
        S8("quality"),
    };
    for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_atomic_byte_allocators); allocator_index += 1)
    {
        TemporalArena atomic_byte_temporary = scratch_begin(&arguments->arena, 1);
        String8 atomic_byte_path = buster_test_temporary_path(atomic_byte_temporary.arena, S8("buster-c-atomic-byte-exchange"),
                                                              string_format(atomic_byte_temporary.arena, S8("-{S8}"), c_atomic_byte_allocators[allocator_index]));
        String8 atomic_byte_command_line[] = {
            S8("-target"),
            S8("x86_64-unknown-linux-gnu"),
            string_format(atomic_byte_temporary.arena, S8("-fregister-allocator={S8}"), c_atomic_byte_allocators[allocator_index]),
            S8("-o"),
            atomic_byte_path,
            S8("tests/basic_c_atomic_byte_exchange.c"),
        };
        CompilerDriverResult atomic_byte = compiler_driver_execute_invocation(
            atomic_byte_temporary.arena,
            compiler_driver_parse_arguments(atomic_byte_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_byte_command_line)));
        BUSTER_TEST(arguments, atomic_byte.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, atomic_byte.codegen_statistics.fallback_function_count == 0u);
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        if (atomic_byte.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 atomic_byte_arguments[] = {atomic_byte_path};
            ProcessSpawnResult atomic_byte_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_byte_arguments), (SliceString8){0},
                                                                    (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, atomic_byte_spawn.handle != 0);
            if (atomic_byte_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(atomic_byte_temporary.arena, atomic_byte_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
#endif
        scratch_end(atomic_byte_temporary);
    }
    // A byte source in SIL, DIL, SPL or BPL needs a bare REX prefix that the
    // low four registers do not, and an allocator is free to pick one. The
    // assembly is where both halves are visible: `f0 40 0f b0` is the locked
    // byte compare-exchange carrying that REX byte, and `86` the byte
    // exchange, against the `87` its sibling row would have written.
    String8 c_atomic_byte_assembly_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("x86_64-unknown-linux-gnu"),
        S8("-fregister-allocator=fast"),
        S8("tests/basic_c_atomic_byte_exchange.c"),
    };
    CompilerDriverResult c_atomic_byte_assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_atomic_byte_assembly_command_line)));
    BUSTER_TEST(arguments, c_atomic_byte_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(c_atomic_byte_assembly.output, S8("0x86, 0x02")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(c_atomic_byte_assembly.output, S8("0xf0, 0x40, 0x0f, 0xb0")) != BUSTER_STRING_NO_MATCH);
    // Nine frontend gaps, each with its own fixture so a regression names the
    // contract it broke. Seven came from the 2026-08-30 differential harness
    // run and two from the CPython configure differential: a
    // positional initializer storing into an anonymous bit-field instead of
    // skipping it (#818), a typedef taking an attributed struct definition's
    // attribute operand as the alias's own alignment (#819), a statement
    // expression refused in every lazily lowered control position (#820), GNU
    // `~` conjugation and `__builtin_complex` (#822, #823), GNU `aligned` on a
    // bit-field (#824), and a `[*]` prototype conflicting with the `[n]`
    // definition it forward-declares (#825), and the GNU `__atomic_*` builtin
    // family, which the frontend had only the `__c11_atomic_*` spelling of
    // (#829), and the function-pointer conversions that must stay legal beside
    // the incompatible ones now refused (#830), and a read-modify-write on an
    // atomic floating-point object, which lowers to a compare-exchange loop
    // (#821). All nine run under every register allocator: five are layout or
    // lowering defects rather than parsing ones, the layout pair has to agree
    // between the sizeof folding in the parse and the IR layout, and the
    // atomic-float loop has to terminate -- none of which a compile alone
    // proves.
    String8 c_differential_regression_paths[] = {
        S8("tests/basic_c_anonymous_bit_field_initializer.c"),
        S8("tests/basic_c_attributed_struct_typedef_alignment.c"),
        S8("tests/basic_c_statement_expression_condition.c"),
        S8("tests/basic_c_complex_conjugate.c"),
        S8("tests/basic_c_bit_field_aligned.c"),
        S8("tests/basic_c_unspecified_array_parameter.c"),
        S8("tests/basic_c_gnu_atomic_builtins.c"),
        S8("tests/basic_c_function_pointer_compatibility.c"),
        S8("tests/basic_c_atomic_float_update.c"),
    };
    String8 c_differential_regression_names[] = {
        S8("buster-c-anonymous-bit-field-initializer"),
        S8("buster-c-attributed-struct-typedef-alignment"),
        S8("buster-c-statement-expression-condition"),
        S8("buster-c-complex-conjugate"),
        S8("buster-c-bit-field-aligned"),
        S8("buster-c-unspecified-array-parameter"),
        S8("buster-c-gnu-atomic-builtins"),
        S8("buster-c-function-pointer-compatibility"),
        S8("buster-c-atomic-float-update"),
    };
    for (u64 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_differential_regression_paths); fixture_index += 1)
    {
        for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
        {
            TemporalArena differential_temporary = scratch_begin(&arguments->arena, 1);
            String8 fixture_path = buster_test_temporary_path(differential_temporary.arena, c_differential_regression_names[fixture_index], S8(""));
            String8 fixture_command_line[] = {
                c_lz4_regression_allocators[allocator_index], S8("-o"), fixture_path, c_differential_regression_paths[fixture_index],
            };
            CompilerDriverResult fixture = compiler_driver_execute_invocation(
                differential_temporary.arena,
                compiler_driver_parse_arguments(differential_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
            BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
            if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 fixture_arguments[] = {fixture_path};
                ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, fixture_spawn.handle != 0);
                if (fixture_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(differential_temporary.arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(differential_temporary);
        }
    }
    // `_Alignas` on a bit-field stays refused, which is the half of #824 that
    // is not a gap: C11 6.7.5p2 forbids it and both reference compilers refuse
    // it. Asserting it here is what keeps the spelling partition above from
    // being widened into an unconditional acceptance.
    String8 c_bit_field_alignas_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("x86_64-unknown-linux-gnu"),
        S8("tests/basic_c_bit_field_alignas.c"),
    };
    CompilerDriverResult c_bit_field_alignas = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_bit_field_alignas_command_line)));
    BUSTER_TEST(arguments, c_bit_field_alignas.error != COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(c_bit_field_alignas.diagnostic, S8("alignment specifier cannot be applied to a bit-field")) != BUSTER_STRING_NO_MATCH);
    // The refused half of #830: an assignment between incompatible function
    // prototypes. It used to compile in silence, which is what flipped
    // CPython's readline probe. The assertion is on the diagnostic because a
    // successful compile is the failure mode.
    String8 c_function_pointer_conflict_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("x86_64-unknown-linux-gnu"),
        S8("tests/basic_c_function_pointer_conflict.c"),
    };
    CompilerDriverResult c_function_pointer_conflict = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_function_pointer_conflict_command_line)));
    BUSTER_TEST(arguments, c_function_pointer_conflict.error != COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(c_function_pointer_conflict.diagnostic, S8("incompatible function pointer type")) != BUSTER_STRING_NO_MATCH);
    // An inline-assembly template the emitter refuses must name the rule that
    // refused it and the register it named, not leak an opcode number (#831).
    // The refusal itself is by design, so the assertion is on the wording; the
    // fixture is expected never to compile.
    String8 c_asm_literal_register_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("x86_64-unknown-linux-gnu"),
        S8("tests/basic_c_asm_literal_register.c"),
    };
    CompilerDriverResult c_asm_literal_register = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_literal_register_command_line)));
    BUSTER_TEST(arguments, c_asm_literal_register.error == COMPILER_DRIVER_ERROR_CODEGEN);
    BUSTER_TEST(arguments, string_first_sequence(c_asm_literal_register.diagnostic, S8("names the literal register '%eax'")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(c_asm_literal_register.diagnostic, S8("opcode")) == BUSTER_STRING_NO_MATCH);
    // Decimal literals with a large exponent still require correctly rounded
    // binary conversion.  This exact bit pattern is the value Clang emits
    // for 123e+127; an accumulated decimal f64 can drift by two ulps.
    String8 c_float_literal_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-literal"), S8(""));
    String8 c_float_literal_command_line[] = {
        S8("-o"),
        c_float_literal_path,
        S8("tests/basic_c_float_literal.c"),
    };
    CompilerDriverResult c_float_literal = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_literal_command_line)));
    BUSTER_TEST(arguments, c_float_literal.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_literal.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_float_literal_arguments[] = {
            c_float_literal_path,
        };
        ProcessSpawnResult c_float_literal_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_literal_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_float_literal_spawn.handle != 0);
        if (c_float_literal_spawn.handle)
        {
            ProcessWaitResult c_float_literal_wait = os_process_wait_sync(arguments->arena, c_float_literal_spawn);
            BUSTER_TEST(arguments, c_float_literal_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // The same conversion must answer for a literal that initializes static
    // storage.  The global initializer writers used to decode by accumulating
    // digits into an f64, so a large or small decimal exponent landed ulps
    // away from the value the identical literal produced inside a function
    // body, and a subnormal flushed to zero.
    String8 c_static_float_literal_path = buster_test_temporary_path(arguments->arena, S8("buster-c-static-float-literal"), S8(""));
    String8 c_static_float_literal_command_line[] = {
        S8("-o"),
        c_static_float_literal_path,
        S8("tests/basic_c_static_float_literal.c"),
    };
    CompilerDriverResult c_static_float_literal = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_float_literal_command_line)));
    BUSTER_TEST(arguments, c_static_float_literal.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_static_float_literal.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_static_float_literal_arguments[] = {
            c_static_float_literal_path,
        };
        ProcessSpawnResult c_static_float_literal_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_static_float_literal_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_static_float_literal_spawn.handle != 0);
        if (c_static_float_literal_spawn.handle)
        {
            ProcessWaitResult c_static_float_literal_wait = os_process_wait_sync(arguments->arena, c_static_float_literal_spawn);
            BUSTER_TEST(arguments, c_static_float_literal_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A void cast around an assignment must still execute its store.  This is
    // the exact loop-update shape used by cJSON_Utils.c.
    String8 c_void_assignment_path = buster_test_temporary_path(arguments->arena, S8("buster-c-void-assignment"), S8(""));
    String8 c_void_assignment_command_line[] = {
        S8("-o"),
        c_void_assignment_path,
        S8("tests/basic_c_void_assignment.c"),
    };
    CompilerDriverResult c_void_assignment = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_void_assignment_command_line)));
    BUSTER_TEST(arguments, c_void_assignment.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_void_assignment.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_void_assignment_arguments[] = {
            c_void_assignment_path,
        };
        ProcessSpawnResult c_void_assignment_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_void_assignment_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_void_assignment_spawn.handle != 0);
        if (c_void_assignment_spawn.handle)
        {
            ProcessWaitResult c_void_assignment_wait = os_process_wait_sync(arguments->arena, c_void_assignment_spawn);
            BUSTER_TEST(arguments, c_void_assignment_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // A GNU statement expression whose body contains control flow evaluates to
    // its final expression statement. The wrong answer was 0 in every context
    // -- initializer, return, assignment, arithmetic operand, call argument --
    // with no diagnostic, so the fixture runs, and it runs under all four
    // register allocators because the value crosses the block boundary the
    // control statement opened.
    String8 c_statement_expression_value_allocator_flags[] = {
        S8("-fregister-allocator=none"),
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=quality"),
    };
    for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_statement_expression_value_allocator_flags); allocator_index += 1)
    {
        TemporalArena c_statement_expression_value_temporary = arena_begin_temporal(arguments->arena);
        String8 c_statement_expression_value_path =
            buster_test_temporary_path(c_statement_expression_value_temporary.arena, S8("buster-c-statement-expression-value"),
                                       string_format(c_statement_expression_value_temporary.arena, S8("-{u32}"), allocator_index));
        String8 c_statement_expression_value_command_line[] = {
            c_statement_expression_value_allocator_flags[allocator_index],
            S8("-o"),
            c_statement_expression_value_path,
            S8("tests/basic_c_statement_expression_value.c"),
        };
        CompilerDriverResult c_statement_expression_value = compiler_driver_execute_invocation(
            c_statement_expression_value_temporary.arena,
            compiler_driver_parse_arguments(c_statement_expression_value_temporary.arena,
                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(c_statement_expression_value_command_line)));
        BUSTER_TEST(arguments, c_statement_expression_value.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_statement_expression_value.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 c_statement_expression_value_arguments[] = {
                c_statement_expression_value_path,
            };
            ProcessSpawnResult c_statement_expression_value_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_statement_expression_value_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, c_statement_expression_value_spawn.handle != 0);
            if (c_statement_expression_value_spawn.handle)
            {
                ProcessWaitResult c_statement_expression_value_wait =
                    os_process_wait_sync(c_statement_expression_value_temporary.arena, c_statement_expression_value_spawn);
                BUSTER_TEST(arguments, c_statement_expression_value_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(c_statement_expression_value_temporary);
    }
    // Every fixture here folds a sizeof or _Alignof whose wrong lowering still
    // produces a plausible constant -- an unevaluated operand, an enum constant
    // over an object sizeof, a compound literal, a call-typed array bound, a
    // function designator, an expression operand under GNU's `__alignof__` --
    // so reading the diagnostic is no evidence and the fixtures run.
    String8 c_runtime_fixture_paths[] = {
        S8("tests/basic_c_sizeof_unevaluated.c"),
        S8("tests/basic_c_enum_sizeof_object.c"),
        S8("tests/basic_c_sizeof_compound_literal.c"),
        S8("tests/basic_c_sizeof_call_array_bound.c"),
        S8("tests/basic_c_sizeof_function_designator.c"),
        S8("tests/basic_c_alignof_expression.c"),
        // The two sbase fixtures belong here for the same reason: a wrong
        // lowering of a self-referential initializer or of `onestr + 1` still
        // produces a program, and only running it reads the pointer.
        S8("tests/basic_c_sbase_declarations.c"),
        S8("tests/basic_c_sbase_expressions.c"),
    };
    String8 c_runtime_fixture_names[] = {
        S8("buster-c-sizeof-unevaluated"),
        S8("buster-c-enum-sizeof-object"),
        S8("buster-c-sizeof-compound-literal"),
        S8("buster-c-sizeof-call-array-bound"),
        S8("buster-c-sizeof-function-designator"),
        S8("buster-c-alignof-expression"),
        S8("buster-c-sbase-declarations"),
        S8("buster-c-sbase-expressions"),
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
    // The musl singleton fixtures. Each one is a shape musl uses that the
    // frontend refused -- or, for the last one, answered with the wrong sign
    // -- and each answer is observable at run time rather than only in a
    // diagnostic, so they run rather than merely compile -- and run under
    // every register allocator, because the value a subscript or a
    // typeof-declared call produces must not depend on which one placed it.
    String8 c_musl_shape_fixture_paths[] = {
        S8("tests/basic_c_reversed_subscript.c"),
        S8("tests/basic_c_macro_self_reference.c"),
        S8("tests/basic_c_typeof_declaration.c"),
        S8("tests/basic_c_opaque_tag_cast.c"),
        S8("tests/basic_c_designated_subscript_initializer.c"),
        S8("tests/basic_c_conditional_array_branch.c"),
        S8("tests/basic_c_array_parameter_assignment.c"),
        S8("tests/basic_c_unparenthesized_sizeof_initializer.c"),
        S8("tests/basic_c_incrementing_assignment_condition.c"),
        S8("tests/basic_c_null_pointer_offsetof.c"),
        S8("tests/basic_c_created_nan_sign.c"),
        S8("tests/basic_c_static_compound_literal.c"),
        S8("tests/basic_c_typeof_conditional.c"),
    };
    String8 c_musl_shape_fixture_names[] = {
        S8("buster-c-reversed-subscript"),
        S8("buster-c-macro-self-reference"),
        S8("buster-c-typeof-declaration"),
        S8("buster-c-opaque-tag-cast"),
        S8("buster-c-designated-subscript-initializer"),
        S8("buster-c-conditional-array-branch"),
        S8("buster-c-array-parameter-assignment"),
        S8("buster-c-unparenthesized-sizeof-initializer"),
        S8("buster-c-incrementing-assignment-condition"),
        S8("buster-c-null-pointer-offsetof"),
        S8("buster-c-created-nan-sign"),
        S8("buster-c-static-compound-literal"),
        S8("buster-c-typeof-conditional"),
    };
    String8 c_musl_shape_allocator_flags[] = {
        S8("-fregister-allocator=none"),
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=quality"),
    };
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(c_musl_shape_fixture_paths); fixture_index += 1)
    {
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_musl_shape_allocator_flags); allocator_index += 1)
        {
            TemporalArena c_musl_shape_temporary = arena_begin_temporal(arguments->arena);
            String8 c_musl_shape_path =
                buster_test_temporary_path(c_musl_shape_temporary.arena, c_musl_shape_fixture_names[fixture_index],
                                           string_format(c_musl_shape_temporary.arena, S8("-{u32}"), allocator_index));
            String8 c_musl_shape_command_line[] = {
                c_musl_shape_allocator_flags[allocator_index],
                S8("-o"),
                c_musl_shape_path,
                c_musl_shape_fixture_paths[fixture_index],
            };
            CompilerDriverResult c_musl_shape = compiler_driver_execute_invocation(
                c_musl_shape_temporary.arena,
                compiler_driver_parse_arguments(c_musl_shape_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_musl_shape_command_line)));
            BUSTER_TEST(arguments, c_musl_shape.error == COMPILER_DRIVER_ERROR_NONE);
            if (c_musl_shape.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 c_musl_shape_arguments[] = {
                    c_musl_shape_path,
                };
                ProcessSpawnResult c_musl_shape_spawn =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_musl_shape_arguments), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){
                                         .use_process_environment = true,
                                     });
                BUSTER_TEST(arguments, c_musl_shape_spawn.handle != 0);
                if (c_musl_shape_spawn.handle)
                {
                    ProcessWaitResult c_musl_shape_wait = os_process_wait_sync(c_musl_shape_temporary.arena, c_musl_shape_spawn);
                    BUSTER_TEST(arguments, c_musl_shape_wait.result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(c_musl_shape_temporary);
        }
    }
    // A GNU statement expression standing in a declaration's initializer.
    // The body declares names the statements after it read, and only running
    // the program proves the reads found them: the scope the body needs is
    // opened while the declaration is bound, well before code generation, and
    // a body whose locals resolved to the enclosing block's names of the same
    // spelling still compiles. It runs under every register allocator for the
    // same reason the musl shapes do.
    String8 c_statement_expression_allocator_flags[] = {
        S8("-fregister-allocator=none"),
        S8("-fregister-allocator=mir-stack"),
        S8("-fregister-allocator=fast"),
        S8("-fregister-allocator=quality"),
    };
    for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_statement_expression_allocator_flags); allocator_index += 1)
    {
        TemporalArena c_statement_expression_temporary = arena_begin_temporal(arguments->arena);
        String8 c_statement_expression_path = buster_test_temporary_path(
            c_statement_expression_temporary.arena, S8("buster-c-statement-expression"),
            string_format(c_statement_expression_temporary.arena, S8("-{u32}"), allocator_index));
        String8 c_statement_expression_command_line[] = {
            S8("-std=gnu23"),
            c_statement_expression_allocator_flags[allocator_index],
            S8("-o"),
            c_statement_expression_path,
            S8("tests/basic_c_statement_expression.c"),
        };
        CompilerDriverResult c_statement_expression = compiler_driver_execute_invocation(
            c_statement_expression_temporary.arena,
            compiler_driver_parse_arguments(c_statement_expression_temporary.arena,
                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(c_statement_expression_command_line)));
        BUSTER_TEST(arguments, c_statement_expression.error == COMPILER_DRIVER_ERROR_NONE);
        if (c_statement_expression.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 c_statement_expression_arguments[] = {
                c_statement_expression_path,
            };
            ProcessSpawnResult c_statement_expression_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_statement_expression_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, c_statement_expression_spawn.handle != 0);
            if (c_statement_expression_spawn.handle)
            {
                ProcessWaitResult c_statement_expression_wait = os_process_wait_sync(c_statement_expression_temporary.arena, c_statement_expression_spawn);
                BUSTER_TEST(arguments, c_statement_expression_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(c_statement_expression_temporary);
    }
    // A negative constant narrower than the global it initializes is folded
    // and written into the data image without ever reaching code generation,
    // so only running the program reads the bytes the object writer emitted.
    // The fixture reads its images both directly and through pointers, which
    // separates a wrong stored image from a wrong constant fold.
    String8 c_negative_constant_widening_path = buster_test_temporary_path(arguments->arena, S8("buster-c-negative-constant-widening"), S8(""));
    String8 c_negative_constant_widening_command_line[] = {
        S8("-o"),
        c_negative_constant_widening_path,
        S8("tests/basic_c_negative_constant_widening.c"),
    };
    CompilerDriverResult c_negative_constant_widening =
        compiler_driver_execute_invocation(arguments->arena, compiler_driver_parse_arguments(
                                                                 arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_negative_constant_widening_command_line)));
    BUSTER_TEST(arguments, c_negative_constant_widening.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_negative_constant_widening.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_negative_constant_widening_arguments[] = {
            c_negative_constant_widening_path,
        };
        ProcessSpawnResult c_negative_constant_widening_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_negative_constant_widening_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_negative_constant_widening_spawn.handle != 0);
        if (c_negative_constant_widening_spawn.handle)
        {
            ProcessWaitResult c_negative_constant_widening_wait = os_process_wait_sync(arguments->arena, c_negative_constant_widening_spawn);
            BUSTER_TEST(arguments, c_negative_constant_widening_wait.result == PROCESS_RESULT_SUCCESS);
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
    // A call to a function declared `()` places its arguments from its own
    // promoted types, because the declaration names none. Only the callees'
    // own translation unit has the prototypes, so nothing in the caller can
    // recover them and the host's ABI decides whether each argument arrived:
    // a wrong answer here is a wrong register or stack slot, which is exactly
    // what the Darwin AArch64 and Win64 variadic rules would produce if the
    // call site were lowered as a variadic tail rather than named arguments.
    String8 c_unprototyped_path = buster_test_temporary_path(arguments->arena, S8("buster-c-unprototyped-call"), S8(""));
    String8 c_unprototyped_command_line[] = {
        S8("-o"),
        c_unprototyped_path,
        S8("tests/basic_c_unprototyped_call.c"),
        S8("tests/basic_c_unprototyped_call_callee.c"),
    };
    CompilerDriverResult c_unprototyped = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_unprototyped_command_line)));
    BUSTER_TEST(arguments, c_unprototyped.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_unprototyped.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_unprototyped_arguments[] = {
            c_unprototyped_path,
        };
        ProcessSpawnResult c_unprototyped_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_unprototyped_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_unprototyped_spawn.handle != 0);
        if (c_unprototyped_spawn.handle)
        {
            ProcessWaitResult c_unprototyped_wait = os_process_wait_sync(arguments->arena, c_unprototyped_spawn);
            BUSTER_TEST(arguments, c_unprototyped_wait.result == PROCESS_RESULT_SUCCESS);
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
    // The inline-assembly vocabulary a libc's atomics are written in, and the
    // aggregate-defining type names its math library punts floats through.
    // Both are run rather than only compiled: a memory operand that reaches
    // the wrong address, and a compound literal that reads the wrong member,
    // both assemble.
    {
        String8 libc_shape_fixtures[] = {S8("tests/basic_c_atomic_asm.c"), S8("tests/basic_c_compound_literal_type.c")};
        String8 libc_shape_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(libc_shape_fixtures); fixture_index += 1)
        {
            for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(libc_shape_allocators); allocator_index += 1)
            {
                Arena* libc_shape_conflicts[] = {
                    arguments->arena,
                    c_asm_arena,
                };
                TemporalArena libc_shape_temporary = scratch_begin(libc_shape_conflicts, BUSTER_ARRAY_LENGTH(libc_shape_conflicts));
                Arena* libc_shape_arena = libc_shape_temporary.arena;
                String8 libc_shape_path =
                    buster_test_temporary_path(libc_shape_arena, S8("buster-c-libc-shape"),
                                               string_format(libc_shape_arena, S8("-{u32}-{u32}"), fixture_index, allocator_index));
                String8 libc_shape_command_line[] = {
                    string_format(libc_shape_arena, S8("-fregister-allocator={S8}"), libc_shape_allocators[allocator_index]),
                    S8("-o"),
                    libc_shape_path,
                    libc_shape_fixtures[fixture_index],
                };
                CompilerDriverResult libc_shape = compiler_driver_execute_invocation(
                    libc_shape_arena,
                    compiler_driver_parse_arguments(libc_shape_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(libc_shape_command_line)));
                BUSTER_TEST(arguments, libc_shape.error == COMPILER_DRIVER_ERROR_NONE);
                if (libc_shape.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 run_arguments[] = {libc_shape_path};
                    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                                (ProcessSpawnOptions){.use_process_environment = true});
                    BUSTER_TEST(arguments, spawn.handle != 0);
                    if (spawn.handle)
                    {
                        BUSTER_TEST(arguments, os_process_wait_sync(libc_shape_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                    }
                }
                scratch_end(libc_shape_temporary);
            }
        }
#if BUSTER_CPU_ARCH_X86_64
        // The thread-pointer read is in the atomics fixture but never called
        // there, because a program this driver links has no thread area. Its
        // encoding is what the fixture is carrying it for. The fixture compiles
        // to an empty main off x86-64, where there is no such encoding to find.
        Arena* segment_conflicts[] = {
            arguments->arena,
            c_asm_arena,
        };
        TemporalArena segment_temporary = scratch_begin(segment_conflicts, BUSTER_ARRAY_LENGTH(segment_conflicts));
        Arena* segment_arena = segment_temporary.arena;
        String8 segment_path = buster_test_temporary_path(segment_arena, S8("buster-c-segment-override"), S8(".o"));
        String8 segment_command_line[] = {S8("-c"), S8("-g0"), S8("-o"), segment_path, S8("tests/basic_c_atomic_asm.c")};
        CompilerDriverResult segment = compiler_driver_execute_invocation(
            segment_arena, compiler_driver_parse_arguments(segment_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(segment_command_line)));
        BUSTER_TEST(arguments, segment.error == COMPILER_DRIVER_ERROR_NONE);
        if (segment.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, segment.has_object);
            BUSTER_TEST(arguments, compiler_driver_test_x64_segment_override_load(&segment.object));
        }
        scratch_end(segment_temporary);
#endif
    }
    // __attribute__((weak)) and __attribute__((alias)), which is how musl
    // publishes malloc, free, errno and most of its pthread surface: an
    // archive built without them compiles and does not link. The fixture is
    // both run and read, because the two failures are different -- running
    // proves each alias reaches the right code and the right object, and the
    // symbol table proves the binding and the coincidence with the target
    // that another translation unit's linker resolves against.
    {
        String8 weak_alias_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(weak_alias_allocators); allocator_index += 1)
        {
            Arena* weak_alias_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena weak_alias_temporary = scratch_begin(weak_alias_conflicts, BUSTER_ARRAY_LENGTH(weak_alias_conflicts));
            Arena* weak_alias_arena = weak_alias_temporary.arena;
            String8 weak_alias_path = buster_test_temporary_path(weak_alias_arena, S8("buster-c-weak-alias"),
                                                                  string_format(weak_alias_arena, S8("-{u32}"), allocator_index));
            String8 weak_alias_command_line[] = {
                string_format(weak_alias_arena, S8("-fregister-allocator={S8}"), weak_alias_allocators[allocator_index]),
                S8("-o"),
                weak_alias_path,
                S8("tests/basic_c_weak_alias.c"),
            };
            CompilerDriverResult weak_alias = compiler_driver_execute_invocation(
                weak_alias_arena, compiler_driver_parse_arguments(weak_alias_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_alias_command_line)));
            BUSTER_TEST(arguments, weak_alias.error == COMPILER_DRIVER_ERROR_NONE);
            if (weak_alias.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {weak_alias_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(weak_alias_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(weak_alias_temporary);
        }
        Arena* weak_alias_object_conflicts[] = {
            arguments->arena,
            c_asm_arena,
        };
        TemporalArena weak_alias_object_temporary = scratch_begin(weak_alias_object_conflicts, BUSTER_ARRAY_LENGTH(weak_alias_object_conflicts));
        Arena* weak_alias_object_arena = weak_alias_object_temporary.arena;
        String8 weak_alias_object_path = buster_test_temporary_path(weak_alias_object_arena, S8("buster-c-weak-alias-object"), S8(".o"));
        String8 weak_alias_object_command_line[] = {S8("-c"), S8("-g0"), S8("-o"), weak_alias_object_path, S8("tests/basic_c_weak_alias.c")};
        CompilerDriverResult weak_alias_object = compiler_driver_execute_invocation(
            weak_alias_object_arena,
            compiler_driver_parse_arguments(weak_alias_object_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_alias_object_command_line)));
        BUSTER_TEST(arguments, weak_alias_object.error == COMPILER_DRIVER_ERROR_NONE);
        if (weak_alias_object.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, weak_alias_object.has_object);
            BUSTER_TEST(arguments, compiler_driver_test_weak_alias_symbols(&weak_alias_object.object));
        }
        scratch_end(weak_alias_object_temporary);
    }
    // Local register variables under every allocator, with -std=c99 because
    // that is the dialect a libc's build actually asks for. The fixture proves
    // the binding through the kernel rather than through a round trip: it
    // passes system-call arguments four, five and six, which have no x86-64
    // constraint letter and reach R10, R8 and R9 only when the binding holds.
    {
        String8 allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(allocators); allocator_index += 1)
        {
            Arena* register_variable_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena register_variable_temporary = scratch_begin(register_variable_conflicts, BUSTER_ARRAY_LENGTH(register_variable_conflicts));
            Arena* register_variable_arena = register_variable_temporary.arena;
            String8 register_variable_path = buster_test_temporary_path(register_variable_arena, S8("buster-c-register-variable"),
                                                                        string_format(register_variable_arena, S8("-{u32}"), allocator_index));
            String8 register_variable_command_line[] = {
                S8("-std=c99"),
                string_format(register_variable_arena, S8("-fregister-allocator={S8}"), allocators[allocator_index]),
                S8("-o"),
                register_variable_path,
                S8("tests/basic_c_register_variable.c"),
            };
            CompilerDriverResult register_variable = compiler_driver_execute_invocation(
                register_variable_arena,
                compiler_driver_parse_arguments(register_variable_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(register_variable_command_line)));
            BUSTER_TEST(arguments, register_variable.error == COMPILER_DRIVER_ERROR_NONE);
            if (register_variable.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {register_variable_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(register_variable_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(register_variable_temporary);
        }
    }
    // Converting a narrower integer to a pointer under every allocator.  The
    // fixture's own comment carries the rule; what it is here for is that
    // INTEGER_TO_POINTER is a plain register copy in all four backends, so the
    // widening the C frontend now emits ahead of it is the only thing keeping
    // `(void *)-1` from arriving as the low half of a pointer.  It exits
    // non-zero on the first wrong answer, naming the case.
    {
        String8 integer_to_pointer_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(integer_to_pointer_allocators); allocator_index += 1)
        {
            Arena* integer_to_pointer_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena integer_to_pointer_temporary = scratch_begin(integer_to_pointer_conflicts, BUSTER_ARRAY_LENGTH(integer_to_pointer_conflicts));
            Arena* integer_to_pointer_arena = integer_to_pointer_temporary.arena;
            String8 integer_to_pointer_path = buster_test_temporary_path(integer_to_pointer_arena, S8("buster-c-integer-to-pointer"),
                                                                        string_format(integer_to_pointer_arena, S8("-{u32}"), allocator_index));
            String8 integer_to_pointer_command_line[] = {
                string_format(integer_to_pointer_arena, S8("-fregister-allocator={S8}"), integer_to_pointer_allocators[allocator_index]),
                S8("-o"),
                integer_to_pointer_path,
                S8("tests/basic_c_integer_to_pointer.c"),
            };
            CompilerDriverResult integer_to_pointer = compiler_driver_execute_invocation(
                integer_to_pointer_arena,
                compiler_driver_parse_arguments(integer_to_pointer_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(integer_to_pointer_command_line)));
            BUSTER_TEST(arguments, integer_to_pointer.error == COMPILER_DRIVER_ERROR_NONE);
            if (integer_to_pointer.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {integer_to_pointer_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(integer_to_pointer_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(integer_to_pointer_temporary);
        }
    }
    // The one function in C whose fall-off is defined: reaching the `}` that
    // terminates `main` returns 0 (C 5.1.2.2.3), where every other function's
    // is undefined and terminates with the IR's unreachable.  The fixture's
    // own comment carries the rule; what it is here for is that the fall-off
    // is a code-generation terminator, so all four allocators have to agree
    // that the zero is materialized and no trap follows it.  Exit zero is
    // reachable only through the closing brace, so the run is the assertion.
    {
        String8 main_implicit_return_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(main_implicit_return_allocators); allocator_index += 1)
        {
            Arena* main_implicit_return_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena main_implicit_return_temporary = scratch_begin(main_implicit_return_conflicts, BUSTER_ARRAY_LENGTH(main_implicit_return_conflicts));
            Arena* main_implicit_return_arena = main_implicit_return_temporary.arena;
            String8 main_implicit_return_path = buster_test_temporary_path(main_implicit_return_arena, S8("buster-c-main-implicit-return"),
                                                                          string_format(main_implicit_return_arena, S8("-{u32}"), allocator_index));
            String8 main_implicit_return_command_line[] = {
                string_format(main_implicit_return_arena, S8("-fregister-allocator={S8}"), main_implicit_return_allocators[allocator_index]),
                S8("-o"),
                main_implicit_return_path,
                S8("tests/basic_c_main_implicit_return.c"),
            };
            CompilerDriverResult main_implicit_return = compiler_driver_execute_invocation(
                main_implicit_return_arena,
                compiler_driver_parse_arguments(main_implicit_return_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(main_implicit_return_command_line)));
            BUSTER_TEST(arguments, main_implicit_return.error == COMPILER_DRIVER_ERROR_NONE);
            if (main_implicit_return.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {main_implicit_return_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(main_implicit_return_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(main_implicit_return_temporary);
        }
    }
    // Dereferencing a pointer to an array under every allocator.  `*p`
    // designates the array object, so the walk hands its place back instead of
    // loading it; a load would copy the whole array into a frame temporary and
    // everything downstream -- a subscript store, a decay into a call, a
    // returned pointer -- would address the copy.  That is the shape musl's
    // strftime is written in, and it is what made libc-test's
    // `functional/strftime` disagree with the reference on every format.  It
    // exits non-zero on the first wrong answer, naming the case.
    {
        String8 pointer_to_array_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(pointer_to_array_allocators); allocator_index += 1)
        {
            Arena* pointer_to_array_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena pointer_to_array_temporary = scratch_begin(pointer_to_array_conflicts, BUSTER_ARRAY_LENGTH(pointer_to_array_conflicts));
            Arena* pointer_to_array_arena = pointer_to_array_temporary.arena;
            String8 pointer_to_array_path = buster_test_temporary_path(pointer_to_array_arena, S8("buster-c-pointer-to-array-place"),
                                                                      string_format(pointer_to_array_arena, S8("-{u32}"), allocator_index));
            String8 pointer_to_array_command_line[] = {
                string_format(pointer_to_array_arena, S8("-fregister-allocator={S8}"), pointer_to_array_allocators[allocator_index]),
                S8("-o"),
                pointer_to_array_path,
                S8("tests/basic_c_pointer_to_array_place.c"),
            };
            CompilerDriverResult pointer_to_array = compiler_driver_execute_invocation(
                pointer_to_array_arena,
                compiler_driver_parse_arguments(pointer_to_array_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pointer_to_array_command_line)));
            BUSTER_TEST(arguments, pointer_to_array.error == COMPILER_DRIVER_ERROR_NONE);
            if (pointer_to_array.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {pointer_to_array_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(pointer_to_array_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(pointer_to_array_temporary);
        }
    }
    // Short-circuit and conditional operands inside a call argument, under
    // every allocator.  The fixture's own comment carries the rule; what it is
    // here for is that a call argument is the position where the arithmetic
    // core runs the control-expression prepass, so a parenthesized group in a
    // lazy operand is the one shape both prepasses have to agree to leave
    // alone.  It exits non-zero on the first wrong answer, naming the case.
    {
        String8 lazy_operand_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(lazy_operand_allocators); allocator_index += 1)
        {
            Arena* lazy_operand_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena lazy_operand_temporary = scratch_begin(lazy_operand_conflicts, BUSTER_ARRAY_LENGTH(lazy_operand_conflicts));
            Arena* lazy_operand_arena = lazy_operand_temporary.arena;
            String8 lazy_operand_path = buster_test_temporary_path(lazy_operand_arena, S8("buster-c-lazy-operand-argument"),
                                                                   string_format(lazy_operand_arena, S8("-{u32}"), allocator_index));
            String8 lazy_operand_command_line[] = {
                string_format(lazy_operand_arena, S8("-fregister-allocator={S8}"), lazy_operand_allocators[allocator_index]),
                S8("-o"),
                lazy_operand_path,
                S8("tests/basic_c_lazy_operand_argument.c"),
            };
            CompilerDriverResult lazy_operand = compiler_driver_execute_invocation(
                lazy_operand_arena,
                compiler_driver_parse_arguments(lazy_operand_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(lazy_operand_command_line)));
            BUSTER_TEST(arguments, lazy_operand.error == COMPILER_DRIVER_ERROR_NONE);
            if (lazy_operand.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {lazy_operand_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(lazy_operand_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(lazy_operand_temporary);
        }
    }
    // The three ELF thread-local models under every allocator, twice: a
    // plain build, where a definition in the module is local-exec and a
    // declaration it does not define is initial-exec, and a -fPIC build,
    // where every reference is general-dynamic.  Two things are pinned.  The
    // objects have to carry the right relocations, because the linker relaxes
    // all three back to local-exec in an executable and a run alone cannot
    // tell a general-dynamic build from a local-exec one.  And the programs
    // have to run: general-dynamic is a call that writes RDI and answers in
    // RAX, so every allocator has to keep the values live across it, and the
    // fixture holds four of them at once.  It exits non-zero with its own
    // number on the first wrong answer.
    {
        String8 thread_local_model_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(thread_local_model_allocators); allocator_index += 1)
        {
            for (u32 pic_index = 0; pic_index < 2; pic_index += 1)
            {
                Arena* thread_local_model_conflicts[] = {
                    arguments->arena,
                    c_asm_arena,
                };
                TemporalArena thread_local_model_temporary =
                    scratch_begin(thread_local_model_conflicts, BUSTER_ARRAY_LENGTH(thread_local_model_conflicts));
                Arena* thread_local_model_arena = thread_local_model_temporary.arena;
                String8 allocator_flag =
                    string_format(thread_local_model_arena, S8("-fregister-allocator={S8}"), thread_local_model_allocators[allocator_index]);
                String8 position_independent_flag = pic_index ? S8("-fPIC") : S8("-fno-pic");
                String8 thread_local_model_object_command_line[] = {
                    allocator_flag, position_independent_flag, S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"),
                    S8("tests/basic_c_thread_local_models.c"),
                };
                CompilerDriverResult thread_local_model_object = compiler_driver_execute_invocation(
                    thread_local_model_arena, compiler_driver_parse_arguments(thread_local_model_arena,
                                                                             (SliceString8)BUSTER_ARRAY_TO_SLICE(thread_local_model_object_command_line)));
                BUSTER_TEST(arguments, thread_local_model_object.error == COMPILER_DRIVER_ERROR_NONE);
                BUSTER_TEST(arguments, thread_local_model_object.has_object);
                if (thread_local_model_object.has_object)
                {
                    u32 local_exec_count = 0;
                    u32 initial_exec_count = 0;
                    u32 general_dynamic_count = 0;
                    u32 tls_get_addr_count = 0;
                    for (u32 relocation_index = 0; relocation_index < thread_local_model_object.object.relocation_count; relocation_index += 1)
                    {
                        ObjectRelocation* relocation = thread_local_model_object.object.relocations + relocation_index;
                        local_exec_count += relocation->kind == OBJECT_RELOCATION_X86_64_TPOFF32;
                        initial_exec_count += relocation->kind == OBJECT_RELOCATION_X86_64_GOTTPOFF;
                        general_dynamic_count += relocation->kind == OBJECT_RELOCATION_X86_64_TLSGD;
                        tls_get_addr_count += relocation->kind == OBJECT_RELOCATION_X86_64_PLT32 &&
                                              relocation->symbol < thread_local_model_object.object.symbol_count &&
                                              string_equal(thread_local_model_object.object.symbols[relocation->symbol].name, S8("__tls_get_addr"));
                    }
                    if (pic_index)
                    {
                        // -fPIC takes every reference, defined or not, and
                        // each general-dynamic site is a pair.
                        BUSTER_TEST(arguments, general_dynamic_count != 0);
                        BUSTER_TEST(arguments, tls_get_addr_count == general_dynamic_count);
                        BUSTER_TEST(arguments, local_exec_count == 0);
                        BUSTER_TEST(arguments, initial_exec_count == 0);
                    }
                    else
                    {
                        BUSTER_TEST(arguments, local_exec_count != 0);
                        BUSTER_TEST(arguments, initial_exec_count != 0);
                        BUSTER_TEST(arguments, general_dynamic_count == 0);
                        BUSTER_TEST(arguments, tls_get_addr_count == 0);
                    }
                }
                scratch_end(thread_local_model_temporary);
            }
        }
    }
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    {
        String8 thread_local_run_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(thread_local_run_allocators); allocator_index += 1)
        {
            for (u32 pic_index = 0; pic_index < 2; pic_index += 1)
            {
                Arena* thread_local_run_conflicts[] = {
                    arguments->arena,
                    c_asm_arena,
                };
                TemporalArena thread_local_run_temporary = scratch_begin(thread_local_run_conflicts, BUSTER_ARRAY_LENGTH(thread_local_run_conflicts));
                Arena* thread_local_run_arena = thread_local_run_temporary.arena;
                String8 thread_local_run_path =
                    buster_test_temporary_path(thread_local_run_arena, S8("buster-c-thread-local-models"),
                                               string_format(thread_local_run_arena, S8("-{u32}-{u32}"), allocator_index, pic_index));
                String8 thread_local_run_command_line[] = {
                    string_format(thread_local_run_arena, S8("-fregister-allocator={S8}"), thread_local_run_allocators[allocator_index]),
                    pic_index ? S8("-fPIC") : S8("-fno-pic"),
                    S8("-o"),
                    thread_local_run_path,
                    S8("tests/basic_c_thread_local_models.c"),
                    S8("tests/basic_c_thread_local_models_extern.c"),
                };
                CompilerDriverResult thread_local_run = compiler_driver_execute_invocation(
                    thread_local_run_arena,
                    compiler_driver_parse_arguments(thread_local_run_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(thread_local_run_command_line)));
                BUSTER_TEST(arguments, thread_local_run.error == COMPILER_DRIVER_ERROR_NONE);
                if (thread_local_run.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 run_arguments[] = {thread_local_run_path};
                    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                                (ProcessSpawnOptions){.use_process_environment = true});
                    BUSTER_TEST(arguments, spawn.handle != 0);
                    if (spawn.handle)
                    {
                        BUSTER_TEST(arguments, os_process_wait_sync(thread_local_run_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                    }
                }
                scratch_end(thread_local_run_temporary);
            }
        }
    }
#endif
    // musl's <tgmath.h> machinery under every allocator.  The fixture's own
    // comment carries the three rules; what it is here for is that all three
    // are type-only questions the frontend answers before any code is
    // generated, so a wrong answer is a silently different program rather
    // than a diagnostic -- `sizeof pow(2.0, 0.5)` came back as `long double
    // _Complex` and libc-test's `functional/tgmath` was the only thing that
    // said so.  -std=c99 is part of the test: it is the dialect libc-test and
    // musl's own makefile pass, and the `__GNUC__` half of the fixture only
    // has something to check outside a GNU dialect.
    {
        String8 type_generic_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(type_generic_allocators); allocator_index += 1)
        {
            Arena* type_generic_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena type_generic_temporary = scratch_begin(type_generic_conflicts, BUSTER_ARRAY_LENGTH(type_generic_conflicts));
            Arena* type_generic_arena = type_generic_temporary.arena;
            String8 type_generic_path = buster_test_temporary_path(type_generic_arena, S8("buster-c-type-generic-math"),
                                                                   string_format(type_generic_arena, S8("-{u32}"), allocator_index));
            String8 type_generic_command_line[] = {
                S8("-std=c99"),
                string_format(type_generic_arena, S8("-fregister-allocator={S8}"), type_generic_allocators[allocator_index]),
                S8("-o"),
                type_generic_path,
                S8("tests/basic_c_type_generic_math.c"),
            };
            CompilerDriverResult type_generic = compiler_driver_execute_invocation(
                type_generic_arena, compiler_driver_parse_arguments(type_generic_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(type_generic_command_line)));
            BUSTER_TEST(arguments, type_generic.error == COMPILER_DRIVER_ERROR_NONE);
            if (type_generic.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {type_generic_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(type_generic_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(type_generic_temporary);
        }
    }
    // The two constructs the __GNUC__ predefine change made reachable in
    // musl's own sources.  Both are frontend-only questions -- where a
    // declaration's declarator starts, and what a static initializer folds to
    // -- so one allocator answers them, unlike the type-generic fixture above
    // whose sizes reach code generation.  -std=c99 is again part of the test.
    {
        String8 gnu_specifier_fixtures[] = {
            S8("tests/basic_c_local_typedef_attribute.c"),
            S8("tests/basic_c_offsetof_subscript.c"),
        };
        String8 gnu_specifier_names[] = {
            S8("buster-c-local-typedef-attribute"),
            S8("buster-c-offsetof-subscript"),
        };
        for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(gnu_specifier_fixtures); fixture_index += 1)
        {
            Arena* gnu_specifier_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena gnu_specifier_temporary = scratch_begin(gnu_specifier_conflicts, BUSTER_ARRAY_LENGTH(gnu_specifier_conflicts));
            Arena* gnu_specifier_arena = gnu_specifier_temporary.arena;
            String8 gnu_specifier_path = buster_test_temporary_path(gnu_specifier_arena, gnu_specifier_names[fixture_index], S8(""));
            String8 gnu_specifier_command_line[] = {
                S8("-std=c99"), S8("-o"), gnu_specifier_path, gnu_specifier_fixtures[fixture_index],
            };
            CompilerDriverResult gnu_specifier = compiler_driver_execute_invocation(
                gnu_specifier_arena, compiler_driver_parse_arguments(gnu_specifier_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(gnu_specifier_command_line)));
            BUSTER_TEST(arguments, gnu_specifier.error == COMPILER_DRIVER_ERROR_NONE);
            if (gnu_specifier.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {gnu_specifier_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(gnu_specifier_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(gnu_specifier_temporary);
        }
    }
    // Walking through an intermediate aggregate member under every allocator.
    // `((T *)p)->a.b` names b, and a is only the route: loading a copied a
    // whole object the expression never reads.  The copy was dead but it was
    // still a read, so the offsetof spelled `&(((T *)0)->a.b)` -- what a
    // header writes for a compiler without __builtin_offsetof -- faulted on
    // the null pointer, which is how libc-test's `functional/pthread_robust`
    // and `regression/pthread-robust-detach` died inside musl's
    // `__pthread_exit` (#737) before the __GNUC__ predefine moved musl to the
    // builtin.  The fixture spells the pointer form itself rather than through
    // a header, so it faults when the copy comes back whatever <stddef.h>
    // picks, and exits non-zero on any other wrong answer, naming the case.
    {
        String8 member_chain_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(member_chain_allocators); allocator_index += 1)
        {
            Arena* member_chain_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena member_chain_temporary = scratch_begin(member_chain_conflicts, BUSTER_ARRAY_LENGTH(member_chain_conflicts));
            Arena* member_chain_arena = member_chain_temporary.arena;
            String8 member_chain_path = buster_test_temporary_path(member_chain_arena, S8("buster-c-member-chain-place"),
                                                                   string_format(member_chain_arena, S8("-{u32}"), allocator_index));
            String8 member_chain_command_line[] = {
                string_format(member_chain_arena, S8("-fregister-allocator={S8}"), member_chain_allocators[allocator_index]),
                S8("-o"),
                member_chain_path,
                S8("tests/basic_c_member_chain_place.c"),
            };
            CompilerDriverResult member_chain = compiler_driver_execute_invocation(
                member_chain_arena, compiler_driver_parse_arguments(member_chain_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(member_chain_command_line)));
            BUSTER_TEST(arguments, member_chain.error == COMPILER_DRIVER_ERROR_NONE);
            if (member_chain.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {member_chain_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(member_chain_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(member_chain_temporary);
        }
    }
    // `void *` arithmetic under every allocator.  GNU gives `void` a size of
    // one so that a `void *` steps by bytes; this compiler folded 0, so the
    // index `p + 3` becomes was scaled by nothing and the pointer never moved
    // -- a silently wrong address rather than a diagnostic (#743).  The
    // allocators are here because that index is materialized differently by
    // each: a scale of one is the case where a shift or a LEA disappears, and
    // where the multiply that is gone has to have been the right one.  The
    // fixture reads every stepped pointer back through a live object, so an
    // address that computes correctly and lowers wrongly still fails, and it
    // exits non-zero at its first wrong answer, naming the case.
    {
        String8 void_size_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(void_size_allocators); allocator_index += 1)
        {
            Arena* void_size_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena void_size_temporary = scratch_begin(void_size_conflicts, BUSTER_ARRAY_LENGTH(void_size_conflicts));
            Arena* void_size_arena = void_size_temporary.arena;
            String8 void_size_path =
                buster_test_temporary_path(void_size_arena, S8("buster-c-void-size"), string_format(void_size_arena, S8("-{u32}"), allocator_index));
            String8 void_size_command_line[] = {
                string_format(void_size_arena, S8("-fregister-allocator={S8}"), void_size_allocators[allocator_index]),
                S8("-o"),
                void_size_path,
                S8("tests/basic_c_void_size.c"),
            };
            CompilerDriverResult void_size = compiler_driver_execute_invocation(
                void_size_arena, compiler_driver_parse_arguments(void_size_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(void_size_command_line)));
            BUSTER_TEST(arguments, void_size.error == COMPILER_DRIVER_ERROR_NONE);
            if (void_size.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {void_size_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(void_size_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(void_size_temporary);
        }
    }
    // An aggregate crossing a `volatile` qualifier under every allocator.  The
    // qualified copy of a struct is a second IR type with the same layout, and
    // the frontend's value conversion only spanned the scalar kinds, so
    // `oldset = set2` with a volatile destination -- libc-test's
    // `functional/setjmp`, whose `sigset_t` is a struct -- was refused before
    // any code was generated.  The allocators are here because the store that
    // replaces the refusal is an aggregate copy whose two ends now disagree in
    // type, and each allocator materializes that copy its own way.  The
    // fixture exits non-zero at its first wrong answer, naming the case.
    {
        String8 volatile_aggregate_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(volatile_aggregate_allocators); allocator_index += 1)
        {
            Arena* volatile_aggregate_conflicts[] = {
                arguments->arena,
                c_asm_arena,
            };
            TemporalArena volatile_aggregate_temporary = scratch_begin(volatile_aggregate_conflicts, BUSTER_ARRAY_LENGTH(volatile_aggregate_conflicts));
            Arena* volatile_aggregate_arena = volatile_aggregate_temporary.arena;
            String8 volatile_aggregate_path = buster_test_temporary_path(volatile_aggregate_arena, S8("buster-c-volatile-aggregate"),
                                                                        string_format(volatile_aggregate_arena, S8("-{u32}"), allocator_index));
            String8 volatile_aggregate_command_line[] = {
                string_format(volatile_aggregate_arena, S8("-fregister-allocator={S8}"), volatile_aggregate_allocators[allocator_index]),
                S8("-o"),
                volatile_aggregate_path,
                S8("tests/basic_c_volatile_aggregate.c"),
            };
            CompilerDriverResult volatile_aggregate = compiler_driver_execute_invocation(
                volatile_aggregate_arena,
                compiler_driver_parse_arguments(volatile_aggregate_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(volatile_aggregate_command_line)));
            BUSTER_TEST(arguments, volatile_aggregate.error == COMPILER_DRIVER_ERROR_NONE);
            if (volatile_aggregate.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run_arguments[] = {volatile_aggregate_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){.use_process_environment = true});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(volatile_aggregate_arena, spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
            scratch_end(volatile_aggregate_temporary);
        }
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
    // A module-level assembly block that is the image's entry point, which is
    // what a libc's startup object is. Linked with `-e`, so the label the
    // block defines is the entry rather than something startup code reaches,
    // and executed: the block aligns the stack, takes a PC-relative address
    // and calls into C, and the C function returns a nonzero status for
    // whichever of the three did not survive encoding.
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    {
        String8 entry_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-global-asm-entry"), S8(""));
        String8 entry_command_line[] = {
            S8("-e"), S8("global_asm_entry"), S8("-o"), entry_path, S8("tests/basic_c_global_asm_entry.c"),
        };
        CompilerDriverResult entry = compiler_driver_execute_invocation(
            c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(entry_command_line)));
        BUSTER_TEST(arguments, entry.error == COMPILER_DRIVER_ERROR_NONE);
        if (entry.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 entry_arguments[] = {entry_path};
            ProcessSpawnResult entry_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(entry_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
            BUSTER_TEST(arguments, entry_spawn.handle != 0);
            if (entry_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(c_asm_arena, entry_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
#endif
    // The same block cross-compiled: the AArch64 arm is the call relocation
    // alone, because the textual assembler's AArch64 vocabulary cannot spell
    // the rest of an entry point.
    {
        String8 entry_aarch64_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-global-asm-entry-aarch64"), S8(".o"));
        String8 entry_aarch64_command_line[] = {
            S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), entry_aarch64_path, S8("tests/basic_c_global_asm_entry.c"),
        };
        CompilerDriverResult entry_aarch64 = compiler_driver_execute_invocation(
            c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(entry_aarch64_command_line)));
        BUSTER_TEST(arguments, entry_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
        if (entry_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ObjectSymbol const* entry_symbol = compiler_driver_test_object_symbol(&entry_aarch64.object, S8("global_asm_entry"));
            BUSTER_TEST(arguments, entry_symbol && entry_symbol->global && entry_symbol->section == OBJECT_SECTION_TEXT);
            BUSTER_TEST(arguments, compiler_driver_test_object_relocates(&entry_aarch64.object, S8("global_asm_entry_c"),
                                                                         OBJECT_RELOCATION_AARCH64_CALL26));
        }
    }
    // musl's crt_arch.h shape: `.weak` and `.hidden` on a symbol nothing
    // defines, referenced PC-relatively. Compiled, and then linked and run
    // where the host can, because the two halves fail differently. The object
    // is what a linker reads -- weak binding, hidden visibility, a relocation
    // into the instruction that names the symbol -- and the image is what the
    // linker did with it: an undefined weak reference resolves to zero, so
    // the program that reads it exits zero, and the image stays the static
    // one a startup object belongs to rather than acquiring a loader.
#if BUSTER_CPU_ARCH_X86_64
    {
        String8 weak_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-global-asm-weak"), S8(".o"));
        String8 weak_command_line[] = {
            S8("-c"), S8("-o"), weak_path, S8("tests/basic_c_global_asm_weak.c"),
        };
        CompilerDriverResult weak = compiler_driver_execute_invocation(
            c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_command_line)));
        BUSTER_TEST(arguments, weak.error == COMPILER_DRIVER_ERROR_NONE);
        if (weak.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ObjectSymbol const* dynamic_symbol = compiler_driver_test_object_symbol(&weak.object, S8("global_asm_weak_dynamic"));
            BUSTER_TEST(arguments, dynamic_symbol && dynamic_symbol->weak && dynamic_symbol->hidden && dynamic_symbol->global &&
                                       dynamic_symbol->section == OBJECT_SECTION_UNDEFINED);
            ObjectSymbol const* start_symbol = compiler_driver_test_object_symbol(&weak.object, S8("global_asm_weak_start"));
            BUSTER_TEST(arguments, start_symbol && start_symbol->global && !start_symbol->weak && start_symbol->section == OBJECT_SECTION_TEXT);
            BUSTER_TEST(arguments, compiler_driver_test_object_relocates(&weak.object, S8("global_asm_weak_dynamic"), OBJECT_RELOCATION_X86_64_PC32));
            BUSTER_TEST(arguments, compiler_driver_test_object_relocates(&weak.object, S8("global_asm_weak_start_c"), OBJECT_RELOCATION_X86_64_PC32));
        }
    }
#endif
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    // Every allocator, because the status the program exits with is computed
    // in C from the address the assembly handed it, and each allocator places
    // that argument differently.
    {
        String8 weak_link_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(weak_link_allocators); allocator_index += 1)
        {
            String8 weak_link_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-global-asm-weak-link"),
                                                                string_format(c_asm_arena, S8("-{u32}"), allocator_index));
            String8 weak_link_command_line[] = {
                string_format(c_asm_arena, S8("-fregister-allocator={S8}"), weak_link_allocators[allocator_index]),
                S8("-e"),
                S8("global_asm_weak_start"),
                S8("-o"),
                weak_link_path,
                S8("tests/basic_c_global_asm_weak.c"),
            };
            CompilerDriverResult weak_link = compiler_driver_execute_invocation(
                c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_link_command_line)));
            BUSTER_TEST(arguments, weak_link.error == COMPILER_DRIVER_ERROR_NONE);
            if (weak_link.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 weak_link_arguments[] = {weak_link_path};
                ProcessSpawnResult weak_link_spawn =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(weak_link_arguments), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){
                                         .use_process_environment = true,
                                     });
                BUSTER_TEST(arguments, weak_link_spawn.handle != 0);
                if (weak_link_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(c_asm_arena, weak_link_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
    }
    // The same two questions asked of an inline template rather than a
    // module-level block: a symbol named inside a function body, which is what
    // musl's GETFUNCSYM and CRTJMP are. The object is what a linker reads -- a
    // PC-relative relocation out of the template, and the weak hidden binding
    // a `.weak`/`.hidden` pair inside the template asked for -- and the image
    // is what the linker did with it, because a reference that lands one
    // instruction off still assembles and hands back an address that is merely
    // wrong. The program is run under every allocator: the addresses the
    // templates produce are checked in C, and each allocator places the
    // operands that carry them differently.
#if BUSTER_CPU_ARCH_X86_64
    {
        String8 inline_symbol_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-inline-asm-symbol"), S8(".o"));
        String8 inline_symbol_command_line[] = {
            S8("-c"), S8("-g0"), S8("-o"), inline_symbol_path, S8("tests/basic_c_inline_asm_symbol.c"),
        };
        CompilerDriverResult inline_symbol = compiler_driver_execute_invocation(
            c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(inline_symbol_command_line)));
        BUSTER_TEST(arguments, inline_symbol.error == COMPILER_DRIVER_ERROR_NONE);
        if (inline_symbol.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ObjectSymbol const* absent_symbol = compiler_driver_test_object_symbol(&inline_symbol.object, S8("inline_asm_symbol_absent"));
            BUSTER_TEST(arguments, absent_symbol && absent_symbol->weak && absent_symbol->hidden && absent_symbol->global &&
                                       absent_symbol->section == OBJECT_SECTION_UNDEFINED);
            BUSTER_TEST(arguments, compiler_driver_test_object_relocates(&inline_symbol.object, S8("inline_asm_symbol_absent"),
                                                                        OBJECT_RELOCATION_X86_64_PC32));
            BUSTER_TEST(arguments, compiler_driver_test_object_relocates(&inline_symbol.object, S8("inline_asm_symbol_answer"),
                                                                        OBJECT_RELOCATION_X86_64_PC32));
            BUSTER_TEST(arguments, compiler_driver_test_object_relocates(&inline_symbol.object, S8("inline_asm_symbol_cell"),
                                                                        OBJECT_RELOCATION_X86_64_PC32));
        }
    }
#endif
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    {
        String8 inline_symbol_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(inline_symbol_allocators); allocator_index += 1)
        {
            String8 inline_symbol_run_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-inline-asm-symbol-run"),
                                                                        string_format(c_asm_arena, S8("-{u32}"), allocator_index));
            String8 inline_symbol_run_command_line[] = {
                string_format(c_asm_arena, S8("-fregister-allocator={S8}"), inline_symbol_allocators[allocator_index]),
                S8("-o"),
                inline_symbol_run_path,
                S8("tests/basic_c_inline_asm_symbol.c"),
            };
            CompilerDriverResult inline_symbol_run = compiler_driver_execute_invocation(
                c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(inline_symbol_run_command_line)));
            BUSTER_TEST(arguments, inline_symbol_run.error == COMPILER_DRIVER_ERROR_NONE);
            if (inline_symbol_run.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 inline_symbol_run_arguments[] = {inline_symbol_run_path};
                ProcessSpawnResult inline_symbol_spawn =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(inline_symbol_run_arguments), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){
                                         .use_process_environment = true,
                                     });
                BUSTER_TEST(arguments, inline_symbol_spawn.handle != 0);
                if (inline_symbol_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(c_asm_arena, inline_symbol_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
    }
#endif
    // The two register files an inline-assembly operand may name besides the
    // general registers: the SSE class musl's own x86-64 sqrt, fabs and lrint
    // are written in, and the x87 stack its `long double` math is. All four
    // fixtures are programs rather than compilations: an operand carried into
    // the wrong register, or pushed into the wrong stack position, still
    // assembles and still hands back a number, so the answers are checked --
    // and `remquol`'s quotient, which is decoded out of the x87 status word, is
    // the one that a plausible remainder would otherwise hide. Each runs under
    // every allocator because the operand's frame slot is placed differently by
    // each.
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    {
        String8 sse_operand_fixtures[] = {
            S8("tests/basic_c_asm_sse_output.c"),
            S8("tests/basic_c_asm_sse_input.c"),
            S8("tests/basic_c_asm_x87_output.c"),
            S8("tests/basic_c_asm_x87_clobber.c"),
            S8("tests/basic_c_asm_x87_control_word.c"),
        };
        String8 sse_operand_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(sse_operand_fixtures); fixture_index += 1)
        {
            for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(sse_operand_allocators); allocator_index += 1)
            {
                String8 sse_operand_path =
                    buster_test_temporary_path(c_asm_arena, S8("buster-c-asm-sse-operand"),
                                               string_format(c_asm_arena, S8("-{u32}-{u32}"), fixture_index, allocator_index));
                String8 sse_operand_command_line[] = {
                    string_format(c_asm_arena, S8("-fregister-allocator={S8}"), sse_operand_allocators[allocator_index]),
                    S8("-o"),
                    sse_operand_path,
                    sse_operand_fixtures[fixture_index],
                };
                CompilerDriverResult sse_operand = compiler_driver_execute_invocation(
                    c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sse_operand_command_line)));
                BUSTER_TEST(arguments, sse_operand.error == COMPILER_DRIVER_ERROR_NONE);
                if (sse_operand.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 sse_operand_arguments[] = {sse_operand_path};
                    ProcessSpawnResult sse_operand_spawn =
                        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(sse_operand_arguments), (SliceString8){0}, (SliceString8){0},
                                         (ProcessSpawnOptions){
                                             .use_process_environment = true,
                                         });
                    BUSTER_TEST(arguments, sse_operand_spawn.handle != 0);
                    if (sse_operand_spawn.handle)
                    {
                        BUSTER_TEST(arguments, os_process_wait_sync(c_asm_arena, sse_operand_spawn).result == PROCESS_RESULT_SUCCESS);
                    }
                }
            }
        }
    }
#endif
    // The hosted half of the same question. `__attribute__((weak))` on a
    // declaration must reach the object as STB_WEAK, and in an image that has
    // a shared library the three references part company: the one libc
    // defines still has to reach it, the two nothing defines must not fail
    // the load, and the hidden one is still owed zero.
    {
        String8 weak_undefined_object_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-weak-undefined"), S8(".o"));
        String8 weak_undefined_object_command_line[] = {
            S8("-c"), S8("-g0"), S8("-o"), weak_undefined_object_path, S8("tests/basic_c_weak_undefined.c"),
        };
        CompilerDriverResult weak_undefined_object = compiler_driver_execute_invocation(
            c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_undefined_object_command_line)));
        BUSTER_TEST(arguments, weak_undefined_object.error == COMPILER_DRIVER_ERROR_NONE);
        if (weak_undefined_object.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 weak_undefined_names[] = {S8("puts"), S8("weak_undefined_object"), S8("weak_undefined_function")};
            for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(weak_undefined_names); name_index += 1)
            {
                ObjectSymbol const* weak_undefined_symbol =
                    compiler_driver_test_object_symbol(&weak_undefined_object.object, weak_undefined_names[name_index]);
                BUSTER_TEST(arguments, weak_undefined_symbol && weak_undefined_symbol->weak && weak_undefined_symbol->global &&
                                           !weak_undefined_symbol->hidden && weak_undefined_symbol->section == OBJECT_SECTION_UNDEFINED);
            }
            ObjectSymbol const* weak_undefined_hidden =
                compiler_driver_test_object_symbol(&weak_undefined_object.object, S8("weak_undefined_hidden"));
            BUSTER_TEST(arguments, weak_undefined_hidden && weak_undefined_hidden->weak && weak_undefined_hidden->hidden &&
                                       weak_undefined_hidden->section == OBJECT_SECTION_UNDEFINED);
        }
        String8 weak_undefined_allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(weak_undefined_allocators); allocator_index += 1)
        {
            String8 weak_undefined_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-weak-undefined-link"),
                                                                     string_format(c_asm_arena, S8("-{u32}"), allocator_index));
            String8 weak_undefined_command_line[] = {
                string_format(c_asm_arena, S8("-fregister-allocator={S8}"), weak_undefined_allocators[allocator_index]),
                S8("-o"),
                weak_undefined_path,
                S8("tests/basic_c_weak_undefined.c"),
            };
            CompilerDriverResult weak_undefined = compiler_driver_execute_invocation(
                c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_undefined_command_line)));
            BUSTER_TEST(arguments, weak_undefined.error == COMPILER_DRIVER_ERROR_NONE);
            if (weak_undefined.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 weak_undefined_arguments[] = {weak_undefined_path};
                ProcessSpawnResult weak_undefined_spawn =
                    os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(weak_undefined_arguments), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){
                                         .use_process_environment = true,
                                     });
                BUSTER_TEST(arguments, weak_undefined_spawn.handle != 0);
                if (weak_undefined_spawn.handle)
                {
                    BUSTER_TEST(arguments, os_process_wait_sync(c_asm_arena, weak_undefined_spawn).result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
    }
#endif
    scratch_end(c_asm_temporary);
#endif
    // Assembly as driver input. The fixture is one complete translation unit
    // -- two sections, the symbol and data directives, local numeric labels
    // resolved forward and backward -- so the object is checked for what a
    // linker reads and, where the host can run it, the program is checked for
    // what the assembler actually encoded.
    {
        TemporalArena asm_unit_temporary = scratch_begin(&arguments->arena, 1);
        Arena* asm_unit_arena = asm_unit_temporary.arena;
        String8 asm_unit_object_path = buster_test_temporary_path(asm_unit_arena, S8("buster-asm-unit"), S8(".o"));
        // The fixture is x86-64 assembly, so the object half is asked for
        // that target explicitly rather than for the host's: it is then the
        // same check on every platform in the matrix, and only the half that
        // runs the program depends on where the test is running.
        String8 asm_unit_command_line[] = {
            S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-o"), asm_unit_object_path, S8("tests/basic_asm_unit.s"),
        };
        CompilerDriverResult asm_unit = compiler_driver_execute_invocation(
            asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(asm_unit_command_line)));
        BUSTER_TEST(arguments, asm_unit.error == COMPILER_DRIVER_ERROR_NONE);
        if (asm_unit.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, asm_unit.has_object);
            // `.section .rodata` and `.text`, each under its own name: the
            // object keeps the file's section names rather than a fixed
            // kind-indexed table, which is what `.init` in a crt depends on.
            ObjectSection const* asm_unit_text = 0;
            ObjectSection const* asm_unit_rodata = 0;
            u32 asm_unit_text_section = OBJECT_SECTION_UNDEFINED;
            u32 asm_unit_rodata_section = OBJECT_SECTION_UNDEFINED;
            for (u32 section_index = 0; section_index < asm_unit.object.section_count; section_index += 1)
            {
                ObjectSection* section = asm_unit.object.sections + section_index;
                bool text = string_equal(section->name, S8(".text"));
                bool rodata = string_equal(section->name, S8(".rodata"));
                asm_unit_text = text ? section : asm_unit_text;
                asm_unit_rodata = rodata ? section : asm_unit_rodata;
                asm_unit_text_section = text ? section_index : asm_unit_text_section;
                asm_unit_rodata_section = rodata ? section_index : asm_unit_rodata_section;
            }
            BUSTER_TEST(arguments, asm_unit_text && asm_unit_text->kind == OBJECT_SECTION_TEXT && asm_unit_text->data.length != 0);
            BUSTER_TEST(arguments, asm_unit_rodata && asm_unit_rodata->kind == OBJECT_SECTION_READ_ONLY_DATA && asm_unit_rodata->alignment == 8);
            // `.long 3`, `.long 5`, `.quad`, `.ascii "buster"`, `.byte 0` and
            // `.short 1`: 25 bytes, and `.size` computed from `.-symbol` says
            // the same.
            ObjectSymbol const* asm_unit_table = compiler_driver_test_object_symbol(&asm_unit.object, S8("basic_asm_unit_table"));
            BUSTER_TEST(arguments, asm_unit_table && asm_unit_table->global && !asm_unit_table->hidden &&
                                       asm_unit_table->kind == OBJECT_SYMBOL_DATA && asm_unit_table->size == 25);
            ObjectSymbol const* asm_unit_helper = compiler_driver_test_object_symbol(&asm_unit.object, S8("basic_asm_unit_helper"));
            BUSTER_TEST(arguments, asm_unit_helper && asm_unit_helper->global && asm_unit_helper->hidden &&
                                       asm_unit_helper->kind == OBJECT_SYMBOL_FUNCTION && asm_unit_helper->size == 3);
            ObjectSymbol const* asm_unit_weak = compiler_driver_test_object_symbol(&asm_unit.object, S8("basic_asm_unit_weak"));
            BUSTER_TEST(arguments, asm_unit_weak && asm_unit_weak->weak && asm_unit_weak->hidden && asm_unit_weak->global &&
                                       asm_unit_weak->section == OBJECT_SECTION_UNDEFINED);
            // A local label is assembler bookkeeping and leaves no name
            // behind, so the `1:`..`4:` in the fixture are not in the table.
            for (u32 symbol_index = 0; symbol_index < asm_unit.object.symbol_count; symbol_index += 1)
            {
                BUSTER_TEST(arguments, !string_starts_with_sequence(asm_unit.object.symbols[symbol_index].name, S8(".L")));
            }
            // `.quad basic_asm_unit_helper` is an absolute relocation the
            // linker resolves; the two cross-section reads in the code are
            // PC-relative ones. Every reference to a label in its own section
            // is already folded into the bytes, so it is not here.
            // The section a relocation sits in is an index into this object's
            // own sparse table, not the kind-indexed one a compiled module
            // has, so the sections found above are what they are checked
            // against.
            bool asm_unit_absolute = false;
            bool asm_unit_pointer_reference = false;
            bool asm_unit_table_reference = false;
            for (u32 relocation_index = 0; relocation_index < asm_unit.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocation relocation = asm_unit.object.relocations[relocation_index];
                if (relocation.symbol >= asm_unit.object.symbol_count)
                {
                    continue;
                }
                String8 relocated = asm_unit.object.symbols[relocation.symbol].name;
                asm_unit_absolute = asm_unit_absolute || (relocation.section == asm_unit_rodata_section &&
                                                          relocation.kind == OBJECT_RELOCATION_ABSOLUTE64 &&
                                                          string_equal(relocated, S8("basic_asm_unit_helper")));
                asm_unit_pointer_reference = asm_unit_pointer_reference || (relocation.section == asm_unit_text_section &&
                                                                            relocation.kind == OBJECT_RELOCATION_X86_64_PC32 &&
                                                                            string_equal(relocated, S8("basic_asm_unit_pointer")));
                asm_unit_table_reference = asm_unit_table_reference || (relocation.section == asm_unit_text_section &&
                                                                        relocation.kind == OBJECT_RELOCATION_X86_64_PC32 &&
                                                                        string_equal(relocated, S8("basic_asm_unit_table")));
            }
            BUSTER_TEST(arguments, asm_unit_absolute && asm_unit_pointer_reference && asm_unit_table_reference);
        }
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        String8 asm_unit_program_path = buster_test_temporary_path(asm_unit_arena, S8("buster-asm-unit-program"), S8(""));
        String8 asm_unit_link_command_line[] = {
            S8("-e"), S8("basic_asm_unit_start"), S8("-o"), asm_unit_program_path, S8("tests/basic_asm_unit.s"),
        };
        CompilerDriverResult asm_unit_link = compiler_driver_execute_invocation(
            asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(asm_unit_link_command_line)));
        BUSTER_TEST(arguments, asm_unit_link.error == COMPILER_DRIVER_ERROR_NONE);
        if (asm_unit_link.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 asm_unit_program_arguments[] = {asm_unit_program_path};
            ProcessSpawnResult asm_unit_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(asm_unit_program_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, asm_unit_spawn.handle != 0);
            if (asm_unit_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(asm_unit_arena, asm_unit_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        // The same unit linked against a C one. A compiled module carries one
        // section per kind and an assembled one carries only the sections its
        // file named, so this is what proves the merge reads a section's kind
        // rather than its position.
        String8 asm_unit_mixed_path = buster_test_temporary_path(asm_unit_arena, S8("buster-asm-unit-mixed"), S8(""));
        String8 asm_unit_mixed_command_line[] = {
            S8("-o"), asm_unit_mixed_path, S8("tests/basic_asm_unit_caller.c"), S8("tests/basic_asm_unit.s"),
        };
        CompilerDriverResult asm_unit_mixed = compiler_driver_execute_invocation(
            asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(asm_unit_mixed_command_line)));
        BUSTER_TEST(arguments, asm_unit_mixed.error == COMPILER_DRIVER_ERROR_NONE);
        if (asm_unit_mixed.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 asm_unit_mixed_arguments[] = {asm_unit_mixed_path};
            ProcessSpawnResult asm_unit_mixed_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(asm_unit_mixed_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, asm_unit_mixed_spawn.handle != 0);
            if (asm_unit_mixed_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(asm_unit_arena, asm_unit_mixed_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        // A `.long symbol` in an assembled unit is R_X86_64_32, the
        // zero-extended absolute form -fno-pic small-model foreign objects
        // use for every address literal.  The linked program reads a
        // function's address back out of the slot and calls it.
        String8 abs32_path = buster_test_temporary_path(asm_unit_arena, S8("buster-asm-abs32"), S8(""));
        String8 abs32_command_line[] = {
            S8("-o"), abs32_path, S8("tests/basic_c_abs32_caller.c"), S8("tests/basic_asm_abs32_anchor.s"),
        };
        CompilerDriverResult abs32 = compiler_driver_execute_invocation(
            asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(abs32_command_line)));
        BUSTER_TEST(arguments, abs32.error == COMPILER_DRIVER_ERROR_NONE);
        if (abs32.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 abs32_arguments[] = {abs32_path};
            ProcessSpawnResult abs32_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(abs32_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, abs32_spawn.handle != 0);
            if (abs32_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(asm_unit_arena, abs32_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        // -rdynamic: the program's own globals land in .dynsym, and
        // dlopen(NULL)+dlsym finds them -- ctypes.pythonapi's whole
        // mechanism.  The fixture exits nonzero when the lookup fails.
        String8 export_dynamic_path = buster_test_temporary_path(asm_unit_arena, S8("buster-c-export-dynamic"), S8(""));
        String8 export_dynamic_command_line[] = {
            S8("-rdynamic"), S8("-o"), export_dynamic_path, S8("tests/basic_c_export_dynamic.c"), S8("-ldl"),
        };
        CompilerDriverResult export_dynamic = compiler_driver_execute_invocation(
            asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(export_dynamic_command_line)));
        BUSTER_TEST(arguments, export_dynamic.error == COMPILER_DRIVER_ERROR_NONE);
        if (export_dynamic.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 export_dynamic_arguments[] = {export_dynamic_path};
            ProcessSpawnResult export_dynamic_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(export_dynamic_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, export_dynamic_spawn.handle != 0);
            if (export_dynamic_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(asm_unit_arena, export_dynamic_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
#endif
        // Driver options follow last-option-wins: an allocator named AFTER
        // the -O flag decides the emitter, and the two objects must differ.
        // (The reverse order deliberately restores the default -- the
        // parse-level contract earlier in this file pins that -- which is
        // why the CPython harness carries its allocator in CFLAGS, after
        // configure's own -O3.)
        {
            String8 sticky_none_path = buster_test_temporary_path(asm_unit_arena, S8("buster-c-allocator-sticky-none"), S8(".o"));
            String8 sticky_fast_path = buster_test_temporary_path(asm_unit_arena, S8("buster-c-allocator-sticky-fast"), S8(".o"));
            String8 sticky_none_command_line[] = {
                S8("-O2"), S8("-fregister-allocator=none"), S8("-c"), S8("-o"), sticky_none_path, S8("tests/basic_c_explicit_allocator_sticks.c"),
            };
            String8 sticky_fast_command_line[] = {
                S8("-O2"), S8("-fregister-allocator=fast"), S8("-c"), S8("-o"), sticky_fast_path, S8("tests/basic_c_explicit_allocator_sticks.c"),
            };
            CompilerDriverResult sticky_none = compiler_driver_execute_invocation(
                asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sticky_none_command_line)));
            CompilerDriverResult sticky_fast = compiler_driver_execute_invocation(
                asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sticky_fast_command_line)));
            BUSTER_TEST(arguments, sticky_none.error == COMPILER_DRIVER_ERROR_NONE && sticky_fast.error == COMPILER_DRIVER_ERROR_NONE);
            if (sticky_none.error == COMPILER_DRIVER_ERROR_NONE && sticky_fast.error == COMPILER_DRIVER_ERROR_NONE)
            {
                ByteSlice none_bytes = file_read(asm_unit_arena, sticky_none_path, (FileReadOptions){0});
                ByteSlice fast_bytes = file_read(asm_unit_arena, sticky_fast_path, (FileReadOptions){0});
                bool identical = none_bytes.length == fast_bytes.length && none_bytes.length &&
                                 memcmp(none_bytes.pointer, fast_bytes.pointer, none_bytes.length) == 0;
                BUSTER_TEST(arguments, none_bytes.length && fast_bytes.length && !identical);
            }
        }
        // A directive the vocabulary does not cover is refused by name and by
        // line, rather than dropped from an object that then quietly lacks
        // whatever it was there to do.
        String8 unsupported_path = buster_test_temporary_path(asm_unit_arena, S8("buster-asm-unsupported"), S8(".o"));
        String8 unsupported_command_line[] = {
            S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-o"), unsupported_path, S8("tests/basic_asm_unsupported.s"),
        };
        CompilerDriverResult unsupported = compiler_driver_execute_invocation(
            asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unsupported_command_line)));
        BUSTER_TEST(arguments, unsupported.error != COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, string_first_sequence(unsupported.diagnostic, S8(".subsection")) < unsupported.diagnostic.length);
        BUSTER_TEST(arguments, string_first_sequence(unsupported.diagnostic, S8(":9:")) < unsupported.diagnostic.length);
        // A `.S` is asm_unit with the C preprocessor in front of it: the
        // faithful -E text preserves the assembly spellings, `#` commentary
        // reads as GNU-as commentary, and the `$` immediate prefix splits
        // off the macro name it would otherwise glue shut.  The fixture's
        // status comes from a macro, so an unexpanded name fails to
        // assemble rather than encoding the wrong immediate.
        String8 preprocessed_path = buster_test_temporary_path(asm_unit_arena, S8("buster-asm-preprocessed"), S8(".o"));
        String8 preprocessed_command_line[] = {
            S8("-c"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-o"), preprocessed_path, S8("tests/basic_asm_preprocessed.S"),
        };
        CompilerDriverResult preprocessed = compiler_driver_execute_invocation(
            asm_unit_arena, compiler_driver_parse_arguments(asm_unit_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(preprocessed_command_line)));
        BUSTER_TEST(arguments, preprocessed.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, preprocessed.has_object);
        scratch_end(asm_unit_temporary);
    }
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
