#include <buster/compiler/object/object.h>

#include <buster/integer.h>
#include <buster/os.h>
#include <buster/string.h>

typedef struct ObjectBuffer ObjectBuffer;
struct ObjectBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    ObjectError error;
};

BUSTER_GLOBAL_LOCAL void object_buffer_write(ObjectBuffer* buffer, void const* source, u64 size)
{
    if (buffer->error != OBJECT_ERROR_NONE)
    {
        return;
    }
    if (!size)
    {
        return;
    }
    if (size > buffer->capacity - buffer->count)
    {
        buffer->error = OBJECT_ERROR_CAPACITY;
        return;
    }
    memcpy(buffer->bytes + buffer->count, source, size);
    buffer->count += size;
}

BUSTER_GLOBAL_LOCAL void object_buffer_zero(ObjectBuffer* buffer, u64 size)
{
    if (buffer->error != OBJECT_ERROR_NONE)
    {
        return;
    }
    if (!size)
    {
        return;
    }
    if (size > buffer->capacity - buffer->count)
    {
        buffer->error = OBJECT_ERROR_CAPACITY;
        return;
    }
    memset(buffer->bytes + buffer->count, 0, size);
    buffer->count += size;
}

BUSTER_GLOBAL_LOCAL void object_buffer_align(ObjectBuffer* buffer, u64 alignment)
{
    if (!alignment)
    {
        alignment = 1;
    }
    u64 aligned = (buffer->count + alignment - 1) & ~(alignment - 1);
    object_buffer_zero(buffer, aligned - buffer->count);
}

BUSTER_GLOBAL_LOCAL void object_write_u16_at(ObjectBuffer* buffer, u64 offset, u16 value)
{
    if (offset + sizeof(value) > buffer->capacity)
    {
        buffer->error = OBJECT_ERROR_CAPACITY;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void object_write_u32_at(ObjectBuffer* buffer, u64 offset, u32 value)
{
    if (offset + sizeof(value) > buffer->capacity)
    {
        buffer->error = OBJECT_ERROR_CAPACITY;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void object_write_u64_at(ObjectBuffer* buffer, u64 offset, u64 value)
{
    if (offset + sizeof(value) > buffer->capacity)
    {
        buffer->error = OBJECT_ERROR_CAPACITY;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void object_write_s64_at(ObjectBuffer* buffer, u64 offset, s64 value)
{
    object_write_u64_at(buffer, offset, (u64)value);
}

BUSTER_GLOBAL_LOCAL u64 object_writer_capacity(ObjectFile* object)
{
    u64 result = 16384;
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        result += object->sections[section].data.length + 256;
    }
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        result += object->symbols[symbol].name.length + 128;
    }
    result += (u64)object->relocation_count * 64;
    return result;
}

ObjectFormat object_format_for_target(Target target)
{
    switch (target.os)
    {
    case OPERATING_SYSTEM_WINDOWS:
    case OPERATING_SYSTEM_UEFI:
        return OBJECT_FORMAT_COFF;
    case OPERATING_SYSTEM_MACOS:
    case OPERATING_SYSTEM_IOS:
        return OBJECT_FORMAT_MACH_O64;
    case OPERATING_SYSTEM_LINUX:
    case OPERATING_SYSTEM_ANDROID:
    case OPERATING_SYSTEM_FREESTANDING:
        return OBJECT_FORMAT_ELF64;
    default:
        return OBJECT_FORMAT_COUNT;
    }
}

typedef struct ObjectAssemblyBuffer ObjectAssemblyBuffer;
struct ObjectAssemblyBuffer
{
    char8* bytes;
    u64 count;
    u64 capacity;
    Arena* arena;
    u8* internal_labels;
    u64 internal_label_count;
    u32 internal_label_section;
    bool error;
};

BUSTER_GLOBAL_LOCAL void object_assembly_append(ObjectAssemblyBuffer* buffer, const char8* bytes, u64 count)
{
    if (buffer->error)
    {
        return;
    }
    if (count > buffer->capacity - buffer->count)
    {
        buffer->error = true;
        return;
    }
    if (count)
    {
        memcpy(buffer->bytes + buffer->count, bytes, count);
        buffer->count += count;
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_string(ObjectAssemblyBuffer* buffer, String8 string)
{
    object_assembly_append(buffer, string.pointer, string.length);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_u64_decimal(ObjectAssemblyBuffer* buffer, u64 value)
{
    char8 digits[32];
    u32 digit_count = 0;
    do
    {
        digits[digit_count++] = (char8)('0' + value % 10);
        value /= 10;
    } while (value);
    while (digit_count)
    {
        object_assembly_append(buffer, digits + --digit_count, 1);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_register_width(ObjectAssemblyBuffer* buffer, u32 register_index, bool wide)
{
    object_assembly_append_string(buffer, wide ? S8("x") : S8("w"));
    object_assembly_append_u64_decimal(buffer, register_index & 31);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_register(ObjectAssemblyBuffer* buffer, u32 register_index)
{
    object_assembly_append_aarch64_register_width(buffer, register_index, true);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_u64_hex(ObjectAssemblyBuffer* buffer, u64 value, u32 digit_count)
{
    char8 digits[] = "0123456789abcdef";
    for (u32 index = 0; index < digit_count; index += 1)
    {
        u32 shift = (digit_count - index - 1) * 4;
        object_assembly_append(buffer, digits + ((value >> shift) & 0xf), 1);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_number(ObjectAssemblyBuffer* buffer, s64 value)
{
    if (value < 0)
    {
        object_assembly_append_string(buffer, S8("-0x"));
        u64 magnitude = (u64)(-(value + 1)) + 1;
        u32 digit_count = 1;
        while (digit_count < 16 && (magnitude >> (digit_count * 4)))
        {
            digit_count += 1;
        }
        object_assembly_append_u64_hex(buffer, magnitude, digit_count);
    }
    else
    {
        object_assembly_append_string(buffer, S8("0x"));
        u64 unsigned_value = (u64)value;
        u32 digit_count = 1;
        while (digit_count < 16 && (unsigned_value >> (digit_count * 4)))
        {
            digit_count += 1;
        }
        object_assembly_append_u64_hex(buffer, unsigned_value, digit_count);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_internal_label(ObjectAssemblyBuffer* buffer, u32 section, u64 offset)
{
    object_assembly_append_string(buffer, S8(".Lbuster_"));
    object_assembly_append_u64_decimal(buffer, section);
    object_assembly_append_string(buffer, S8("_"));
    object_assembly_append_u64_decimal(buffer, offset);
}

BUSTER_GLOBAL_LOCAL bool object_assembly_has_internal_label(ObjectAssemblyBuffer* buffer, u32 section, u64 offset)
{
    return buffer->internal_labels && buffer->internal_label_section == section && offset < buffer->internal_label_count &&
           buffer->internal_labels[offset];
}

BUSTER_GLOBAL_LOCAL void object_assembly_emit_internal_label(ObjectAssemblyBuffer* buffer, u32 section, u64 offset)
{
    if (object_assembly_has_internal_label(buffer, section, offset))
    {
        object_assembly_append_internal_label(buffer, section, offset);
        object_assembly_append_string(buffer, S8(":\n"));
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_s64_addend(ObjectAssemblyBuffer* buffer, s64 value)
{
    if (!value)
    {
        return;
    }
    u64 magnitude = value < 0 ? (u64)(-(value + 1)) + 1 : (u64)value;
    object_assembly_append_string(buffer, value < 0 ? S8(" - ") : S8(" + "));
    object_assembly_append_u64_decimal(buffer, magnitude);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_assembly_symbol(ObjectAssemblyBuffer* buffer, Target target, String8 name)
{
    if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
    {
        object_assembly_append_string(buffer, S8("_"));
    }
    object_assembly_append_string(buffer, name);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_symbol_reference(ObjectAssemblyBuffer* buffer, Target target, String8 name)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        object_assembly_append_string(buffer, S8("\""));
    }
    object_assembly_append_assembly_symbol(buffer, target, name);
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        object_assembly_append_string(buffer, S8("\""));
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_hex_byte(ObjectAssemblyBuffer* buffer, u8 value)
{
    object_assembly_append_string(buffer, S8("0x"));
    object_assembly_append_u64_hex(buffer, value, 2);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_byte_range(ObjectAssemblyBuffer* buffer, ByteSlice bytes, u64 start, u64 end)
{
    while (start < end && !buffer->error)
    {
        u64 line_end = BUSTER_MIN(end, start + 16);
        object_assembly_append_string(buffer, S8("\t.byte "));
        for (u64 index = start; index < line_end; index += 1)
        {
            if (index != start)
            {
                object_assembly_append_string(buffer, S8(", "));
            }
            object_assembly_append_hex_byte(buffer, bytes.pointer[index]);
        }
        object_assembly_append_string(buffer, S8("\n"));
        start = line_end;
    }
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_emit_x86_raw_instruction(ObjectAssemblyBuffer* buffer, ByteSlice data, u64 offset, u64 instruction_length)
{
    if (!instruction_length || offset > data.length || instruction_length > data.length - offset)
    {
        return 0;
    }
    object_assembly_append_byte_range(buffer, data, offset, offset + instruction_length);
    return instruction_length;
}

BUSTER_GLOBAL_LOCAL u32 object_assembly_alignment_exponent(u32 alignment)
{
    u32 exponent = 0;
    while (alignment > 1)
    {
        alignment >>= 1;
        exponent += 1;
    }
    return exponent;
}

BUSTER_GLOBAL_LOCAL String8 object_assembly_section_directive(Target target, ObjectSectionKind kind)
{
    if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
    {
        switch (kind)
        {
        case OBJECT_SECTION_TEXT:
            return S8("\t.section __TEXT,__text,regular,pure_instructions\n");
        case OBJECT_SECTION_READ_ONLY_DATA:
            return S8("\t.section __TEXT,__const\n");
        case OBJECT_SECTION_DATA:
            return S8("\t.section __DATA,__data\n");
        case OBJECT_SECTION_THREAD_LOCAL_DATA:
            return S8("\t.section __DATA,__thread_data\n");
        case OBJECT_SECTION_THREAD_LOCAL_ZERO:
            return S8("\t.section __DATA,__thread_bss\n");
        case OBJECT_SECTION_COUNT:
            break;
        }
    }
    switch (kind)
    {
    case OBJECT_SECTION_TEXT:
        return S8("\t.text\n");
    case OBJECT_SECTION_READ_ONLY_DATA:
        return S8("\t.section .rodata\n");
    case OBJECT_SECTION_DATA:
        return S8("\t.section .data\n");
    case OBJECT_SECTION_THREAD_LOCAL_DATA:
        return S8("\t.section .tdata\n");
    case OBJECT_SECTION_THREAD_LOCAL_ZERO:
        return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI ? S8("\t.section .tbss\n")
                                                                                           : S8("\t.section .tbss,\"awT\",@nobits\n");
    case OBJECT_SECTION_COUNT:
        break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL u32 object_assembly_relocation_size(ObjectRelocationKind kind)
{
    return kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
}

BUSTER_GLOBAL_LOCAL bool object_assembly_is_apple(Target target)
{
    return target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS;
}

BUSTER_GLOBAL_LOCAL bool object_assembly_is_gnu_type_target(Target target)
{
    return target.os == OPERATING_SYSTEM_LINUX || target.os == OPERATING_SYSTEM_ANDROID || target.os == OPERATING_SYSTEM_FREESTANDING ||
           target.os == OPERATING_SYSTEM_UEFI;
}

BUSTER_GLOBAL_LOCAL ObjectRelocation* object_assembly_relocation_at(ObjectFile* object, u32 section, u64 offset)
{
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = object->relocations + index;
        if (relocation->section == section && relocation->offset == offset)
        {
            return relocation;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_next_relocation(ObjectFile* object, u32 section, u64 offset, u64 end)
{
    u64 result = end;
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = object->relocations + index;
        if (relocation->section == section && relocation->offset >= offset && relocation->offset < result)
        {
            result = relocation->offset;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ObjectRelocation* object_assembly_first_relocation(ObjectFile* object, u32 section, u64 offset, u64 end)
{
    u64 relocation_offset = object_assembly_next_relocation(object, section, offset, end);
    return relocation_offset < end ? object_assembly_relocation_at(object, section, relocation_offset) : 0;
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_next_symbol(ObjectFile* object, u32 section, u64 offset, u64 end)
{
    u64 result = end;
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = object->symbols + index;
        if (symbol->section == section && symbol->value > offset && symbol->value < result)
        {
            result = symbol->value;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void object_assembly_emit_labels(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, u32 section, u64 offset)
{
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = object->symbols + index;
        if (symbol->section != section || symbol->value != offset)
        {
            continue;
        }
        if (symbol->global)
        {
            object_assembly_append_string(buffer, S8("\t.globl "));
            object_assembly_append_assembly_symbol(buffer, target, symbol->name);
            object_assembly_append_string(buffer, S8("\n"));
        }
        if (object_assembly_is_gnu_type_target(target))
        {
            object_assembly_append_string(buffer, S8("\t.type "));
            object_assembly_append_assembly_symbol(buffer, target, symbol->name);
            object_assembly_append_string(buffer, symbol->kind == OBJECT_SYMBOL_FUNCTION ? S8(", @function\n") : S8(", @object\n"));
        }
        object_assembly_append_assembly_symbol(buffer, target, symbol->name);
        object_assembly_append_string(buffer, S8(":\n"));
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_emit_sizes(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, u32 section)
{
    if (!object_assembly_is_gnu_type_target(target))
    {
        return;
    }
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = object->symbols + index;
        if (symbol->section != section)
        {
            continue;
        }
        object_assembly_append_string(buffer, S8("\t.size "));
        object_assembly_append_assembly_symbol(buffer, target, symbol->name);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_u64_decimal(buffer, symbol->size);
        object_assembly_append_string(buffer, S8("\n"));
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_relocation_symbol(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, ObjectRelocation* relocation)
{
    ObjectSymbol* symbol = relocation->symbol < object->symbol_count ? object->symbols + relocation->symbol : 0;
    if (!symbol)
    {
        object_assembly_append_string(buffer, S8("0"));
        return;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        object_assembly_append_x86_symbol_reference(buffer, target, symbol->name);
    }
    else
    {
        object_assembly_append_assembly_symbol(buffer, target, symbol->name);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_relocation_value(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, ObjectRelocation* relocation,
                                                                  ByteSlice section_data)
{
    object_assembly_append_relocation_symbol(buffer, object, target, relocation);
    object_assembly_append_s64_addend(buffer, relocation->addend);
    BUSTER_UNUSED(section_data);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_relocation_value(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target,
                                                                      ObjectRelocation* relocation, String8 modifier)
{
    ObjectSymbol* symbol = relocation->symbol < object->symbol_count ? object->symbols + relocation->symbol : 0;
    if (!symbol)
    {
        object_assembly_append_string(buffer, S8("0"));
        return;
    }
    object_assembly_append_x86_symbol_reference(buffer, target, symbol->name);
    object_assembly_append_string(buffer, modifier);
    object_assembly_append_s64_addend(buffer, relocation->addend);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_relocation_value(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target,
                                                                         ObjectRelocation* relocation, String8 modifier, bool modifier_after)
{
    ObjectSymbol* symbol = relocation->symbol < object->symbol_count ? object->symbols + relocation->symbol : 0;
    if (!symbol)
    {
        object_assembly_append_string(buffer, S8("0"));
        return;
    }
    if (!modifier_after)
    {
        object_assembly_append_string(buffer, modifier);
    }
    object_assembly_append_assembly_symbol(buffer, target, symbol->name);
    if (modifier_after)
    {
        object_assembly_append_string(buffer, modifier);
    }
    object_assembly_append_s64_addend(buffer, relocation->addend);
}

BUSTER_GLOBAL_LOCAL bool object_assembly_emit_aarch64_immediate_relocation(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target,
                                                                            ObjectRelocation* relocation, ByteSlice section_data, String8 modifier,
                                                                            bool modifier_after)
{
    u32 word = 0;
    memcpy(&word, section_data.pointer + relocation->offset, sizeof(word));
    if ((word & UINT32_C(0x7f000000)) == UINT32_C(0x11000000))
    {
        object_assembly_append_string(buffer, S8("\tadd "));
        object_assembly_append_aarch64_register_width(buffer, word, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register_width(buffer, word >> 5, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_relocation_value(buffer, object, target, relocation, modifier, modifier_after);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    if ((word & UINT32_C(0x3f000000)) == UINT32_C(0x39000000))
    {
        u32 size = (word >> 30) & 3;
        bool load = (word & (UINT32_C(1) << 22)) != 0;
        object_assembly_append_string(buffer, load ? S8("\tldr ") : S8("\tstr "));
        object_assembly_append_aarch64_register_width(buffer, word, size == 3);
        object_assembly_append_string(buffer, S8(", ["));
        object_assembly_append_aarch64_register(buffer, word >> 5);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_relocation_value(buffer, object, target, relocation, modifier, modifier_after);
        object_assembly_append_string(buffer, S8("]\n"));
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool object_assembly_emit_relocation(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, ObjectRelocation* relocation,
                                                         ByteSlice section_data)
{
    if (!relocation || relocation->offset + object_assembly_relocation_size(relocation->kind) > section_data.length)
    {
        return false;
    }
    switch (relocation->kind)
    {
    case OBJECT_RELOCATION_ABSOLUTE64:
        object_assembly_append_string(buffer, S8("\t.quad "));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_X86_64_PC32:
    case OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32:
        object_assembly_append_string(buffer, S8("\t.long "));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8(" - ."));
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_X86_64_MACH_TLV_PC32:
        object_assembly_append_string(buffer, S8("\t.long "));
        object_assembly_append_x86_relocation_value(buffer, object, target, relocation, S8("@TLVP"));
        object_assembly_append_string(buffer, S8(" - .\n"));
        return true;
    case OBJECT_RELOCATION_X86_64_TPOFF32:
        object_assembly_append_string(buffer, S8("\t.long "));
        object_assembly_append_x86_relocation_value(buffer, object, target, relocation, S8("@TPOFF"));
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_PE_TLS_OFFSET32:
        object_assembly_append_string(buffer, S8("\t.long "));
        object_assembly_append_x86_relocation_value(buffer, object, target, relocation, S8("@SECREL32"));
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_AARCH64_CALL26:
    {
        object_assembly_append_string(buffer, S8("\tbl "));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    case OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21:
    case OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP:
    {
        u32 word = 0;
        memcpy(&word, section_data.pointer + relocation->offset, sizeof(word));
        object_assembly_append_string(buffer, S8("\tadrp "));
        object_assembly_append_aarch64_register(buffer, word);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_relocation_value(
            buffer, object, target, relocation,
            relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 ? S8("@TLVPPAGE") : (String8){0}, true);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    case OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12:
    case OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12:
    case OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12:
    {
        String8 modifier = relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 ? S8("@TLVPPAGEOFF")
                           : relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12          ? S8(":secrel_lo12:")
                                                                                                      : S8(":lo12:");
        return object_assembly_emit_aarch64_immediate_relocation(buffer, object, target, relocation, section_data, modifier,
                                                                 relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12);
    }
    case OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12:
    {
        u32 word = 0;
        memcpy(&word, section_data.pointer + relocation->offset, sizeof(word));
        object_assembly_append_string(buffer, S8("\tadd "));
        object_assembly_append_aarch64_register(buffer, word);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register(buffer, word >> 5);
        object_assembly_append_string(buffer, S8(", #:tprel_hi12:"));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    case OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12:
    {
        u32 word = 0;
        memcpy(&word, section_data.pointer + relocation->offset, sizeof(word));
        object_assembly_append_string(buffer, S8("\tadd "));
        object_assembly_append_aarch64_register(buffer, word);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register(buffer, word >> 5);
        object_assembly_append_string(buffer, S8(", #:tprel_lo12_nc:"));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    case OBJECT_RELOCATION_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_apple_x86_relocation_start(ObjectRelocation* relocation, ByteSlice data, u64 cursor)
{
    if (!relocation || (relocation->kind != OBJECT_RELOCATION_X86_64_PC32 && relocation->kind != OBJECT_RELOCATION_X86_64_MACH_TLV_PC32) ||
        relocation->offset < cursor + 1 || relocation->offset + 4 > data.length)
    {
        return UINT64_MAX;
    }
    u64 start = relocation->offset - 1;
    if (data.pointer[start] == 0xe8)
    {
        return start;
    }
    if (relocation->offset < 3)
    {
        return UINT64_MAX;
    }
    start = relocation->offset - 3;
    if (data.pointer[start] != 0x48)
    {
        return UINT64_MAX;
    }
    if (data.pointer[start + 1] != 0x8b && data.pointer[start + 1] != 0x8d)
    {
        return UINT64_MAX;
    }
    if ((data.pointer[start + 2] & 0xc7) != 0x05)
    {
        return UINT64_MAX;
    }
    return start;
}

BUSTER_GLOBAL_LOCAL String8 object_assembly_x86_register(u32 index)
{
    switch (index & 7)
    {
    case 0:
        return S8("rax");
    case 1:
        return S8("rcx");
    case 2:
        return S8("rdx");
    case 3:
        return S8("rbx");
    case 4:
        return S8("rsp");
    case 5:
        return S8("rbp");
    case 6:
        return S8("rsi");
    case 7:
        return S8("rdi");
    }
    return S8("rax");
}

typedef struct ObjectAssemblyX86Prefix ObjectAssemblyX86Prefix;
struct ObjectAssemblyX86Prefix
{
    u8 rex;
    u8 length;
    bool operand16;
    bool address32;
    bool lock;
    bool rep;
    bool repne;
    bool segment_fs;
    bool segment_gs;
};

typedef struct ObjectAssemblyX86Modrm ObjectAssemblyX86Modrm;
struct ObjectAssemblyX86Modrm
{
    u8 mod;
    u8 scale;
    u8 disp_size;
    u8 reserved;
    u32 reg;
    u32 rm;
    u32 base;
    u32 index;
    s32 displacement;
    u64 length;
    u64 disp_offset;
    bool has_base;
    bool has_index;
    bool rip_relative;
};

BUSTER_GLOBAL_LOCAL bool object_assembly_x86_read_u32(ByteSlice data, u64 offset, u32* result)
{
    if (offset > data.length || sizeof(*result) > data.length - offset)
    {
        return false;
    }
    memcpy(result, data.pointer + offset, sizeof(*result));
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_assembly_x86_read_u64(ByteSlice data, u64 offset, u64* result)
{
    if (offset > data.length || sizeof(*result) > data.length - offset)
    {
        return false;
    }
    memcpy(result, data.pointer + offset, sizeof(*result));
    return true;
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_register(ObjectAssemblyBuffer* buffer, u32 index, u32 width, bool rex)
{
    index &= 15;
    if (width == 8)
    {
        if (!rex && index >= 4 && index < 8)
        {
            switch (index)
            {
            case 4:
                object_assembly_append_string(buffer, S8("ah"));
                return;
            case 5:
                object_assembly_append_string(buffer, S8("ch"));
                return;
            case 6:
                object_assembly_append_string(buffer, S8("dh"));
                return;
            case 7:
                object_assembly_append_string(buffer, S8("bh"));
                return;
            }
        }
        if (index < 4)
        {
            switch (index)
            {
            case 0:
                object_assembly_append_string(buffer, S8("al"));
                return;
            case 1:
                object_assembly_append_string(buffer, S8("cl"));
                return;
            case 2:
                object_assembly_append_string(buffer, S8("dl"));
                return;
            case 3:
                object_assembly_append_string(buffer, S8("bl"));
                return;
            }
        }
        if (index < 8)
        {
            switch (index)
            {
            case 4:
                object_assembly_append_string(buffer, S8("spl"));
                return;
            case 5:
                object_assembly_append_string(buffer, S8("bpl"));
                return;
            case 6:
                object_assembly_append_string(buffer, S8("sil"));
                return;
            case 7:
                object_assembly_append_string(buffer, S8("dil"));
                return;
            }
        }
        object_assembly_append_string(buffer, S8("r"));
        object_assembly_append_u64_decimal(buffer, index);
        object_assembly_append_string(buffer, S8("b"));
        return;
    }
    if (index >= 8)
    {
        object_assembly_append_string(buffer, S8("r"));
        object_assembly_append_u64_decimal(buffer, index);
        object_assembly_append_string(buffer, width == 16 ? S8("w") : width == 32 ? S8("d") : S8(""));
        return;
    }
    if (width == 16)
    {
        switch (index)
        {
        case 0:
            object_assembly_append_string(buffer, S8("ax"));
            return;
        case 1:
            object_assembly_append_string(buffer, S8("cx"));
            return;
        case 2:
            object_assembly_append_string(buffer, S8("dx"));
            return;
        case 3:
            object_assembly_append_string(buffer, S8("bx"));
            return;
        case 4:
            object_assembly_append_string(buffer, S8("sp"));
            return;
        case 5:
            object_assembly_append_string(buffer, S8("bp"));
            return;
        case 6:
            object_assembly_append_string(buffer, S8("si"));
            return;
        case 7:
            object_assembly_append_string(buffer, S8("di"));
            return;
        }
    }
    if (width == 32)
    {
        switch (index)
        {
        case 0:
            object_assembly_append_string(buffer, S8("eax"));
            return;
        case 1:
            object_assembly_append_string(buffer, S8("ecx"));
            return;
        case 2:
            object_assembly_append_string(buffer, S8("edx"));
            return;
        case 3:
            object_assembly_append_string(buffer, S8("ebx"));
            return;
        case 4:
            object_assembly_append_string(buffer, S8("esp"));
            return;
        case 5:
            object_assembly_append_string(buffer, S8("ebp"));
            return;
        case 6:
            object_assembly_append_string(buffer, S8("esi"));
            return;
        case 7:
            object_assembly_append_string(buffer, S8("edi"));
            return;
        }
    }
    switch (index)
    {
    case 0:
        object_assembly_append_string(buffer, S8("rax"));
        break;
    case 1:
        object_assembly_append_string(buffer, S8("rcx"));
        break;
    case 2:
        object_assembly_append_string(buffer, S8("rdx"));
        break;
    case 3:
        object_assembly_append_string(buffer, S8("rbx"));
        break;
    case 4:
        object_assembly_append_string(buffer, S8("rsp"));
        break;
    case 5:
        object_assembly_append_string(buffer, S8("rbp"));
        break;
    case 6:
        object_assembly_append_string(buffer, S8("rsi"));
        break;
    case 7:
        object_assembly_append_string(buffer, S8("rdi"));
        break;
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_vector_register(ObjectAssemblyBuffer* buffer, u32 index, u32 width)
{
    object_assembly_append_string(buffer, width == 32 ? S8("ymm") : width == 64 ? S8("zmm") : S8("xmm"));
    object_assembly_append_u64_decimal(buffer, index & 31);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_size(ObjectAssemblyBuffer* buffer, u32 width)
{
    switch (width)
    {
    case 8:
        object_assembly_append_string(buffer, S8("BYTE PTR "));
        break;
    case 16:
        object_assembly_append_string(buffer, S8("WORD PTR "));
        break;
    case 32:
        object_assembly_append_string(buffer, S8("DWORD PTR "));
        break;
    case 64:
        object_assembly_append_string(buffer, S8("QWORD PTR "));
        break;
    case 128:
        object_assembly_append_string(buffer, S8("XMMWORD PTR "));
        break;
    case 256:
        object_assembly_append_string(buffer, S8("YMMWORD PTR "));
        break;
    case 512:
        object_assembly_append_string(buffer, S8("ZMMWORD PTR "));
        break;
    }
}

BUSTER_GLOBAL_LOCAL bool object_assembly_x86_parse_prefix(ByteSlice data, u64 offset, u64 end, ObjectAssemblyX86Prefix* result)
{
    memset(result, 0, sizeof(*result));
    u64 cursor = offset;
    while (cursor < end && cursor - offset < 15)
    {
        u8 byte = data.pointer[cursor];
        if (byte == 0x66)
        {
            result->operand16 = true;
        }
        else if (byte == 0x67)
        {
            result->address32 = true;
        }
        else if (byte == 0xf0)
        {
            result->lock = true;
        }
        else if (byte == 0xf2)
        {
            result->repne = true;
        }
        else if (byte == 0xf3)
        {
            result->rep = true;
        }
        else if (byte == 0x64)
        {
            result->segment_fs = true;
        }
        else if (byte == 0x65)
        {
            result->segment_gs = true;
        }
        else if (byte >= 0x40 && byte <= 0x4f)
        {
            result->rex = byte;
        }
        else
        {
            break;
        }
        cursor += 1;
    }
    result->length = (u8)(cursor - offset);
    return cursor < end;
}

BUSTER_GLOBAL_LOCAL bool object_assembly_x86_parse_modrm(ByteSlice data, u64 offset, u64 end, ObjectAssemblyX86Prefix prefix,
                                                         ObjectAssemblyX86Modrm* result)
{
    memset(result, 0, sizeof(*result));
    if (offset >= end)
    {
        return false;
    }
    u8 modrm = data.pointer[offset];
    result->mod = modrm >> 6;
    result->reg = ((modrm >> 3) & 7) | ((prefix.rex & 4) ? 8 : 0);
    result->rm = (modrm & 7) | ((prefix.rex & 1) ? 8 : 0);
    u64 cursor = offset + 1;
    result->disp_offset = cursor;
    if (result->mod != 3 && (modrm & 7) == 4)
    {
        if (cursor >= end)
        {
            return false;
        }
        u8 sib = data.pointer[cursor++];
        result->scale = (u8)(1u << (sib >> 6));
        result->index = ((sib >> 3) & 7) | ((prefix.rex & 2) ? 8 : 0);
        result->base = (sib & 7) | ((prefix.rex & 1) ? 8 : 0);
        result->has_index = (sib & 7) != 4 || (prefix.rex & 2) != 0;
        result->has_base = result->mod != 0 || (sib & 7) != 5;
        if (!result->has_base)
        {
            result->disp_size = 4;
        }
    }
    else if (result->mod != 3)
    {
        result->base = result->rm;
        result->has_base = result->mod != 0 || (modrm & 7) != 5;
        result->rip_relative = result->mod == 0 && (modrm & 7) == 5 && !prefix.address32;
        if (!result->has_base)
        {
            result->disp_size = 4;
        }
    }
    if (result->mod == 1)
    {
        result->disp_size = 1;
    }
    else if (result->mod == 2)
    {
        result->disp_size = 4;
    }
    result->disp_offset = cursor;
    if (result->disp_size == 1)
    {
        if (cursor >= end)
        {
            return false;
        }
        result->displacement = (s8)data.pointer[cursor];
        cursor += 1;
    }
    else if (result->disp_size == 4)
    {
        u32 displacement = 0;
        if (!object_assembly_x86_read_u32(data, cursor, &displacement))
        {
            return false;
        }
        result->displacement = (s32)displacement;
        cursor += 4;
    }
    result->length = cursor - offset;
    return true;
}

BUSTER_GLOBAL_LOCAL ObjectRelocation* object_assembly_relocation_in_range(ObjectFile* object, u32 section, u64 start, u64 end)
{
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = object->relocations + index;
        if (relocation->section == section && relocation->offset >= start && relocation->offset < end)
        {
            return relocation;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool object_assembly_x86_modrm_has_stable_displacement(ObjectFile* object, u32 section, ObjectAssemblyX86Modrm modrm)
{
    return modrm.disp_size != 4 || object_assembly_relocation_at(object, section, modrm.disp_offset);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_relocation_expression(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target,
                                                                           ObjectRelocation* relocation, bool pc_relative)
{
    ObjectSymbol* symbol = relocation->symbol < object->symbol_count ? object->symbols + relocation->symbol : 0;
    if (!symbol)
    {
        object_assembly_append_string(buffer, S8("0"));
        return;
    }
    object_assembly_append_x86_symbol_reference(buffer, target, symbol->name);
    if (relocation->kind == OBJECT_RELOCATION_X86_64_TPOFF32)
    {
        object_assembly_append_string(buffer, S8("@TPOFF"));
    }
    else if (relocation->kind == OBJECT_RELOCATION_PE_TLS_OFFSET32)
    {
        object_assembly_append_string(buffer, S8("@SECREL32"));
    }
    else if (relocation->kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32)
    {
        object_assembly_append_string(buffer, S8("@TLVP"));
    }
    s64 addend = relocation->addend + (pc_relative ? 4 : 0);
    object_assembly_append_s64_addend(buffer, addend);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_displacement(ObjectAssemblyBuffer* buffer, s32 displacement)
{
    if (displacement)
    {
        object_assembly_append_string(buffer, displacement < 0 ? S8(" - ") : S8(" + "));
        u64 magnitude = displacement < 0 ? (u64)(-(displacement + 1)) + 1 : (u64)displacement;
        object_assembly_append_x86_number(buffer, (s64)magnitude);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_memory(ObjectAssemblyBuffer* buffer, ObjectAssemblyX86Modrm modrm, ObjectAssemblyX86Prefix prefix,
                                                            ObjectFile* object, Target target, ObjectRelocation* relocation)
{
    if (prefix.segment_fs)
    {
        object_assembly_append_string(buffer, S8("fs:"));
    }
    else if (prefix.segment_gs)
    {
        object_assembly_append_string(buffer, S8("gs:"));
    }
    object_assembly_append_string(buffer, S8("["));
    if (modrm.rip_relative)
    {
        object_assembly_append_string(buffer, S8("rip"));
    }
    else if (modrm.has_base)
    {
        object_assembly_append_x86_register(buffer, modrm.base, 64, true);
    }
    if (modrm.has_index)
    {
        if (modrm.has_base || modrm.rip_relative)
        {
            object_assembly_append_string(buffer, S8(" + "));
        }
        object_assembly_append_x86_register(buffer, modrm.index, 64, true);
        if (modrm.scale != 1)
        {
            object_assembly_append_string(buffer, S8(" * "));
            object_assembly_append_u64_decimal(buffer, modrm.scale);
        }
    }
    if (relocation && relocation->offset == modrm.disp_offset)
    {
        if (modrm.has_base || modrm.rip_relative || modrm.has_index)
        {
            object_assembly_append_string(buffer, S8(" + "));
        }
        object_assembly_append_x86_relocation_expression(buffer, object, target, relocation, modrm.rip_relative);
    }
    else if (!modrm.has_base && !modrm.has_index && !modrm.rip_relative)
    {
        object_assembly_append_x86_number(buffer, modrm.displacement);
    }
    else
    {
        object_assembly_append_x86_displacement(buffer, modrm.displacement);
    }
    object_assembly_append_string(buffer, S8("]"));
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_rm(ObjectAssemblyBuffer* buffer, ObjectAssemblyX86Modrm modrm, ObjectAssemblyX86Prefix prefix,
                                                        u32 width, bool sized, ObjectFile* object, Target target, ObjectRelocation* relocation)
{
    if (modrm.mod == 3)
    {
        object_assembly_append_x86_register(buffer, modrm.rm, width, prefix.rex != 0);
    }
    else
    {
        if (sized)
        {
            object_assembly_append_x86_size(buffer, width);
        }
        object_assembly_append_x86_memory(buffer, modrm, prefix, object, target, relocation);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_vector_rm(ObjectAssemblyBuffer* buffer, ObjectAssemblyX86Modrm modrm, u32 width, bool sized,
                                                              ObjectAssemblyX86Prefix prefix, ObjectFile* object, Target target,
                                                              ObjectRelocation* relocation)
{
    if (modrm.mod == 3)
    {
        object_assembly_append_x86_vector_register(buffer, modrm.rm, width);
    }
    else
    {
        if (sized)
        {
            object_assembly_append_x86_size(buffer, width * 8);
        }
        object_assembly_append_x86_memory(buffer, modrm, prefix, object, target, relocation);
    }
}

BUSTER_GLOBAL_LOCAL String8 object_assembly_x86_binary_name(u8 opcode)
{
    switch (opcode)
    {
    case 0x01:
    case 0x03:
        return S8("add");
    case 0x09:
    case 0x0b:
        return S8("or");
    case 0x21:
    case 0x23:
        return S8("and");
    case 0x29:
    case 0x2b:
        return S8("sub");
    case 0x31:
    case 0x33:
        return S8("xor");
    case 0x39:
    case 0x3b:
        return S8("cmp");
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 object_assembly_x86_group_name(u32 group)
{
    switch (group)
    {
    case 0:
        return S8("add");
    case 1:
        return S8("or");
    case 2:
        return S8("adc");
    case 3:
        return S8("sbb");
    case 4:
        return S8("and");
    case 5:
        return S8("sub");
    case 6:
        return S8("xor");
    case 7:
        return S8("cmp");
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 object_assembly_x86_condition(u32 condition)
{
    switch (condition & 15)
    {
    case 0:
        return S8("o");
    case 1:
        return S8("no");
    case 2:
        return S8("b");
    case 3:
        return S8("ae");
    case 4:
        return S8("e");
    case 5:
        return S8("ne");
    case 6:
        return S8("be");
    case 7:
        return S8("a");
    case 8:
        return S8("s");
    case 9:
        return S8("ns");
    case 10:
        return S8("p");
    case 11:
        return S8("np");
    case 12:
        return S8("l");
    case 13:
        return S8("ge");
    case 14:
        return S8("le");
    case 15:
        return S8("g");
    }
    return S8("e");
}

BUSTER_GLOBAL_LOCAL void object_assembly_x86_emit_prefix(ObjectAssemblyBuffer* buffer, ObjectAssemblyX86Prefix prefix)
{
    object_assembly_append_string(buffer, S8("\t"));
    if (prefix.lock)
    {
        object_assembly_append_string(buffer, S8("lock "));
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_x86_emit_immediate(ObjectAssemblyBuffer* buffer, u64 value, u32 width, bool sign_extend)
{
    if (sign_extend)
    {
        s64 signed_value = width == 8 ? (s64)(s8)value : width == 16 ? (s64)(s16)value : width == 32 ? (s64)(s32)value : (s64)value;
        object_assembly_append_x86_number(buffer, signed_value);
    }
    else
    {
        object_assembly_append_string(buffer, S8("0x"));
        u32 digit_count = 1;
        while (digit_count < 16 && (value >> (digit_count * 4)))
        {
            digit_count += 1;
        }
        object_assembly_append_u64_hex(buffer, value, digit_count);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_x86_branch_target(ObjectAssemblyBuffer* buffer, u32 section, u64 target)
{
    object_assembly_append_internal_label(buffer, section, target);
}

BUSTER_GLOBAL_LOCAL bool object_assembly_x86_branch_target(ByteSlice data, u64 target)
{
    return target < data.length;
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_x86_relative_target(u64 offset, u64 instruction_length, s64 displacement)
{
    return (u64)((s64)(offset + instruction_length) + displacement);
}

BUSTER_GLOBAL_LOCAL void object_assembly_mark_internal_label(ObjectAssemblyBuffer* buffer, u32 section, u64 offset);

BUSTER_GLOBAL_LOCAL bool object_assembly_x86_is_pc_relocation(ObjectRelocation* relocation)
{
    return relocation && (relocation->kind == OBJECT_RELOCATION_X86_64_PC32 || relocation->kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ||
                          relocation->kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32);
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_emit_x86_instruction(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, u32 section,
                                                              ByteSlice data, u64 offset, u64 end)
{
    ObjectAssemblyX86Prefix prefix;
    if (!object_assembly_x86_parse_prefix(data, offset, end, &prefix))
    {
        return 0;
    }
    u64 opcode_offset = offset + prefix.length;
    if (opcode_offset >= end)
    {
        return 0;
    }
    u8 opcode = data.pointer[opcode_offset];
    u64 cursor = opcode_offset + 1;
    u32 width = prefix.rex & 8 ? 64 : prefix.operand16 ? 16 : 32;
    if (opcode == 0x90 && prefix.rep && !prefix.lock)
    {
        object_assembly_append_string(buffer, S8("\t pause\n"));
        return prefix.length + 1;
    }
    if (opcode == 0x90 && !prefix.lock && !prefix.rep && !prefix.repne)
    {
        object_assembly_append_string(buffer, S8("\tnop\n"));
        return prefix.length + 1;
    }
    if ((opcode >= 0x50 && opcode <= 0x57) || (opcode >= 0x58 && opcode <= 0x5f))
    {
        object_assembly_x86_emit_prefix(buffer, prefix);
        object_assembly_append_string(buffer, opcode < 0x58 ? S8("push ") : S8("pop "));
        object_assembly_append_x86_register(buffer, (opcode & 7) | ((prefix.rex & 1) ? 8 : 0), prefix.operand16 ? 16 : 64, prefix.rex != 0);
        object_assembly_append_string(buffer, S8("\n"));
        return prefix.length + 1;
    }
    if (opcode == 0xc3 || opcode == 0xcb)
    {
        object_assembly_append_string(buffer, opcode == 0xc3 ? S8("\tret\n") : S8("\tretf\n"));
        return prefix.length + 1;
    }
    if (opcode == 0xc2 || opcode == 0xca)
    {
        u16 immediate = 0;
        if (cursor + 2 > end)
        {
            return 0;
        }
        memcpy(&immediate, data.pointer + cursor, sizeof(immediate));
        object_assembly_append_string(buffer, opcode == 0xc2 ? S8("\tret ") : S8("\tretf "));
        object_assembly_append_u64_decimal(buffer, immediate);
        object_assembly_append_string(buffer, S8("\n"));
        return prefix.length + 3;
    }
    if (opcode == 0xc9)
    {
        object_assembly_append_string(buffer, S8("\tleave\n"));
        return prefix.length + 1;
    }
    if (opcode == 0x98)
    {
        object_assembly_append_string(buffer, width == 64 ? S8("\tcdqe\n") : width == 16 ? S8("\tcwde\n") : S8("\tcwde\n"));
        return prefix.length + 1;
    }
    if (opcode == 0x99)
    {
        object_assembly_append_string(buffer, width == 64 ? S8("\tcqo\n") : width == 16 ? S8("\tcwd\n") : S8("\tcdq\n"));
        return prefix.length + 1;
    }
    if (opcode == 0x0f && cursor < end && data.pointer[cursor] == 0x77 && prefix.length == 0)
    {
        object_assembly_append_string(buffer, S8("\tvzeroupper\n"));
        return 2;
    }
    if (opcode == 0xe8 || opcode == 0xe9 || opcode == 0xeb || (opcode >= 0x70 && opcode <= 0x7f))
    {
        u32 displacement_size = opcode == 0xeb || (opcode >= 0x70 && opcode <= 0x7f) ? 1 : 4;
        u64 instruction_length = prefix.length + 1 + displacement_size;
        if (offset + instruction_length > end)
        {
            return 0;
        }
        ObjectRelocation* relocation = object_assembly_relocation_at(object, section, offset + prefix.length + 1);
        bool has_symbol = object_assembly_x86_is_pc_relocation(relocation) && displacement_size == 4;
        s64 displacement = displacement_size == 1 ? (s8)data.pointer[offset + prefix.length + 1] : 0;
        if (!has_symbol)
        {
            u32 encoded = 0;
            if (displacement_size == 4 && !object_assembly_x86_read_u32(data, offset + prefix.length + 1, &encoded))
            {
                return 0;
            }
            if (displacement_size == 4)
            {
                displacement = (s32)encoded;
            }
            u64 target_offset = object_assembly_x86_relative_target(offset, instruction_length, displacement);
            bool fixed_length_branch = opcode == 0xe8 || opcode == 0xeb || (opcode >= 0x70 && opcode <= 0x7f);
            bool has_target_label = fixed_length_branch && object_assembly_has_internal_label(buffer, section, target_offset);
            if (!has_target_label && fixed_length_branch && target_offset > offset && object_assembly_x86_branch_target(data, target_offset))
            {
                object_assembly_mark_internal_label(buffer, section, target_offset);
                has_target_label = true;
            }
            if (!fixed_length_branch || !object_assembly_x86_branch_target(data, target_offset) || !has_target_label)
            {
                return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
            }
        }
        object_assembly_x86_emit_prefix(buffer, prefix);
        if (opcode == 0xe8)
        {
            object_assembly_append_string(buffer, S8("call "));
        }
        else if (opcode == 0xe9 || opcode == 0xeb)
        {
            object_assembly_append_string(buffer, S8("jmp "));
        }
        else
        {
            object_assembly_append_string(buffer, S8("j"));
            object_assembly_append_string(buffer, object_assembly_x86_condition(opcode));
            object_assembly_append_string(buffer, S8(" "));
        }
        if (has_symbol)
        {
            object_assembly_append_x86_relocation_expression(buffer, object, target, relocation, true);
        }
        else
        {
            object_assembly_append_x86_branch_target(buffer, section, object_assembly_x86_relative_target(offset, instruction_length, displacement));
        }
        object_assembly_append_string(buffer, S8("\n"));
        return instruction_length;
    }
    if (opcode == 0x68 || opcode == 0x6a)
    {
        if (opcode == 0x68)
        {
            return 0;
        }
        u64 immediate_offset = cursor;
        u32 immediate_size = opcode == 0x6a ? 1 : 4;
        u32 immediate = 0;
        if (immediate_offset + immediate_size > end)
        {
            return 0;
        }
        if (immediate_size == 1)
        {
            immediate = data.pointer[immediate_offset];
        }
        else if (!object_assembly_x86_read_u32(data, immediate_offset, &immediate))
        {
            return 0;
        }
        object_assembly_append_string(buffer, S8("\tpush "));
        object_assembly_x86_emit_immediate(buffer, immediate, immediate_size * 8, true);
        object_assembly_append_string(buffer, S8("\n"));
        return prefix.length + 1 + immediate_size;
    }
    if (opcode >= 0xb8 && opcode <= 0xbf)
    {
        u32 immediate_size = prefix.rex & 8 ? 8 : prefix.operand16 ? 2 : 4;
        u64 immediate = 0;
        if (cursor + immediate_size > end)
        {
            return 0;
        }
        if (immediate_size == 2)
        {
            u16 value = 0;
            memcpy(&value, data.pointer + cursor, sizeof(value));
            immediate = value;
        }
        else if (immediate_size == 4)
        {
            u32 value = 0;
            if (!object_assembly_x86_read_u32(data, cursor, &value))
            {
                return 0;
            }
            immediate = value;
        }
        else if (!object_assembly_x86_read_u64(data, cursor, &immediate))
        {
            return 0;
        }
        object_assembly_append_string(buffer, S8("\t"));
        if (immediate_size == 8)
        {
            object_assembly_append_string(buffer, S8("movabs "));
        }
        else
        {
            object_assembly_append_string(buffer, S8("mov "));
        }
        object_assembly_append_x86_register(buffer, (opcode & 7) | ((prefix.rex & 1) ? 8 : 0), immediate_size == 8 ? 64 : immediate_size * 8,
                                             prefix.rex != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_x86_emit_immediate(buffer, immediate, immediate_size * 8, false);
        object_assembly_append_string(buffer, S8("\n"));
        return prefix.length + 1 + immediate_size;
    }
    if (opcode == 0x05 || opcode == 0x0d || opcode == 0x15 || opcode == 0x1d || opcode == 0x25 || opcode == 0x2d || opcode == 0x35 ||
        opcode == 0x3d)
    {
        if ((u64)prefix.length + 5 <= end - offset)
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, prefix.length + 5);
        }
        return 0;
    }
    if (opcode == 0x0f)
    {
        if (cursor >= end)
        {
            return 0;
        }
        u8 extended = data.pointer[cursor++];
        if (extended == 0x05)
        {
            object_assembly_append_string(buffer, S8("\tsyscall\n"));
            return prefix.length + 2;
        }
        if (extended == 0x34)
        {
            object_assembly_append_string(buffer, S8("\tsysenter\n"));
            return prefix.length + 2;
        }
        if (extended == 0x31)
        {
            object_assembly_append_string(buffer, S8("\trdtsc\n"));
            return prefix.length + 2;
        }
        if (extended == 0x77 && prefix.length == 0)
        {
            object_assembly_append_string(buffer, S8("\tvzeroupper\n"));
            return 2;
        }
        if (extended >= 0x80 && extended <= 0x8f)
        {
            if (cursor + 4 > end)
            {
                return 0;
            }
            u32 encoded = 0;
            object_assembly_x86_read_u32(data, cursor, &encoded);
            s64 displacement = (s32)encoded;
            u64 instruction_length = prefix.length + 6;
            u64 target_offset = object_assembly_x86_relative_target(offset, instruction_length, displacement);
            BUSTER_UNUSED(target_offset);
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        if (extended >= 0x90 && extended <= 0x9f)
        {
            ObjectAssemblyX86Modrm modrm;
            if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
            {
                return 0;
            }
            u64 instruction_length = prefix.length + 2 + modrm.length;
            if (offset + instruction_length > end)
            {
                return 0;
            }
            if (!object_assembly_x86_modrm_has_stable_displacement(object, section, modrm))
            {
                return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
            }
            object_assembly_x86_emit_prefix(buffer, prefix);
            object_assembly_append_string(buffer, S8("set"));
            object_assembly_append_string(buffer, object_assembly_x86_condition(extended));
            object_assembly_append_string(buffer, S8(" "));
            object_assembly_append_x86_rm(buffer, modrm, prefix, 8, true, object, target,
                                          object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
            object_assembly_append_string(buffer, S8("\n"));
            return instruction_length;
        }
        if (extended == 0xaf || extended == 0xbe || extended == 0xbf || extended == 0xb6 || extended == 0xb7 || extended == 0xb1 || extended == 0xc1)
        {
            ObjectAssemblyX86Modrm modrm;
            if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
            {
                return 0;
            }
            u32 source_width = extended == 0xbe || extended == 0xb6 ? 8 : extended == 0xbf || extended == 0xb7 ? 16 : width;
            u64 instruction_length = prefix.length + 2 + modrm.length;
            if (offset + instruction_length > end)
            {
                return 0;
            }
            if (!object_assembly_x86_modrm_has_stable_displacement(object, section, modrm))
            {
                return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
            }
            object_assembly_x86_emit_prefix(buffer, prefix);
            object_assembly_append_string(buffer, extended == 0xaf ? S8("imul ") : extended == 0xb1 ? S8("cmpxchg ")
                                                                                                   : extended == 0xc1 ? S8("xadd ")
                                                                                                                      : extended == 0xbe || extended == 0xbf ? S8("movsx ")
                                                                                                                                                           : S8("movzx "));
            if (extended == 0xaf || extended == 0xbe || extended == 0xbf || extended == 0xb6 || extended == 0xb7)
            {
                object_assembly_append_x86_register(buffer, modrm.reg, width, prefix.rex != 0);
                object_assembly_append_string(buffer, S8(", "));
                object_assembly_append_x86_rm(buffer, modrm, prefix, source_width, true, object, target,
                                              object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
            }
            else
            {
                object_assembly_append_x86_rm(buffer, modrm, prefix, width, true, object, target,
                                              object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
                object_assembly_append_string(buffer, S8(", "));
                object_assembly_append_x86_register(buffer, modrm.reg, width, prefix.rex != 0);
            }
            object_assembly_append_string(buffer, S8("\n"));
            return instruction_length;
        }
        if (extended == 0x1f)
        {
            ObjectAssemblyX86Modrm modrm;
            if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
            {
                return 0;
            }
            u64 instruction_length = prefix.length + 2 + modrm.length;
            if (offset + instruction_length > end)
            {
                return 0;
            }
            if (!object_assembly_x86_modrm_has_stable_displacement(object, section, modrm))
            {
                return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
            }
            object_assembly_append_string(buffer, S8("\tnop "));
            object_assembly_append_x86_rm(buffer, modrm, prefix, width, true, object, target,
                                          object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
            object_assembly_append_string(buffer, S8("\n"));
            return instruction_length;
        }
        if (extended == 0x6f || extended == 0x7f || extended == 0x10 || extended == 0x11 || extended == 0x58 || extended == 0x59 || extended == 0x5c ||
            extended == 0x2e || extended == 0xef || extended == 0xfc || extended == 0xfe)
        {
            ObjectAssemblyX86Modrm modrm;
            if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
            {
                return 0;
            }
            u32 vector_width = 16;
            String8 name = (String8){0};
            if (extended == 0x6f || extended == 0x7f)
            {
                name = prefix.rep ? S8("movdqu") : prefix.operand16 ? S8("movdqa") : S8("movq");
            }
            else if (extended == 0x10 || extended == 0x11)
            {
                name = prefix.rep ? S8("movss") : prefix.repne ? S8("movsd") : prefix.operand16 ? S8("movupd") : S8("movups");
                vector_width = prefix.rep || prefix.repne ? 4 : 16;
            }
            else if (extended == 0x58 || extended == 0x59 || extended == 0x5c)
            {
                name = extended == 0x58 ? prefix.rep ? S8("addss") : prefix.repne ? S8("addsd") : prefix.operand16 ? S8("addpd") : S8("addps")
                                       : extended == 0x59 ? prefix.rep ? S8("mulss") : prefix.repne ? S8("mulsd") : prefix.operand16 ? S8("mulpd")
                                                                                                               : S8("mulps")
                                                          : prefix.rep ? S8("subss") : prefix.repne ? S8("subsd") : prefix.operand16 ? S8("subpd")
                                                                                                               : S8("subps");
                vector_width = prefix.rep || prefix.repne ? 4 : 16;
            }
            else if (extended == 0x2e)
            {
                name = prefix.repne ? S8("ucomisd") : S8("ucomiss");
                vector_width = prefix.repne ? 8 : 4;
            }
            else if (extended == 0xef)
            {
                name = S8("pxor");
            }
            else
            {
                name = extended == 0xfc ? S8("paddb") : S8("paddd");
            }
            u64 instruction_length = prefix.length + 2 + modrm.length;
            if (offset + instruction_length > end)
            {
                return 0;
            }
            if (!object_assembly_x86_modrm_has_stable_displacement(object, section, modrm))
            {
                return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
            }
            ObjectRelocation* relocation = object_assembly_relocation_in_range(object, section, offset, offset + instruction_length);
            object_assembly_append_string(buffer, S8("\t"));
            object_assembly_append_string(buffer, name);
            object_assembly_append_string(buffer, S8(" "));
            if (extended == 0x7f || extended == 0x11)
            {
                object_assembly_append_x86_vector_rm(buffer, modrm, vector_width, true, prefix, object, target, relocation);
                object_assembly_append_string(buffer, S8(", "));
                object_assembly_append_x86_vector_register(buffer, modrm.reg, vector_width);
            }
            else
            {
                object_assembly_append_x86_vector_register(buffer, modrm.reg, vector_width);
                object_assembly_append_string(buffer, S8(", "));
                object_assembly_append_x86_vector_rm(buffer, modrm, vector_width, true, prefix, object, target, relocation);
            }
            object_assembly_append_string(buffer, S8("\n"));
            return instruction_length;
        }
        return 0;
    }
    if (opcode == 0x80 || opcode == 0x81 || opcode == 0x83)
    {
        ObjectAssemblyX86Modrm modrm;
        if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
        {
            return 0;
        }
        u32 immediate_size = opcode == 0x80 || opcode == 0x83 ? 1 : 4;
        u64 immediate_offset = cursor + modrm.length;
        if (immediate_offset + immediate_size > end)
        {
            return 0;
        }
        u32 immediate = 0;
        if (immediate_size == 1)
        {
            immediate = data.pointer[immediate_offset];
        }
        else if (!object_assembly_x86_read_u32(data, immediate_offset, &immediate))
        {
            return 0;
        }
        String8 name = object_assembly_x86_group_name(modrm.reg & 7);
        if (!name.length)
        {
            return 0;
        }
        u32 operand_width = opcode == 0x80 ? 8 : width;
        u64 instruction_length = prefix.length + 1 + modrm.length + immediate_size;
        if (opcode == 0x81 || !object_assembly_x86_modrm_has_stable_displacement(object, section, modrm) ||
            (modrm.mod != 3 && (prefix.rex & 8)))
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        ObjectRelocation* relocation = object_assembly_relocation_in_range(object, section, offset, offset + instruction_length);
        object_assembly_x86_emit_prefix(buffer, prefix);
        object_assembly_append_string(buffer, name);
        object_assembly_append_string(buffer, S8(" "));
        object_assembly_append_x86_rm(buffer, modrm, prefix, operand_width, true, object, target, relocation);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_x86_emit_immediate(buffer, immediate, immediate_size * 8, opcode != 0x81 || operand_width != 32);
        object_assembly_append_string(buffer, S8("\n"));
        return instruction_length;
    }
    if (opcode == 0xc6 || opcode == 0xc7)
    {
        ObjectAssemblyX86Modrm modrm;
        if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm) || modrm.reg != 0)
        {
            return 0;
        }
        u32 immediate_size = opcode == 0xc6 ? 1 : 4;
        u64 immediate_offset = cursor + modrm.length;
        u32 immediate = 0;
        if (immediate_offset + immediate_size > end)
        {
            return 0;
        }
        if (immediate_size == 1)
        {
            immediate = data.pointer[immediate_offset];
        }
        else if (!object_assembly_x86_read_u32(data, immediate_offset, &immediate))
        {
            return 0;
        }
        u32 operand_width = opcode == 0xc6 ? 1 : width;
        u64 instruction_length = prefix.length + 1 + modrm.length + immediate_size;
        if (!object_assembly_x86_modrm_has_stable_displacement(object, section, modrm) ||
            (modrm.mod != 3 && (prefix.rex & 8)))
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        object_assembly_append_string(buffer, S8("\tmov "));
        object_assembly_append_x86_rm(buffer, modrm, prefix, operand_width, true, object, target,
                                      object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_x86_emit_immediate(buffer, immediate, immediate_size * 8, opcode == 0xc7 && operand_width == 64);
        object_assembly_append_string(buffer, S8("\n"));
        return instruction_length;
    }
    if (opcode == 0x69 || opcode == 0x6b)
    {
        ObjectAssemblyX86Modrm modrm;
        if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
        {
            return 0;
        }
        u32 immediate_size = opcode == 0x6b ? 1 : 4;
        u64 immediate_offset = cursor + modrm.length;
        u32 immediate = 0;
        if (immediate_offset + immediate_size > end)
        {
            return 0;
        }
        if (immediate_size == 1)
        {
            immediate = data.pointer[immediate_offset];
        }
        else if (!object_assembly_x86_read_u32(data, immediate_offset, &immediate))
        {
            return 0;
        }
        u64 instruction_length = prefix.length + 1 + modrm.length + immediate_size;
        if (opcode == 0x69 || !object_assembly_x86_modrm_has_stable_displacement(object, section, modrm))
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        object_assembly_append_string(buffer, S8("\timul "));
        object_assembly_append_x86_register(buffer, modrm.reg, width, prefix.rex != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_x86_rm(buffer, modrm, prefix, width, true, object, target,
                                      object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_x86_emit_immediate(buffer, immediate, immediate_size * 8, true);
        object_assembly_append_string(buffer, S8("\n"));
        return instruction_length;
    }
    if (opcode == 0x88 || opcode == 0x89 || opcode == 0x8a || opcode == 0x8b || opcode == 0x8d || opcode == 0x01 || opcode == 0x03 || opcode == 0x09 ||
        opcode == 0x0b || opcode == 0x21 || opcode == 0x23 || opcode == 0x29 || opcode == 0x2b || opcode == 0x31 || opcode == 0x33 || opcode == 0x39 ||
        opcode == 0x3b || opcode == 0x85 || opcode == 0x87 || opcode == 0x86 || opcode == 0x63)
    {
        ObjectAssemblyX86Modrm modrm;
        if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
        {
            return 0;
        }
        u32 operand_width = opcode == 0x86 || opcode == 0x88 || opcode == 0x8a ? 8 : opcode == 0x63 ? 32 : width;
        u64 instruction_length = prefix.length + 1 + modrm.length;
        if (offset + instruction_length > end)
        {
            return 0;
        }
        if ((opcode == 0x8d && modrm.mod == 3) || !object_assembly_x86_modrm_has_stable_displacement(object, section, modrm))
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        ObjectRelocation* relocation = object_assembly_relocation_in_range(object, section, offset, offset + instruction_length);
        String8 name = opcode == 0x88 || opcode == 0x89 || opcode == 0x8a || opcode == 0x8b ? S8("mov")
                       : opcode == 0x8d                                    ? S8("lea")
                       : opcode == 0x87 || opcode == 0x86                 ? S8("xchg")
                       : opcode == 0x63                                    ? S8("movsxd")
                                                                            : object_assembly_x86_binary_name(opcode);
        if (!name.length && opcode != 0x85)
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        if (opcode == 0x85)
        {
            name = S8("test");
        }
        object_assembly_x86_emit_prefix(buffer, prefix);
        object_assembly_append_string(buffer, name);
        object_assembly_append_string(buffer, S8(" "));
        bool reverse = opcode == 0x8a || opcode == 0x8b || opcode == 0x8d || opcode == 0x03 || opcode == 0x0b || opcode == 0x23 || opcode == 0x2b ||
                       opcode == 0x33 || opcode == 0x3b || opcode == 0x63;
        if (opcode == 0x85)
        {
            object_assembly_append_x86_rm(buffer, modrm, prefix, operand_width, true, object, target, relocation);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_x86_register(buffer, modrm.reg, operand_width, prefix.rex != 0);
        }
        else if (reverse)
        {
            object_assembly_append_x86_register(buffer, modrm.reg, opcode == 0x63 ? 64 : operand_width, prefix.rex != 0);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_x86_rm(buffer, modrm, prefix, operand_width, opcode != 0x8d, object, target, relocation);
        }
        else
        {
            object_assembly_append_x86_rm(buffer, modrm, prefix, operand_width, true, object, target, relocation);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_x86_register(buffer, modrm.reg, operand_width, prefix.rex != 0);
        }
        object_assembly_append_string(buffer, S8("\n"));
        return instruction_length;
    }
    if (opcode == 0xf6 || opcode == 0xf7 || opcode == 0xfe || opcode == 0xff)
    {
        ObjectAssemblyX86Modrm modrm;
        if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
        {
            return 0;
        }
        u32 operand_width = opcode == 0xf6 || opcode == 0xfe ? 8 : width;
        u32 group = modrm.reg & 7;
        if (opcode == 0xff && (group == 2 || group == 4 || group == 6))
        {
            operand_width = prefix.operand16 ? 16 : 64;
        }
        u32 immediate_size = opcode == 0xf6 && group == 0 ? 1 : opcode == 0xf7 && group == 0 ? 4 : 0;
        u64 immediate_offset = cursor + modrm.length;
        u32 immediate = 0;
        if (immediate_size && immediate_offset + immediate_size > end)
        {
            return 0;
        }
        else if (immediate_size == 1)
        {
            immediate = data.pointer[immediate_offset];
        }
        else if (immediate_size == 4 && !object_assembly_x86_read_u32(data, immediate_offset, &immediate))
        {
            return 0;
        }
        u64 instruction_length = prefix.length + 1 + modrm.length + immediate_size;
        String8 name = group == 2 ? S8("not") : group == 3 ? S8("neg") : group == 4 ? S8("mul") : group == 5 ? S8("imul") : group == 6 ? S8("div")
                                                                                                                        : group == 7 ? S8("idiv")
                                                                                                                                     : group == 0 ? S8("test")
                                                                                                                                                  : group == 1 ? S8("test")
                                                                                                                                                               : (String8){0};
        if (opcode == 0xff)
        {
            name = group == 0 ? S8("inc") : group == 1 ? S8("dec") : group == 2 ? S8("call") : group == 4 ? S8("jmp") : group == 6 ? S8("push") : (String8){0};
        }
        else if (opcode == 0xfe)
        {
            name = group == 0 ? S8("inc") : group == 1 ? S8("dec") : (String8){0};
        }
        if (!name.length || ((opcode == 0xf6 || opcode == 0xf7) && group > 7))
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        if (!object_assembly_x86_modrm_has_stable_displacement(object, section, modrm) ||
            (modrm.mod != 3 && (prefix.rex & 8)))
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        object_assembly_x86_emit_prefix(buffer, prefix);
        object_assembly_append_string(buffer, name);
        object_assembly_append_string(buffer, S8(" "));
        object_assembly_append_x86_rm(buffer, modrm, prefix, operand_width, true, object,
                                      target, object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
        if (immediate_size)
        {
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_x86_emit_immediate(buffer, immediate, immediate_size * 8, true);
        }
        object_assembly_append_string(buffer, S8("\n"));
        return instruction_length;
    }
    if (opcode == 0xd0 || opcode == 0xd1 || opcode == 0xd2 || opcode == 0xd3 || opcode == 0xc0 || opcode == 0xc1)
    {
        ObjectAssemblyX86Modrm modrm;
        if (!object_assembly_x86_parse_modrm(data, cursor, end, prefix, &modrm))
        {
            return 0;
        }
        u32 group = modrm.reg & 7;
        String8 name = group == 4 ? S8("shl") : group == 5 ? S8("shr") : group == 7 ? S8("sar") : (String8){0};
        if (!name.length)
        {
            return 0;
        }
        u32 immediate_size = opcode == 0xc0 || opcode == 0xc1 ? 1 : 0;
        u64 immediate_offset = cursor + modrm.length;
        if (immediate_offset + immediate_size > end)
        {
            return 0;
        }
        u64 instruction_length = prefix.length + 1 + modrm.length + immediate_size;
        if (opcode == 0xc0 || opcode == 0xc1 || !object_assembly_x86_modrm_has_stable_displacement(object, section, modrm))
        {
            return object_assembly_emit_x86_raw_instruction(buffer, data, offset, instruction_length);
        }
        object_assembly_x86_emit_prefix(buffer, prefix);
        object_assembly_append_string(buffer, name);
        object_assembly_append_string(buffer, S8(" "));
        object_assembly_append_x86_rm(buffer, modrm, prefix, opcode == 0xc0 || opcode == 0xd0 || opcode == 0xd2 ? 8 : width, true, object, target,
                                      object_assembly_relocation_in_range(object, section, offset, offset + instruction_length));
        if (opcode == 0xc0 || opcode == 0xc1)
        {
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_x86_emit_immediate(buffer, data.pointer[immediate_offset], 8, false);
        }
        else if (opcode == 0xd2 || opcode == 0xd3)
        {
            object_assembly_append_string(buffer, S8(", cl"));
        }
        object_assembly_append_string(buffer, S8("\n"));
        return instruction_length;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void object_assembly_emit_apple_x86_relocation(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target,
                                                                    ObjectRelocation* relocation, ByteSlice data, u64 start)
{
    if (data.pointer[start] == 0xe8)
    {
        object_assembly_append_string(buffer, S8("\tcall "));
        if (relocation->symbol < object->symbol_count)
        {
            object_assembly_append_x86_symbol_reference(buffer, target, object->symbols[relocation->symbol].name);
        }
        else
        {
            object_assembly_append_string(buffer, S8("0"));
        }
        object_assembly_append_string(buffer, S8("\n"));
        return;
    }
    u32 register_index = (data.pointer[start + 2] >> 3) & 7;
    object_assembly_append_string(buffer, data.pointer[start + 1] == 0x8d ? S8("\tlea ") : S8("\tmov "));
    object_assembly_append_string(buffer, object_assembly_x86_register(register_index));
    object_assembly_append_string(buffer, S8(", [rip + "));
    if (relocation->symbol < object->symbol_count)
    {
        object_assembly_append_x86_symbol_reference(buffer, target, object->symbols[relocation->symbol].name);
        if (relocation->kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32)
        {
            object_assembly_append_string(buffer, S8("@TLVP"));
        }
    }
    else
    {
        object_assembly_append_string(buffer, S8("0"));
    }
    object_assembly_append_string(buffer, S8("]\n"));
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_sp_register(ObjectAssemblyBuffer* buffer, u32 index)
{
    if ((index & 31) == 31)
    {
        object_assembly_append_string(buffer, S8("sp"));
    }
    else
    {
        object_assembly_append_aarch64_register(buffer, index);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_sp_register_width(ObjectAssemblyBuffer* buffer, u32 index, bool wide)
{
    if ((index & 31) == 31)
    {
        object_assembly_append_string(buffer, S8("sp"));
    }
    else
    {
        object_assembly_append_aarch64_register_width(buffer, index, wide);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_load_register(ObjectAssemblyBuffer* buffer, u32 index, u32 size_bits)
{
    if ((index & 31) == 31)
    {
        object_assembly_append_string(buffer, size_bits == 3 ? S8("xzr") : S8("wzr"));
    }
    else if (size_bits < 2)
    {
        object_assembly_append_string(buffer, S8("w"));
        object_assembly_append_u64_decimal(buffer, index & 31);
    }
    else
    {
        object_assembly_append_aarch64_register_width(buffer, index, size_bits == 3);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_zero_register_width(ObjectAssemblyBuffer* buffer, u32 index, bool wide)
{
    if ((index & 31) == 31)
    {
        object_assembly_append_string(buffer, wide ? S8("xzr") : S8("wzr"));
    }
    else
    {
        object_assembly_append_aarch64_register_width(buffer, index, wide);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_immediate(ObjectAssemblyBuffer* buffer, s64 value)
{
    object_assembly_append_string(buffer, S8("#"));
    object_assembly_append_x86_number(buffer, value);
}

BUSTER_GLOBAL_LOCAL s64 object_assembly_aarch64_sign_extend(u64 value, u32 bits)
{
    u64 sign = UINT64_C(1) << (bits - 1);
    u64 mask = (sign << 1) - 1;
    value &= mask;
    return (s64)((value ^ sign) - sign);
}

BUSTER_GLOBAL_LOCAL String8 object_assembly_aarch64_condition(u32 condition)
{
    switch (condition & 15)
    {
    case 0:
        return S8("eq");
    case 1:
        return S8("ne");
    case 2:
        return S8("hs");
    case 3:
        return S8("lo");
    case 4:
        return S8("mi");
    case 5:
        return S8("pl");
    case 6:
        return S8("vs");
    case 7:
        return S8("vc");
    case 8:
        return S8("hi");
    case 9:
        return S8("ls");
    case 10:
        return S8("ge");
    case 11:
        return S8("lt");
    case 12:
        return S8("gt");
    case 13:
        return S8("le");
    case 14:
        return S8("al");
    case 15:
        return S8("nv");
    }
    return S8("al");
}

BUSTER_GLOBAL_LOCAL bool object_assembly_aarch64_target(ByteSlice data, u64 target)
{
    return target < data.length && (target & 3) == 0;
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_emit_aarch64_instruction(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, u32 section,
                                                                  ByteSlice data, u64 offset, u64 end)
{
    BUSTER_UNUSED(object);
    BUSTER_UNUSED(target);
    if (offset + 4 > end)
    {
        return 0;
    }
    u32 word = 0;
    memcpy(&word, data.pointer + offset, sizeof(word));
    if (word == UINT32_C(0xd503201f))
    {
        object_assembly_append_string(buffer, S8("\tnop\n"));
        return 4;
    }
    if ((word & UINT32_C(0xfffffc1f)) == UINT32_C(0xd65f0000))
    {
        object_assembly_append_string(buffer, S8("\tret"));
        if (((word >> 5) & 31) != 30)
        {
            object_assembly_append_string(buffer, S8(" "));
            object_assembly_append_aarch64_zero_register_width(buffer, word >> 5, true);
        }
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0xfffffc1f)) == UINT32_C(0xd61f0000) || (word & UINT32_C(0xfffffc1f)) == UINT32_C(0xd63f0000))
    {
        object_assembly_append_string(buffer, (word & UINT32_C(0xfffffc1f)) == UINT32_C(0xd61f0000) ? S8("\tbr ") : S8("\tblr "));
        object_assembly_append_aarch64_zero_register_width(buffer, word >> 5, true);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7c000000)) == UINT32_C(0x14000000))
    {
        s64 displacement = object_assembly_aarch64_sign_extend(word & UINT32_C(0x03ffffff), 26) << 2;
        u64 target_offset = (u64)((s64)offset + displacement);
        if (!object_assembly_aarch64_target(data, target_offset) || !object_assembly_has_internal_label(buffer, section, target_offset))
        {
            return 0;
        }
        object_assembly_append_string(buffer, word & UINT32_C(0x80000000) ? S8("\tbl ") : S8("\tb "));
        object_assembly_append_internal_label(buffer, section, target_offset);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x3b000000)) == UINT32_C(0x18000000))
    {
        s64 displacement = object_assembly_aarch64_sign_extend((word >> 5) & UINT32_C(0x7ffff), 19) << 2;
        u64 literal_offset = (u64)((s64)offset + displacement);
        if (!object_assembly_aarch64_target(data, literal_offset) || !object_assembly_has_internal_label(buffer, section, literal_offset))
        {
            return 0;
        }
        object_assembly_append_string(buffer, S8("\tldr "));
        object_assembly_append_aarch64_register(buffer, word);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_internal_label(buffer, section, literal_offset);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0xff000010)) == UINT32_C(0x54000000))
    {
        s64 displacement = object_assembly_aarch64_sign_extend((word >> 5) & UINT32_C(0x7ffff), 19) << 2;
        u64 target_offset = (u64)((s64)offset + displacement);
        if (!object_assembly_aarch64_target(data, target_offset) || !object_assembly_has_internal_label(buffer, section, target_offset))
        {
            return 0;
        }
        object_assembly_append_string(buffer, S8("\tb."));
        object_assembly_append_string(buffer, object_assembly_aarch64_condition(word));
        object_assembly_append_string(buffer, S8(" "));
        object_assembly_append_internal_label(buffer, section, target_offset);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7f000000)) == UINT32_C(0x34000000))
    {
        bool wide = (word & UINT32_C(0x80000000)) != 0;
        bool nonzero = (word & UINT32_C(0x01000000)) != 0;
        s64 displacement = object_assembly_aarch64_sign_extend((word >> 5) & UINT32_C(0x7ffff), 19) << 2;
        u64 target_offset = (u64)((s64)offset + displacement);
        if (!object_assembly_aarch64_target(data, target_offset) || !object_assembly_has_internal_label(buffer, section, target_offset))
        {
            return 0;
        }
        object_assembly_append_string(buffer, nonzero ? S8("\tcbnz ") : S8("\tcbz "));
        object_assembly_append_aarch64_zero_register_width(buffer, word, wide);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_internal_label(buffer, section, target_offset);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7f000000)) == UINT32_C(0x36000000))
    {
        bool nonzero = (word & UINT32_C(0x01000000)) != 0;
        u32 bit = ((word >> 31) & 1) * 32 + ((word >> 19) & 31);
        s64 displacement = object_assembly_aarch64_sign_extend((word >> 5) & UINT32_C(0x3fff), 14) << 2;
        u64 target_offset = (u64)((s64)offset + displacement);
        if (!object_assembly_aarch64_target(data, target_offset) || !object_assembly_has_internal_label(buffer, section, target_offset))
        {
            return 0;
        }
        object_assembly_append_string(buffer, nonzero ? S8("\ttbnz ") : S8("\ttbz "));
        object_assembly_append_aarch64_zero_register_width(buffer, word, true);
        object_assembly_append_string(buffer, S8(", #"));
        object_assembly_append_u64_decimal(buffer, bit);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_internal_label(buffer, section, target_offset);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7fe0fc00)) == UINT32_C(0x1ac02000) || (word & UINT32_C(0x7fe0fc00)) == UINT32_C(0x1ac02400) ||
        (word & UINT32_C(0x7fe0fc00)) == UINT32_C(0x1ac02800) || (word & UINT32_C(0x7fe0fc00)) == UINT32_C(0x1ac02c00))
    {
        u32 operation = word & UINT32_C(0x7fe0fc00);
        object_assembly_append_string(buffer, operation == UINT32_C(0x1ac02000) ? S8("\tlsl ") : operation == UINT32_C(0x1ac02400) ? S8("\tlsr ")
                                                                                   : operation == UINT32_C(0x1ac02800)          ? S8("\tasr ")
                                                                                                                               : S8("\tror "));
        object_assembly_append_aarch64_zero_register_width(buffer, word, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register_width(buffer, word >> 5, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register_width(buffer, word >> 16, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7fe0fc00)) == UINT32_C(0x1b007c00))
    {
        object_assembly_append_string(buffer, S8("\tmul "));
        object_assembly_append_aarch64_zero_register_width(buffer, word, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register_width(buffer, word >> 5, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register_width(buffer, word >> 16, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7fe00c00)) == UINT32_C(0x1a800400) && ((word >> 5) & 31) == 31 && ((word >> 16) & 31) == 31)
    {
        object_assembly_append_string(buffer, S8("\tcset "));
        object_assembly_append_aarch64_register_width(buffer, word, (word & UINT32_C(0x80000000)) != 0);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_string(buffer, object_assembly_aarch64_condition(((word >> 12) & 15) ^ 1));
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    u32 logical_operation = word & UINT32_C(0x7f200000);
    if (logical_operation == UINT32_C(0x0a000000) || logical_operation == UINT32_C(0x2a000000) || logical_operation == UINT32_C(0x4a000000))
    {
        bool wide = (word & UINT32_C(0x80000000)) != 0;
        u32 destination = word & 31;
        u32 source = (word >> 5) & 31;
        u32 second = (word >> 16) & 31;
        String8 name = logical_operation == UINT32_C(0x0a000000) ? S8("and") : logical_operation == UINT32_C(0x2a000000) ? S8("orr") : S8("eor");
        if (logical_operation == UINT32_C(0x2a000000) && source == 31)
        {
            object_assembly_append_string(buffer, S8("\tmov "));
            object_assembly_append_aarch64_zero_register_width(buffer, destination, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_zero_register_width(buffer, second, wide);
        }
        else
        {
            object_assembly_append_string(buffer, S8("\t"));
            object_assembly_append_string(buffer, name);
            object_assembly_append_string(buffer, S8(" "));
            object_assembly_append_aarch64_zero_register_width(buffer, destination, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_zero_register_width(buffer, source, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_zero_register_width(buffer, second, wide);
        }
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x1f000000)) == UINT32_C(0x11000000))
    {
        bool wide = (word & UINT32_C(0x80000000)) != 0;
        bool subtract = (word & UINT32_C(0x40000000)) != 0;
        bool set_flags = (word & UINT32_C(0x20000000)) != 0;
        u32 immediate = (word >> 10) & 0xfff;
        if (word & UINT32_C(0x00400000))
        {
            immediate <<= 12;
        }
        u32 destination = word & 31;
        u32 source = (word >> 5) & 31;
        if (set_flags && destination == 31)
        {
            object_assembly_append_string(buffer, subtract ? S8("\tcmp ") : S8("\tcmn "));
            object_assembly_append_aarch64_sp_register_width(buffer, source, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_immediate(buffer, immediate);
        }
        else
        {
            object_assembly_append_string(buffer, subtract ? set_flags ? S8("\tsubs ") : S8("\tsub ") : set_flags ? S8("\tadds ") : S8("\tadd "));
            object_assembly_append_aarch64_sp_register_width(buffer, destination, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_sp_register_width(buffer, source, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_immediate(buffer, immediate);
        }
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x1f200000)) == UINT32_C(0x0b000000))
    {
        bool wide = (word & UINT32_C(0x80000000)) != 0;
        bool subtract = (word & UINT32_C(0x40000000)) != 0;
        bool set_flags = (word & UINT32_C(0x20000000)) != 0;
        u32 destination = word & 31;
        u32 source = (word >> 5) & 31;
        u32 second = (word >> 16) & 31;
        u32 shift = (word >> 10) & 63;
        if (set_flags && destination == 31)
        {
            object_assembly_append_string(buffer, subtract ? S8("\tcmp ") : S8("\tcmn "));
            object_assembly_append_aarch64_register_width(buffer, source, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_register_width(buffer, second, wide);
        }
        else
        {
            object_assembly_append_string(buffer, subtract ? set_flags ? S8("\tsubs ") : S8("\tsub ") : set_flags ? S8("\tadds ") : S8("\tadd "));
            object_assembly_append_aarch64_register_width(buffer, destination, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_register_width(buffer, source, wide);
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_register_width(buffer, second, wide);
            if (shift)
            {
                object_assembly_append_string(buffer, S8(", lsl #"));
                object_assembly_append_u64_decimal(buffer, shift);
            }
        }
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7f800000)) == UINT32_C(0x52800000) || (word & UINT32_C(0x7f800000)) == UINT32_C(0x72800000) ||
        (word & UINT32_C(0x7f800000)) == UINT32_C(0x12800000))
    {
        bool wide = (word & UINT32_C(0x80000000)) != 0;
        u32 operation = (word >> 29) & 3;
        u32 shift = ((word >> 21) & 3) * 16;
        u32 immediate = (word >> 5) & 0xffff;
        String8 name = operation == 2 ? S8("movz") : operation == 3 ? S8("movk") : S8("movn");
        object_assembly_append_string(buffer, S8("\t"));
        object_assembly_append_string(buffer, name);
        object_assembly_append_string(buffer, S8(" "));
        object_assembly_append_aarch64_register_width(buffer, word, wide);
        object_assembly_append_string(buffer, S8(", #0x"));
        object_assembly_append_u64_hex(buffer, immediate, 4);
        if (shift)
        {
            object_assembly_append_string(buffer, S8(", lsl #"));
            object_assembly_append_u64_decimal(buffer, shift);
        }
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x3b000000)) == UINT32_C(0x39000000))
    {
        u32 size_bits = (word >> 30) & 3;
        u32 bytes = 1u << size_bits;
        bool load = (word & UINT32_C(0x00400000)) != 0;
        u32 register_index = word;
        u32 base = word >> 5;
        u32 immediate = ((word >> 10) & 0xfff) * bytes;
        String8 name = size_bits == 0 ? load ? S8("\tldrb ") : S8("\tstrb ") : size_bits == 1 ? load ? S8("\tldrh ") : S8("\tstrh ")
                                                                                               : load ? S8("\tldr ") : S8("\tstr ");
        object_assembly_append_string(buffer, name);
        object_assembly_append_aarch64_load_register(buffer, register_index, size_bits);
        object_assembly_append_string(buffer, S8(", ["));
        object_assembly_append_aarch64_sp_register(buffer, base);
        if (immediate)
        {
            object_assembly_append_string(buffer, S8(", "));
            object_assembly_append_aarch64_immediate(buffer, immediate);
        }
        object_assembly_append_string(buffer, S8("]\n"));
        return 4;
    }
    if ((word & UINT32_C(0x3a000000)) == UINT32_C(0x28000000))
    {
        bool load = (word & UINT32_C(0x00400000)) != 0;
        bool wide = (word & UINT32_C(0x80000000)) != 0;
        u32 mode = (word >> 23) & 3;
        s64 displacement = object_assembly_aarch64_sign_extend((word >> 15) & 0x7f, 7) * (wide ? 8 : 4);
        object_assembly_append_string(buffer, load ? S8("\tldp ") : S8("\tstp "));
        object_assembly_append_aarch64_register_width(buffer, word, wide);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register_width(buffer, word >> 10, wide);
        object_assembly_append_string(buffer, S8(", ["));
        object_assembly_append_aarch64_sp_register(buffer, word >> 5);
        if (mode == 1)
        {
            object_assembly_append_string(buffer, S8("], "));
            object_assembly_append_aarch64_immediate(buffer, displacement);
        }
        else
        {
            if (displacement)
            {
                object_assembly_append_string(buffer, S8(", "));
                object_assembly_append_aarch64_immediate(buffer, displacement);
            }
            object_assembly_append_string(buffer, S8("]"));
            if (mode == 3)
            {
                object_assembly_append_string(buffer, S8("!"));
            }
        }
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0x7f000000)) == UINT32_C(0x13000000))
    {
        object_assembly_append_string(buffer, S8("\tsxtw "));
        object_assembly_append_aarch64_register(buffer, word);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_register_width(buffer, word >> 5, false);
        object_assembly_append_string(buffer, S8("\n"));
        return 4;
    }
    if ((word & UINT32_C(0xffc00000)) == UINT32_C(0xd5000000))
    {
        object_assembly_append_string(buffer, S8("\tmrs "));
        object_assembly_append_aarch64_register(buffer, word);
        object_assembly_append_string(buffer, S8(", tpidr_el0\n"));
        return 4;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void object_assembly_mark_internal_label(ObjectAssemblyBuffer* buffer, u32 section, u64 offset)
{
    if (buffer->internal_labels && buffer->internal_label_section == section && offset < buffer->internal_label_count)
    {
        buffer->internal_labels[offset] = 1;
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_mark_relative_label(ObjectAssemblyBuffer* buffer, u32 section, u64 instruction_end, s64 displacement)
{
    s64 target = (s64)instruction_end + displacement;
    if (target >= 0)
    {
        object_assembly_mark_internal_label(buffer, section, (u64)target);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_prepare_internal_labels(ObjectAssemblyBuffer* buffer, ObjectFile* object, u32 section_index)
{
    ObjectSection* section = object->sections + section_index;
    if (section->kind != OBJECT_SECTION_TEXT || !section->data.length)
    {
        buffer->internal_labels = 0;
        buffer->internal_label_count = 0;
        return;
    }
    buffer->internal_labels = arena_allocate(buffer->arena, u8, section->data.length);
    buffer->internal_label_count = buffer->internal_labels ? section->data.length : 0;
    buffer->internal_label_section = section_index;
    if (!buffer->internal_labels)
    {
        buffer->error = true;
        return;
    }
    memset(buffer->internal_labels, 0, section->data.length);
    ByteSlice data = section->data;
    if (object->target.cpu_arch == CPU_ARCH_AARCH64)
    {
        for (u64 offset = 0; offset + 4 <= data.length; offset += 4)
        {
            u32 word = 0;
            memcpy(&word, data.pointer + offset, sizeof(word));
            s64 displacement = 0;
            u32 instruction_length = 0;
            if ((word & UINT32_C(0x3b000000)) == UINT32_C(0x18000000))
            {
                displacement = object_assembly_aarch64_sign_extend((word >> 5) & UINT32_C(0x7ffff), 19) << 2;
                s64 literal_target = (s64)offset + displacement;
                if (literal_target >= 0 && object_assembly_relocation_at(object, section_index, (u64)literal_target))
                {
                    object_assembly_mark_internal_label(buffer, section_index, (u64)literal_target);
                }
            }
            else if ((word & UINT32_C(0x7c000000)) == UINT32_C(0x14000000))
            {
                displacement = (s64)(word & UINT32_C(0x03ffffff)) << 2;
                if (displacement & (INT64_C(1) << 27))
                {
                    displacement -= INT64_C(1) << 28;
                }
                instruction_length = 4;
            }
            else if ((word & UINT32_C(0xff000010)) == UINT32_C(0x54000000))
            {
                displacement = (s64)((word >> 5) & UINT32_C(0x7ffff)) << 2;
                if (displacement & (INT64_C(1) << 20))
                {
                    displacement -= INT64_C(1) << 21;
                }
                instruction_length = 4;
            }
            else if ((word & UINT32_C(0x7f000000)) == UINT32_C(0x34000000))
            {
                displacement = (s64)((word >> 5) & UINT32_C(0x7ffff)) << 2;
                if (displacement & (INT64_C(1) << 20))
                {
                    displacement -= INT64_C(1) << 21;
                }
                instruction_length = 4;
            }
            else if ((word & UINT32_C(0x7f000000)) == UINT32_C(0x36000000))
            {
                displacement = (s64)((word >> 5) & UINT32_C(0x3fff)) << 2;
                if (displacement & (INT64_C(1) << 15))
                {
                    displacement -= INT64_C(1) << 16;
                }
                instruction_length = 4;
            }
            if (instruction_length)
            {
                object_assembly_mark_relative_label(buffer, section_index, offset, displacement);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL u64 object_assembly_next_internal_label(ObjectAssemblyBuffer* buffer, u32 section, u64 offset, u64 end)
{
    if (!buffer->internal_labels || buffer->internal_label_section != section)
    {
        return end;
    }
    for (u64 index = offset; index < end && index < buffer->internal_label_count; index += 1)
    {
        if (buffer->internal_labels[index])
        {
            return index;
        }
    }
    return end;
}

BUSTER_GLOBAL_LOCAL void object_assembly_emit_section(ObjectAssemblyBuffer* buffer, ObjectFile* object, Target target, u32 section_index)
{
    ObjectSection* section = object->sections + section_index;
    u64 data_length = section->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? section->virtual_size : section->data.length;
    bool has_symbols = false;
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        has_symbols |= object->symbols[symbol_index].section == section_index;
    }
    if (!data_length && !has_symbols)
    {
        return;
    }
    object_assembly_append_string(buffer, object_assembly_section_directive(target, section->kind));
    if (section->alignment > 1)
    {
        object_assembly_append_string(buffer, S8("\t.p2align "));
        object_assembly_append_u64_decimal(buffer, object_assembly_alignment_exponent(section->alignment));
        object_assembly_append_string(buffer, S8("\n"));
    }
    if (section->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO)
    {
        object_assembly_emit_labels(buffer, object, target, section_index, 0);
        if (data_length)
        {
            object_assembly_append_string(buffer, S8("\t.zero "));
            object_assembly_append_u64_decimal(buffer, data_length);
            object_assembly_append_string(buffer, S8("\n"));
        }
        object_assembly_emit_sizes(buffer, object, target, section_index);
        return;
    }
    ByteSlice data = section->data;
    if (section->kind == OBJECT_SECTION_TEXT)
    {
        object_assembly_prepare_internal_labels(buffer, object, section_index);
    }
    u64 cursor = 0;
    while (cursor < data.length && !buffer->error)
    {
        ObjectRelocation* next_relocation = object_assembly_first_relocation(object, section_index, cursor, data.length);
        u64 apple_x86_start = object_assembly_apple_x86_relocation_start(next_relocation, data, cursor);
        if (object_assembly_is_apple(target) && apple_x86_start != UINT64_MAX)
        {
            if (apple_x86_start > cursor)
            {
                u64 boundary = BUSTER_MIN(apple_x86_start, object_assembly_next_symbol(object, section_index, cursor, data.length));
                object_assembly_emit_labels(buffer, object, target, section_index, cursor);
                object_assembly_emit_internal_label(buffer, section_index, cursor);
                object_assembly_append_byte_range(buffer, data, cursor, boundary);
                cursor = boundary;
                continue;
            }
            object_assembly_emit_labels(buffer, object, target, section_index, cursor);
            object_assembly_emit_internal_label(buffer, section_index, cursor);
            object_assembly_emit_apple_x86_relocation(buffer, object, target, next_relocation, data, cursor);
            cursor = next_relocation->offset + 4;
            continue;
        }
        object_assembly_emit_labels(buffer, object, target, section_index, cursor);
        object_assembly_emit_internal_label(buffer, section_index, cursor);
        ObjectRelocation* relocation = object_assembly_relocation_at(object, section_index, cursor);
        if (relocation)
        {
            if (!object_assembly_emit_relocation(buffer, object, target, relocation, data))
            {
                buffer->error = true;
                break;
            }
            cursor += object_assembly_relocation_size(relocation->kind);
            continue;
        }
        if (section->kind == OBJECT_SECTION_TEXT)
        {
            u64 instruction_end = object_assembly_next_symbol(object, section_index, cursor, data.length);
            u64 instruction_length = target.cpu_arch == CPU_ARCH_X86_64
                                         ? object_assembly_emit_x86_instruction(buffer, object, target, section_index, data, cursor, instruction_end)
                                         : target.cpu_arch == CPU_ARCH_AARCH64
                                               ? object_assembly_emit_aarch64_instruction(buffer, object, target, section_index, data, cursor, instruction_end)
                                               : 0;
            if (instruction_length)
            {
                cursor += instruction_length;
                continue;
            }
            if (target.cpu_arch == CPU_ARCH_AARCH64 && cursor + 4 <= data.length)
            {
                u32 word = 0;
                memcpy(&word, data.pointer + cursor, sizeof(word));
                object_assembly_append_string(buffer, S8("\t.word 0x"));
                object_assembly_append_u64_hex(buffer, word, 8);
                object_assembly_append_string(buffer, S8("\n"));
                cursor += 4;
                continue;
            }
        }
        u64 boundary = object_assembly_next_relocation(object, section_index, cursor + 1, data.length);
        boundary = BUSTER_MIN(boundary, object_assembly_next_symbol(object, section_index, cursor, data.length));
        boundary = BUSTER_MIN(boundary, object_assembly_next_internal_label(buffer, section_index, cursor + 1, data.length));
        if (boundary <= cursor)
        {
            boundary = cursor + 1;
        }
        object_assembly_append_byte_range(buffer, data, cursor, boundary);
        cursor = boundary;
    }
    object_assembly_emit_labels(buffer, object, target, section_index, data.length);
    object_assembly_emit_internal_label(buffer, section_index, data.length);
    object_assembly_emit_sizes(buffer, object, target, section_index);
}

String8 object_print_assembly(Arena* arena, ObjectFile* object)
{
    if (!arena || !object || object->error != OBJECT_ERROR_NONE || !object->sections ||
        (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations) ||
        (object->target.cpu_arch != CPU_ARCH_X86_64 && object->target.cpu_arch != CPU_ARCH_AARCH64))
    {
        return (String8){0};
    }
    u64 capacity = 1024;
    for (u32 section_index = 0; section_index < object->section_count; section_index += 1)
    {
        ObjectSection* section = object->sections + section_index;
        if (section->data.length > (UINT64_MAX - capacity) / 16)
        {
            return (String8){0};
        }
        capacity += section->data.length * 16 + 256;
        if (section->virtual_size > (UINT64_MAX - capacity) / 2)
        {
            return (String8){0};
        }
        capacity += section->virtual_size * 2;
    }
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        capacity += object->symbols[symbol_index].name.length * 2 + 128;
    }
    capacity += (u64)object->relocation_count * 256;
    ObjectAssemblyBuffer buffer = {
        .bytes = arena_allocate(arena, char8, capacity),
        .capacity = capacity,
        .arena = arena,
    };
    if (!buffer.bytes)
    {
        return (String8){0};
    }
    if (object->target.cpu_arch == CPU_ARCH_X86_64)
    {
        object_assembly_append_string(&buffer, S8("\t.intel_syntax noprefix\n"));
    }
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        ObjectSymbol* symbol = object->symbols + symbol_index;
        if (symbol->section != OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        object_assembly_append_string(&buffer, S8("\t.extern "));
        object_assembly_append_assembly_symbol(&buffer, object->target, symbol->name);
        object_assembly_append_string(&buffer, S8("\n"));
    }
    for (u32 section_index = 0; section_index < object->section_count; section_index += 1)
    {
        object_assembly_emit_section(&buffer, object, object->target, section_index);
    }
    if (buffer.error)
    {
        return (String8){0};
    }
    return (String8){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
}

BUSTER_GLOBAL_LOCAL bool object_read_u16(ByteSlice bytes, u64 offset, u16* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_read_u32(ByteSlice bytes, u64 offset, u32* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_read_u64(ByteSlice bytes, u64 offset, u64* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_read_s64(ByteSlice bytes, u64 offset, s64* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
}

BUSTER_GLOBAL_LOCAL String8 object_read_string(ByteSlice bytes, u64 table_offset, u64 table_size, u32 string_offset)
{
    if (string_offset >= table_size || table_offset > bytes.length || table_size > bytes.length - table_offset)
    {
        return (String8){0};
    }
    u64 offset = table_offset + string_offset;
    u64 remaining = table_size - string_offset;
    u64 length = 0;
    while (length < remaining && bytes.pointer[offset + length])
    {
        length += 1;
    }
    if (length == remaining)
    {
        return (String8){0};
    }
    return (String8){
        .pointer = (char8*)bytes.pointer + offset,
        .length = length,
    };
}

String8 object_section_name_for_kind(ObjectSectionKind kind)
{
    switch (kind)
    {
    case OBJECT_SECTION_TEXT:
        return S8(".text");
    case OBJECT_SECTION_READ_ONLY_DATA:
        return S8(".rodata");
    case OBJECT_SECTION_DATA:
        return S8(".data");
    case OBJECT_SECTION_THREAD_LOCAL_DATA:
        return S8(".tdata");
    case OBJECT_SECTION_THREAD_LOCAL_ZERO:
        return S8(".tbss");
    case OBJECT_SECTION_DEBUG_INFO:
        return S8(".debug_info");
    case OBJECT_SECTION_DEBUG_ABBREV:
        return S8(".debug_abbrev");
    case OBJECT_SECTION_DEBUG_LINE:
        return S8(".debug_line");
    case OBJECT_SECTION_DEBUG_STR:
        return S8(".debug_str");
    case OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS:
        return S8(".debug$S");
    case OBJECT_SECTION_DEBUG_CODEVIEW_TYPES:
        return S8(".debug$T");
    case OBJECT_SECTION_COUNT:
        break;
    }
    return (String8){0};
}

u32 object_section_default_alignment(ObjectSectionKind kind)
{
    return object_section_kind_is_debug(kind) ? 1 : 16;
}

BUSTER_GLOBAL_LOCAL ObjectSectionKind object_debug_section_kind_from_name(String8 name)
{
    if (string_equal(name, S8(".debug_info")) || string_equal(name, S8("__debug_info")))
    {
        return OBJECT_SECTION_DEBUG_INFO;
    }
    if (string_equal(name, S8(".debug_abbrev")) || string_equal(name, S8("__debug_abbrev")))
    {
        return OBJECT_SECTION_DEBUG_ABBREV;
    }
    if (string_equal(name, S8(".debug_line")) || string_equal(name, S8("__debug_line")))
    {
        return OBJECT_SECTION_DEBUG_LINE;
    }
    if (string_equal(name, S8(".debug_str")) || string_equal(name, S8("__debug_str")))
    {
        return OBJECT_SECTION_DEBUG_STR;
    }
    if (string_equal(name, S8(".debug$S")))
    {
        return OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS;
    }
    if (string_equal(name, S8(".debug$T")))
    {
        return OBJECT_SECTION_DEBUG_CODEVIEW_TYPES;
    }
    return OBJECT_SECTION_COUNT;
}

BUSTER_GLOBAL_LOCAL ObjectFile object_read_elf64(Arena* arena, ByteSlice bytes, Target target)
{
    ObjectFile result = {
        .target = target,
        .error = OBJECT_ERROR_INVALID_INPUT,
    };
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_SECTION_HEADER_SIZE = 64,
        ELF_SYMBOL_SIZE = 24,
        ELF_RELOCATION_SIZE = 24,
    };
    u16 type = 0;
    u16 machine = 0;
    u64 section_table = 0;
    u16 section_entry_size = 0;
    u16 section_count = 0;
    u16 section_string_index = 0;
    if (!arena || bytes.length < ELF_HEADER_SIZE ||
        memcmp(bytes.pointer,
               "\x7f"
               "ELF",
               4) != 0 ||
        bytes.pointer[4] != 2 || bytes.pointer[5] != 1 || !object_read_u16(bytes, 16, &type) || type != 1 || !object_read_u16(bytes, 18, &machine) ||
        !object_read_u64(bytes, 40, &section_table) || !object_read_u16(bytes, 58, &section_entry_size) || !object_read_u16(bytes, 60, &section_count) ||
        !object_read_u16(bytes, 62, &section_string_index) || section_entry_size != ELF_SECTION_HEADER_SIZE || !section_count ||
        section_string_index >= section_count || section_table > bytes.length || (u64)section_count * ELF_SECTION_HEADER_SIZE > bytes.length - section_table ||
        (machine == 62 && target.cpu_arch != CPU_ARCH_X86_64) || (machine == 183 && target.cpu_arch != CPU_ARCH_AARCH64) || (machine != 62 && machine != 183))
    {
        return result;
    }
    u64 section_string_offset = 0;
    u64 section_string_size = 0;
    {
        u64 section = section_table + (u64)section_string_index * ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        if (!object_read_u32(bytes, section + 4, &section_type) || section_type != 3 || !object_read_u64(bytes, section + 24, &section_string_offset) ||
            !object_read_u64(bytes, section + 32, &section_string_size) || section_string_offset > bytes.length ||
            section_string_size > bytes.length - section_string_offset)
        {
            return result;
        }
    }
    u32* section_kinds = arena_allocate(arena, u32, section_count);
    u64* section_bases = arena_allocate(arena, u64, section_count);
    u64 section_sizes[OBJECT_SECTION_COUNT] = {0};
    u32 section_alignments[OBJECT_SECTION_COUNT];
    for (u32 alignment_kind = 0; alignment_kind < OBJECT_SECTION_COUNT; alignment_kind += 1)
    {
        section_alignments[alignment_kind] = 1;
    }
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        section_kinds[section_index] = UINT32_MAX;
        u64 section = section_table + (u64)section_index * ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        u32 name_offset = 0;
        u64 flags = 0;
        u64 offset = 0;
        u64 size = 0;
        u64 alignment = 0;
        if (!object_read_u32(bytes, section, &name_offset) || !object_read_u32(bytes, section + 4, &section_type) ||
            !object_read_u64(bytes, section + 8, &flags) || !object_read_u64(bytes, section + 24, &offset) || !object_read_u64(bytes, section + 32, &size) ||
            !object_read_u64(bytes, section + 48, &alignment))
        {
            return result;
        }
        String8 name = object_read_string(bytes, section_string_offset, section_string_size, name_offset);
        bool ignored = string_equal(name, S8(".eh_frame")) || string_starts_with_sequence(name, S8(".gcc_except_table")) ||
                       string_starts_with_sequence(name, S8(".ARM.exidx")) || string_starts_with_sequence(name, S8(".ARM.extab"));
        ObjectSectionKind debug_kind = flags & 0x2 ? OBJECT_SECTION_COUNT : object_debug_section_kind_from_name(name);
        if ((!(flags & 0x2) && debug_kind == OBJECT_SECTION_COUNT) || (section_type != 1 && section_type != 8) || ignored)
        {
            continue;
        }
        if (!alignment)
        {
            alignment = 1;
        }
        if (alignment > UINT32_MAX || (alignment & (alignment - 1)) || (section_type != 8 && (offset > bytes.length || size > bytes.length - offset)))
        {
            return result;
        }
        ObjectSectionKind kind = debug_kind != OBJECT_SECTION_COUNT ? debug_kind
                                 : flags & 0x400                    ? (section_type == 8 ? OBJECT_SECTION_THREAD_LOCAL_ZERO : OBJECT_SECTION_THREAD_LOCAL_DATA)
                                 : flags & 0x4                      ? OBJECT_SECTION_TEXT
                                 : flags & 0x1                      ? OBJECT_SECTION_DATA
                                                                    : OBJECT_SECTION_READ_ONLY_DATA;
        u64 base = align_forward(section_sizes[kind], alignment);
        if (base < section_sizes[kind] || size > UINT64_MAX - base)
        {
            return result;
        }
        section_kinds[section_index] = (u32)kind;
        section_bases[section_index] = base;
        section_sizes[kind] = base + size;
        section_alignments[kind] = BUSTER_MAX(section_alignments[kind], (u32)alignment);
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        bool zero_fill = kind == OBJECT_SECTION_THREAD_LOCAL_ZERO;
        result.sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .data =
                {
                    .pointer = zero_fill ? 0 : arena_allocate(arena, u8, section_sizes[kind]),
                    .length = zero_fill ? 0 : section_sizes[kind],
                },
            .virtual_size = section_sizes[kind],
            .kind = (ObjectSectionKind)kind,
            .alignment = section_alignments[kind],
        };
    }
    u32 symbol_section = UINT32_MAX;
    u32 symbol_count = 0;
    u32 string_section = UINT32_MAX;
    u64 symbol_offset = 0;
    u64 string_offset = 0;
    u64 string_size = 0;
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        u64 offset = 0;
        u64 size = 0;
        u64 entry_size = 0;
        u32 link = 0;
        if (!object_read_u32(bytes, section + 4, &section_type) || !object_read_u64(bytes, section + 24, &offset) ||
            !object_read_u64(bytes, section + 32, &size) || !object_read_u32(bytes, section + 40, &link) || !object_read_u64(bytes, section + 56, &entry_size))
        {
            return result;
        }
        if (section_kinds[section_index] != UINT32_MAX)
        {
            ObjectSectionKind kind = (ObjectSectionKind)section_kinds[section_index];
            if (section_type != 8 && size)
            {
                memcpy(result.sections[kind].data.pointer + section_bases[section_index], bytes.pointer + offset, size);
            }
        }
        if (section_type == 2)
        {
            if (symbol_section != UINT32_MAX || entry_size != ELF_SYMBOL_SIZE || size % ELF_SYMBOL_SIZE || offset > bytes.length ||
                size > bytes.length - offset || link >= section_count)
            {
                return result;
            }
            symbol_section = section_index;
            symbol_count = (u32)(size / ELF_SYMBOL_SIZE);
            symbol_offset = offset;
            string_section = link;
        }
    }
    if (symbol_section == UINT32_MAX || string_section == UINT32_MAX)
    {
        return result;
    }
    {
        u64 section = section_table + (u64)string_section * ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        if (!object_read_u32(bytes, section + 4, &section_type) || section_type != 3 || !object_read_u64(bytes, section + 24, &string_offset) ||
            !object_read_u64(bytes, section + 32, &string_size) || string_offset > bytes.length || string_size > bytes.length - string_offset)
        {
            return result;
        }
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_count);
    u32* symbol_map = arena_allocate(arena, u32, symbol_count);
    for (u32 source_index = 0; source_index < symbol_count; source_index += 1)
    {
        symbol_map[source_index] = UINT32_MAX;
        if (!source_index)
        {
            continue;
        }
        u64 source = symbol_offset + (u64)source_index * ELF_SYMBOL_SIZE;
        u32 name_offset = 0;
        u16 section_index = 0;
        u64 value = 0;
        u64 size = 0;
        if (!object_read_u32(bytes, source, &name_offset) || !object_read_u16(bytes, source + 6, &section_index) ||
            !object_read_u64(bytes, source + 8, &value) || !object_read_u64(bytes, source + 16, &size))
        {
            return result;
        }
        u8 information = bytes.pointer[source + 4];
        u8 symbol_type = information & 0xf;
        if (symbol_type == 4)
        {
            continue;
        }
        if (section_index != 0 && (section_index >= section_count || section_kinds[section_index] == UINT32_MAX))
        {
            continue;
        }
        String8 name = object_read_string(bytes, string_offset, string_size, name_offset);
        if (!name.length)
        {
            name = string_format(arena, S8(".Lobject.{u32}"), source_index);
        }
        ObjectSymbol* destination = &result.symbols[result.symbol_count];
        *destination = (ObjectSymbol){
            .name = string_duplicate_arena(arena, name, false),
            .value = section_index ? section_bases[section_index] + value : 0,
            .size = size,
            .section = section_index ? section_kinds[section_index] : OBJECT_SECTION_UNDEFINED,
            .kind = symbol_type == 2 ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
            .global = (information >> 4) != 0,
        };
        symbol_map[source_index] = result.symbol_count++;
    }
    u32 relocation_capacity = 0;
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        u64 size = 0;
        u64 entry_size = 0;
        u32 target_section = 0;
        if (!object_read_u32(bytes, section + 4, &section_type) || !object_read_u64(bytes, section + 32, &size) ||
            !object_read_u32(bytes, section + 44, &target_section) || !object_read_u64(bytes, section + 56, &entry_size))
        {
            return result;
        }
        if (section_type != 4)
        {
            continue;
        }
        if (entry_size != ELF_RELOCATION_SIZE || size % ELF_RELOCATION_SIZE || target_section >= section_count)
        {
            return result;
        }
        if (section_kinds[target_section] == UINT32_MAX)
        {
            continue;
        }
        if (size / ELF_RELOCATION_SIZE > UINT32_MAX - relocation_capacity)
        {
            return result;
        }
        relocation_capacity += (u32)(size / ELF_RELOCATION_SIZE);
    }
    result.relocations = arena_allocate(arena, ObjectRelocation, relocation_capacity);
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        u64 offset = 0;
        u64 size = 0;
        u32 linked_symbols = 0;
        u32 target_section = 0;
        if (!object_read_u32(bytes, section + 4, &section_type) || !object_read_u64(bytes, section + 24, &offset) ||
            !object_read_u64(bytes, section + 32, &size) || !object_read_u32(bytes, section + 40, &linked_symbols) ||
            !object_read_u32(bytes, section + 44, &target_section))
        {
            return result;
        }
        if (section_type != 4 || target_section >= section_count || section_kinds[target_section] == UINT32_MAX)
        {
            continue;
        }
        if (linked_symbols != symbol_section || offset > bytes.length || size > bytes.length - offset)
        {
            return result;
        }
        u32 count = (u32)(size / ELF_RELOCATION_SIZE);
        for (u32 relocation_index = 0; relocation_index < count; relocation_index += 1)
        {
            u64 relocation = offset + (u64)relocation_index * ELF_RELOCATION_SIZE;
            u64 source_offset = 0;
            u64 information = 0;
            s64 addend = 0;
            if (!object_read_u64(bytes, relocation, &source_offset) || !object_read_u64(bytes, relocation + 8, &information) ||
                !object_read_s64(bytes, relocation + 16, &addend))
            {
                return result;
            }
            u32 source_symbol = (u32)(information >> 32);
            u32 relocation_type = (u32)information;
            if (source_symbol >= symbol_count || symbol_map[source_symbol] == UINT32_MAX)
            {
                return result;
            }
            ObjectRelocationKind kind = OBJECT_RELOCATION_COUNT;
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                kind = relocation_type == 1                           ? OBJECT_RELOCATION_ABSOLUTE64
                       : relocation_type == 2 || relocation_type == 4 ? OBJECT_RELOCATION_X86_64_PC32
                       : relocation_type == 10                        ? OBJECT_RELOCATION_ABSOLUTE32
                       : relocation_type == 23                        ? OBJECT_RELOCATION_X86_64_TPOFF32
                                                                      : OBJECT_RELOCATION_COUNT;
                if (relocation_type == 4)
                {
                    result.symbols[symbol_map[source_symbol]].kind = OBJECT_SYMBOL_FUNCTION;
                }
            }
            else
            {
                kind = relocation_type == 257                             ? OBJECT_RELOCATION_ABSOLUTE64
                       : relocation_type == 258                           ? OBJECT_RELOCATION_ABSOLUTE32
                       : relocation_type == 282 || relocation_type == 283 ? OBJECT_RELOCATION_AARCH64_CALL26
                       : relocation_type == 549                           ? OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12
                       : relocation_type == 551                           ? OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12
                                                                          : OBJECT_RELOCATION_COUNT;
                if (relocation_type == 282 || relocation_type == 283)
                {
                    result.symbols[symbol_map[source_symbol]].kind = OBJECT_SYMBOL_FUNCTION;
                }
            }
            if (kind == OBJECT_RELOCATION_COUNT || source_offset > result.sections[section_kinds[target_section]].virtual_size - section_bases[target_section])
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
            result.relocations[result.relocation_count++] = (ObjectRelocation){
                .addend = addend,
                .offset = section_bases[target_section] + source_offset,
                .section = section_kinds[target_section],
                .symbol = symbol_map[source_symbol],
                .kind = kind,
            };
        }
    }
    result.error = OBJECT_ERROR_NONE;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 object_read_coff_name(ByteSlice bytes, u64 offset, u64 string_offset, u64 string_size)
{
    if (offset > bytes.length || 8 > bytes.length - offset)
    {
        return (String8){0};
    }
    u32 zero = 0;
    u32 name_offset = 0;
    memcpy(&zero, bytes.pointer + offset, sizeof(zero));
    memcpy(&name_offset, bytes.pointer + offset + 4, sizeof(name_offset));
    if (!zero)
    {
        return object_read_string(bytes, string_offset, string_size, name_offset);
    }
    u64 length = 0;
    while (length < 8 && bytes.pointer[offset + length])
    {
        length += 1;
    }
    return (String8){
        .pointer = (char8*)bytes.pointer + offset,
        .length = length,
    };
}

BUSTER_GLOBAL_LOCAL ObjectFile object_read_coff(Arena* arena, ByteSlice bytes, Target target)
{
    ObjectFile result = {
        .target = target,
        .error = OBJECT_ERROR_INVALID_INPUT,
    };
    enum
    {
        COFF_HEADER_SIZE = 20,
        COFF_SECTION_SIZE = 40,
        COFF_RELOCATION_SIZE = 10,
        COFF_SYMBOL_SIZE = 18,
    };
    u16 machine = 0;
    u16 section_count = 0;
    u32 symbol_offset = 0;
    u32 symbol_count = 0;
    u16 optional_size = 0;
    if (!arena || bytes.length < COFF_HEADER_SIZE || !object_read_u16(bytes, 0, &machine) || !object_read_u16(bytes, 2, &section_count) ||
        !object_read_u32(bytes, 8, &symbol_offset) || !object_read_u32(bytes, 12, &symbol_count) || !object_read_u16(bytes, 16, &optional_size) ||
        optional_size || !section_count || (machine == 0x8664 && target.cpu_arch != CPU_ARCH_X86_64) ||
        (machine == 0xaa64 && target.cpu_arch != CPU_ARCH_AARCH64) || (machine != 0x8664 && machine != 0xaa64) ||
        (u64)COFF_HEADER_SIZE + (u64)section_count * COFF_SECTION_SIZE > bytes.length || symbol_offset > bytes.length ||
        (u64)symbol_count * COFF_SYMBOL_SIZE > bytes.length - symbol_offset)
    {
        return result;
    }
    u64 string_offset = symbol_offset + (u64)symbol_count * COFF_SYMBOL_SIZE;
    u32 string_size_u32 = 0;
    if (!object_read_u32(bytes, string_offset, &string_size_u32) || string_size_u32 < 4 || string_size_u32 > bytes.length - string_offset)
    {
        return result;
    }
    u64 string_size = string_size_u32;
    u32* section_kinds = arena_allocate(arena, u32, section_count);
    u64* section_bases = arena_allocate(arena, u64, section_count);
    u64 section_sizes[OBJECT_SECTION_COUNT] = {0};
    u32 section_alignments[OBJECT_SECTION_COUNT];
    for (u32 alignment_kind = 0; alignment_kind < OBJECT_SECTION_COUNT; alignment_kind += 1)
    {
        section_alignments[alignment_kind] = 1;
    }
    u32 relocation_capacity = 0;
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = COFF_HEADER_SIZE + (u64)section_index * COFF_SECTION_SIZE;
        u32 raw_size = 0;
        u32 raw_offset = 0;
        u32 relocation_offset = 0;
        u16 relocation_count = 0;
        u32 characteristics = 0;
        if (!object_read_u32(bytes, section + 16, &raw_size) || !object_read_u32(bytes, section + 20, &raw_offset) ||
            !object_read_u32(bytes, section + 24, &relocation_offset) || !object_read_u16(bytes, section + 32, &relocation_count) ||
            !object_read_u32(bytes, section + 36, &characteristics) || (raw_offset && (raw_offset > bytes.length || raw_size > bytes.length - raw_offset)) ||
            (relocation_count && (relocation_offset > bytes.length || (u64)relocation_count * COFF_RELOCATION_SIZE > bytes.length - relocation_offset)) ||
            relocation_count > UINT32_MAX - relocation_capacity)
        {
            return result;
        }
        String8 name = {
            .pointer = (char8*)bytes.pointer + section,
            .length = 0,
        };
        while (name.length < 8 && name.pointer[name.length])
        {
            name.length += 1;
        }
        bool is_thread_local =
            string_starts_with_sequence(name, S8(".tls")) || string_starts_with_sequence(name, S8(".tdata")) || string_starts_with_sequence(name, S8(".tbss"));
        // CodeView sections are discardable but must survive a round trip
        // through a relocatable object, so they are classified before the
        // discardable/removable filter.
        ObjectSectionKind debug_kind = object_debug_section_kind_from_name(name);
        bool ignored = debug_kind == OBJECT_SECTION_COUNT &&
                       ((characteristics & 0x02000800) != 0 || string_equal(name, S8(".pdata")) || string_equal(name, S8(".xdata")));
        if (ignored)
        {
            section_kinds[section_index] = UINT32_MAX;
            continue;
        }
        bool zero_fill = !raw_offset || (characteristics & 0x80) != 0;
        ObjectSectionKind kind = debug_kind != OBJECT_SECTION_COUNT ? debug_kind
                                 : is_thread_local                  ? (zero_fill ? OBJECT_SECTION_THREAD_LOCAL_ZERO : OBJECT_SECTION_THREAD_LOCAL_DATA)
                                 : characteristics & 0x20           ? OBJECT_SECTION_TEXT
                                 : characteristics & 0x80000000     ? OBJECT_SECTION_DATA
                                                                    : OBJECT_SECTION_READ_ONLY_DATA;
        u32 alignment_code = (characteristics >> 20) & 0xf;
        u32 alignment = alignment_code > 1 ? 1u << (alignment_code - 1) : 1;
        u64 base = align_forward(section_sizes[kind], alignment);
        if (base < section_sizes[kind] || raw_size > UINT64_MAX - base)
        {
            return result;
        }
        section_kinds[section_index] = (u32)kind;
        section_bases[section_index] = base;
        section_sizes[kind] = base + raw_size;
        section_alignments[kind] = BUSTER_MAX(section_alignments[kind], alignment);
        relocation_capacity += relocation_count;
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        bool zero_fill = kind == OBJECT_SECTION_THREAD_LOCAL_ZERO;
        result.sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .data =
                {
                    .pointer = zero_fill ? 0 : arena_allocate(arena, u8, section_sizes[kind]),
                    .length = zero_fill ? 0 : section_sizes[kind],
                },
            .virtual_size = section_sizes[kind],
            .kind = (ObjectSectionKind)kind,
            .alignment = section_alignments[kind],
        };
    }
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = COFF_HEADER_SIZE + (u64)section_index * COFF_SECTION_SIZE;
        u32 raw_size = 0;
        u32 raw_offset = 0;
        object_read_u32(bytes, section + 16, &raw_size);
        object_read_u32(bytes, section + 20, &raw_offset);
        if (section_kinds[section_index] == UINT32_MAX)
        {
            continue;
        }
        ObjectSectionKind kind = (ObjectSectionKind)section_kinds[section_index];
        if (raw_offset && raw_size)
        {
            memcpy(result.sections[kind].data.pointer + section_bases[section_index], bytes.pointer + raw_offset, raw_size);
        }
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_count);
    u32* symbol_map = arena_allocate(arena, u32, symbol_count);
    for (u32 source_index = 0; source_index < symbol_count;)
    {
        symbol_map[source_index] = UINT32_MAX;
        u64 source = symbol_offset + (u64)source_index * COFF_SYMBOL_SIZE;
        u32 value = 0;
        u16 section_number_u16 = 0;
        u16 symbol_type = 0;
        if (!object_read_u32(bytes, source + 8, &value) || !object_read_u16(bytes, source + 12, &section_number_u16) ||
            !object_read_u16(bytes, source + 14, &symbol_type))
        {
            return result;
        }
        s16 section_number = (s16)section_number_u16;
        u8 storage = bytes.pointer[source + 16];
        u8 auxiliary_count = bytes.pointer[source + 17];
        if (auxiliary_count > symbol_count - source_index - 1)
        {
            return result;
        }
        for (u8 auxiliary = 0; auxiliary < auxiliary_count; auxiliary += 1)
        {
            symbol_map[source_index + auxiliary + 1] = UINT32_MAX;
        }
        if (section_number >= 0 && (section_number == 0 || (section_number <= section_count && section_kinds[(u16)section_number - 1] != UINT32_MAX)))
        {
            String8 name = object_read_coff_name(bytes, source, string_offset, string_size);
            if (!name.length)
            {
                name = string_format(arena, S8(".Lcoff.{u32}"), source_index);
            }
            u32 destination_index = result.symbol_count++;
            result.symbols[destination_index] = (ObjectSymbol){
                .name = string_duplicate_arena(arena, name, false),
                .value = section_number ? section_bases[(u16)section_number - 1] + value : 0,
                .section = section_number ? section_kinds[(u16)section_number - 1] : OBJECT_SECTION_UNDEFINED,
                .kind = symbol_type & 0x20 ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
                .global = storage == 2 || storage == 105,
            };
            symbol_map[source_index] = destination_index;
        }
        source_index += (u32)auxiliary_count + 1;
    }
    result.relocations = arena_allocate(arena, ObjectRelocation, relocation_capacity);
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = COFF_HEADER_SIZE + (u64)section_index * COFF_SECTION_SIZE;
        u32 raw_size = 0;
        u32 raw_offset = 0;
        u32 relocation_offset = 0;
        u16 relocation_count = 0;
        object_read_u32(bytes, section + 16, &raw_size);
        object_read_u32(bytes, section + 20, &raw_offset);
        object_read_u32(bytes, section + 24, &relocation_offset);
        object_read_u16(bytes, section + 32, &relocation_count);
        if (section_kinds[section_index] == UINT32_MAX)
        {
            continue;
        }
        for (u16 relocation_index = 0; relocation_index < relocation_count; relocation_index += 1)
        {
            u64 relocation = relocation_offset + (u64)relocation_index * COFF_RELOCATION_SIZE;
            u32 source_offset = 0;
            u32 source_symbol = 0;
            u16 relocation_type = 0;
            if (!object_read_u32(bytes, relocation, &source_offset) || !object_read_u32(bytes, relocation + 4, &source_symbol) ||
                !object_read_u16(bytes, relocation + 8, &relocation_type) || source_symbol >= symbol_count || symbol_map[source_symbol] == UINT32_MAX ||
                source_offset > raw_size)
            {
                return result;
            }
            ObjectRelocationKind kind = OBJECT_RELOCATION_COUNT;
            s64 addend = 0;
            ObjectSymbol* referenced = &result.symbols[symbol_map[source_symbol]];
            // SECREL32 and SECTION share their type numbers with the TLS
            // relocations, so CodeView sections select the debug meaning.
            bool codeview_section = section_kinds[section_index] == OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS ||
                                    section_kinds[section_index] == OBJECT_SECTION_DEBUG_CODEVIEW_TYPES;
            if (codeview_section)
            {
                u16 secrel_type = target.cpu_arch == CPU_ARCH_X86_64 ? 0x000b : 0x0008;
                u16 section_type = target.cpu_arch == CPU_ARCH_X86_64 ? 0x000a : 0x0007;
                kind = relocation_type == secrel_type    ? OBJECT_RELOCATION_COFF_SECREL32
                       : relocation_type == section_type ? OBJECT_RELOCATION_COFF_SECTION16
                                                         : OBJECT_RELOCATION_COUNT;
                if (kind == OBJECT_RELOCATION_COUNT)
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                result.relocations[result.relocation_count++] = (ObjectRelocation){
                    .offset = section_bases[section_index] + source_offset,
                    .section = section_kinds[section_index],
                    .symbol = symbol_map[source_symbol],
                    .kind = kind,
                };
                continue;
            }
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                if (relocation_type == 1)
                {
                    kind = OBJECT_RELOCATION_ABSOLUTE64;
                    u64 stored = 0;
                    if (!object_read_u64(bytes, (u64)raw_offset + source_offset, &stored))
                    {
                        return result;
                    }
                    addend = (s64)stored;
                }
                else if (relocation_type >= 4 && relocation_type <= 9)
                {
                    u32 stored = 0;
                    if (!object_read_u32(bytes, (u64)raw_offset + source_offset, &stored))
                    {
                        return result;
                    }
                    kind = string_equal(referenced->name, S8("__tls_index")) ? OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 : OBJECT_RELOCATION_X86_64_PC32;
                    addend = (s64)(s32)stored - 4 - (relocation_type - 4);
                    referenced->kind = OBJECT_SYMBOL_FUNCTION;
                }
                else if (relocation_type == 0xb)
                {
                    u32 stored = 0;
                    if (!object_read_u32(bytes, (u64)raw_offset + source_offset, &stored))
                    {
                        return result;
                    }
                    kind = OBJECT_RELOCATION_PE_TLS_OFFSET32;
                    addend = (s32)stored;
                }
            }
            else
            {
                kind = relocation_type == 3     ? OBJECT_RELOCATION_AARCH64_CALL26
                       : relocation_type == 4   ? OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP
                       : relocation_type == 7   ? OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12
                       : relocation_type == 0xf ? OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12
                       : relocation_type == 0xe ? OBJECT_RELOCATION_ABSOLUTE64
                                                : OBJECT_RELOCATION_COUNT;
                if (relocation_type == 3)
                {
                    referenced->kind = OBJECT_SYMBOL_FUNCTION;
                }
            }
            if (kind == OBJECT_RELOCATION_COUNT)
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
            result.relocations[result.relocation_count++] = (ObjectRelocation){
                .addend = addend,
                .offset = section_bases[section_index] + source_offset,
                .section = section_kinds[section_index],
                .symbol = symbol_map[source_symbol],
                .kind = kind,
            };
        }
    }
    result.error = OBJECT_ERROR_NONE;
    return result;
}

BUSTER_GLOBAL_LOCAL ObjectFile object_read_mach_o64(Arena* arena, ByteSlice bytes, Target target)
{
    ObjectFile result = {
        .target = target,
        .error = OBJECT_ERROR_INVALID_INPUT,
    };
    enum
    {
        MACH_HEADER_SIZE = 32,
        MACH_SEGMENT_COMMAND = 0x19,
        MACH_SYMTAB_COMMAND = 0x2,
        MACH_SECTION_SIZE = 80,
        MACH_SYMBOL_SIZE = 16,
        MACH_RELOCATION_SIZE = 8,
    };
    u32 magic = 0;
    u32 cpu_type = 0;
    u32 file_type = 0;
    u32 command_count = 0;
    u32 commands_size = 0;
    if (!arena || bytes.length < MACH_HEADER_SIZE || !object_read_u32(bytes, 0, &magic) || magic != 0xfeedfacf || !object_read_u32(bytes, 4, &cpu_type) ||
        !object_read_u32(bytes, 12, &file_type) || file_type != 1 || !object_read_u32(bytes, 16, &command_count) ||
        !object_read_u32(bytes, 20, &commands_size) || commands_size > bytes.length - MACH_HEADER_SIZE ||
        (cpu_type == 0x01000007 && target.cpu_arch != CPU_ARCH_X86_64) || (cpu_type == 0x0100000c && target.cpu_arch != CPU_ARCH_AARCH64) ||
        (cpu_type != 0x01000007 && cpu_type != 0x0100000c))
    {
        return result;
    }
    u32 mach_section_count = 0;
    u32 symbol_offset = 0;
    u32 symbol_count = 0;
    u32 string_offset = 0;
    u32 string_size = 0;
    u64 command = MACH_HEADER_SIZE;
    for (u32 command_index = 0; command_index < command_count; command_index += 1)
    {
        u32 kind = 0;
        u32 size = 0;
        if (!object_read_u32(bytes, command, &kind) || !object_read_u32(bytes, command + 4, &size) || size < 8 || command > bytes.length ||
            size > bytes.length - command)
        {
            return result;
        }
        if (kind == MACH_SEGMENT_COMMAND)
        {
            u32 section_count = 0;
            if (size < 72 || !object_read_u32(bytes, command + 64, &section_count) || (u64)section_count * MACH_SECTION_SIZE > size - 72 ||
                section_count > UINT32_MAX - mach_section_count)
            {
                return result;
            }
            mach_section_count += section_count;
        }
        else if (kind == MACH_SYMTAB_COMMAND)
        {
            if (size < 24 || symbol_offset || !object_read_u32(bytes, command + 8, &symbol_offset) || !object_read_u32(bytes, command + 12, &symbol_count) ||
                !object_read_u32(bytes, command + 16, &string_offset) || !object_read_u32(bytes, command + 20, &string_size))
            {
                return result;
            }
        }
        command += size;
    }
    if (!mach_section_count || !symbol_offset || symbol_offset > bytes.length || (u64)symbol_count * MACH_SYMBOL_SIZE > bytes.length - symbol_offset ||
        string_offset > bytes.length || string_size > bytes.length - string_offset)
    {
        return result;
    }
    u64* mach_sections = arena_allocate(arena, u64, mach_section_count);
    u32* section_kinds = arena_allocate(arena, u32, mach_section_count);
    u64* section_bases = arena_allocate(arena, u64, mach_section_count);
    u64* section_addresses = arena_allocate(arena, u64, mach_section_count);
    u64 section_sizes[OBJECT_SECTION_COUNT] = {0};
    u32 section_alignments[OBJECT_SECTION_COUNT];
    for (u32 alignment_kind = 0; alignment_kind < OBJECT_SECTION_COUNT; alignment_kind += 1)
    {
        section_alignments[alignment_kind] = 1;
    }
    u32 relocation_capacity = 0;
    u32 output_section_index = 0;
    command = MACH_HEADER_SIZE;
    for (u32 command_index = 0; command_index < command_count; command_index += 1)
    {
        u32 kind = 0;
        u32 size = 0;
        object_read_u32(bytes, command, &kind);
        object_read_u32(bytes, command + 4, &size);
        if (kind == MACH_SEGMENT_COMMAND)
        {
            u32 section_count = 0;
            object_read_u32(bytes, command + 64, &section_count);
            for (u32 section_index = 0; section_index < section_count; section_index += 1)
            {
                u64 section = command + 72 + (u64)section_index * MACH_SECTION_SIZE;
                u64 address = 0;
                u64 section_size = 0;
                u32 offset = 0;
                u32 alignment_power = 0;
                u32 relocation_offset = 0;
                u32 relocation_count = 0;
                u32 flags = 0;
                if (!object_read_u64(bytes, section + 32, &address) || !object_read_u64(bytes, section + 40, &section_size) ||
                    !object_read_u32(bytes, section + 48, &offset) || !object_read_u32(bytes, section + 52, &alignment_power) ||
                    !object_read_u32(bytes, section + 56, &relocation_offset) || !object_read_u32(bytes, section + 60, &relocation_count) ||
                    !object_read_u32(bytes, section + 64, &flags) || alignment_power > 31 ||
                    (relocation_count &&
                     (relocation_offset > bytes.length || (u64)relocation_count * MACH_RELOCATION_SIZE > bytes.length - relocation_offset)) ||
                    relocation_count > UINT32_MAX - relocation_capacity)
                {
                    return result;
                }
                u32 section_type = flags & 0xff;
                bool zero_fill = section_type == 1 || section_type == 0x12;
                if (!zero_fill && (offset > bytes.length || section_size > bytes.length - offset))
                {
                    return result;
                }
                String8 name = {
                    .pointer = (char8*)bytes.pointer + section,
                    .length = 0,
                };
                while (name.length < 16 && name.pointer[name.length])
                {
                    name.length += 1;
                }
                bool is_thread_local = section_type == 0x11 || section_type == 0x12 || string_starts_with_sequence(name, S8("__thread"));
                // The __DWARF sections must keep their debug identity so that
                // their contents and relocations do not land in read-only data.
                ObjectSectionKind debug_kind = object_debug_section_kind_from_name(name);
                ObjectSectionKind output_kind = debug_kind != OBJECT_SECTION_COUNT ? debug_kind
                                                : is_thread_local ? (zero_fill ? OBJECT_SECTION_THREAD_LOCAL_ZERO : OBJECT_SECTION_THREAD_LOCAL_DATA)
                                                : (flags & 0x80000000) || string_equal(name, S8("__text")) ? OBJECT_SECTION_TEXT
                                                : string_starts_with_sequence(name, S8("__data")) || string_starts_with_sequence(name, S8("__bss"))
                                                    ? OBJECT_SECTION_DATA
                                                    : OBJECT_SECTION_READ_ONLY_DATA;
                u32 alignment = 1u << alignment_power;
                u64 base = align_forward(section_sizes[output_kind], alignment);
                if (base < section_sizes[output_kind] || section_size > UINT64_MAX - base)
                {
                    return result;
                }
                mach_sections[output_section_index] = section;
                section_kinds[output_section_index] = (u32)output_kind;
                section_bases[output_section_index] = base;
                section_addresses[output_section_index] = address;
                section_sizes[output_kind] = base + section_size;
                section_alignments[output_kind] = BUSTER_MAX(section_alignments[output_kind], alignment);
                relocation_capacity += relocation_count;
                output_section_index += 1;
            }
        }
        command += size;
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        bool zero_fill = kind == OBJECT_SECTION_THREAD_LOCAL_ZERO;
        result.sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .data =
                {
                    .pointer = zero_fill ? 0 : arena_allocate(arena, u8, section_sizes[kind]),
                    .length = zero_fill ? 0 : section_sizes[kind],
                },
            .virtual_size = section_sizes[kind],
            .kind = (ObjectSectionKind)kind,
            .alignment = section_alignments[kind],
        };
    }
    for (u32 section_index = 0; section_index < mach_section_count; section_index += 1)
    {
        u64 section = mach_sections[section_index];
        u64 size = 0;
        u32 offset = 0;
        u32 flags = 0;
        object_read_u64(bytes, section + 40, &size);
        object_read_u32(bytes, section + 48, &offset);
        object_read_u32(bytes, section + 64, &flags);
        u32 section_type = flags & 0xff;
        if (section_type != 1 && section_type != 0x12 && size)
        {
            ObjectSectionKind kind = (ObjectSectionKind)section_kinds[section_index];
            memcpy(result.sections[kind].data.pointer + section_bases[section_index], bytes.pointer + offset, size);
        }
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_count + mach_section_count);
    u32* symbol_map = arena_allocate(arena, u32, symbol_count);
    for (u32 source_index = 0; source_index < symbol_count; source_index += 1)
    {
        symbol_map[source_index] = UINT32_MAX;
        u64 source = symbol_offset + (u64)source_index * MACH_SYMBOL_SIZE;
        u32 name_offset = 0;
        u64 value = 0;
        if (!object_read_u32(bytes, source, &name_offset) || !object_read_u64(bytes, source + 8, &value))
        {
            return result;
        }
        u8 symbol_type = bytes.pointer[source + 4];
        u8 section_number = bytes.pointer[source + 5];
        if (symbol_type & 0xe0)
        {
            continue;
        }
        u8 kind = symbol_type & 0x0e;
        if ((kind != 0 && kind != 0x0e) || (kind == 0x0e && (!section_number || section_number > mach_section_count)))
        {
            continue;
        }
        String8 name = object_read_string(bytes, string_offset, string_size, name_offset);
        if (name.length && name.pointer[0] == '_')
        {
            name.pointer += 1;
            name.length -= 1;
        }
        if (!name.length)
        {
            name = string_format(arena, S8(".Lmach.{u32}"), source_index);
        }
        u32 destination_index = result.symbol_count++;
        u64 section_value = 0;
        if (kind == 0x0e)
        {
            u32 section_index = section_number - 1;
            if (value < section_addresses[section_index])
            {
                return result;
            }
            section_value = section_bases[section_index] + value - section_addresses[section_index];
        }
        result.symbols[destination_index] = (ObjectSymbol){
            .name = string_duplicate_arena(arena, name, false),
            .value = section_value,
            .section = kind == 0x0e ? section_kinds[section_number - 1] : OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_DATA,
            .global = (symbol_type & 1) != 0,
        };
        symbol_map[source_index] = destination_index;
    }
    u32* section_symbol_maps = arena_allocate(arena, u32, mach_section_count);
    for (u32 section_index = 0; section_index < mach_section_count; section_index += 1)
    {
        section_symbol_maps[section_index] = UINT32_MAX;
    }
    result.relocations = arena_allocate(arena, ObjectRelocation, relocation_capacity);
    for (u32 section_index = 0; section_index < mach_section_count; section_index += 1)
    {
        u64 section = mach_sections[section_index];
        u64 section_size = 0;
        u32 raw_offset = 0;
        u32 relocation_offset = 0;
        u32 relocation_count = 0;
        object_read_u64(bytes, section + 40, &section_size);
        object_read_u32(bytes, section + 48, &raw_offset);
        object_read_u32(bytes, section + 56, &relocation_offset);
        object_read_u32(bytes, section + 60, &relocation_count);
        for (u32 relocation_index = 0; relocation_index < relocation_count; relocation_index += 1)
        {
            u64 relocation = relocation_offset + (u64)relocation_index * MACH_RELOCATION_SIZE;
            u32 source_offset_u32 = 0;
            u32 information = 0;
            object_read_u32(bytes, relocation, &source_offset_u32);
            object_read_u32(bytes, relocation + 4, &information);
            s32 source_offset = (s32)source_offset_u32;
            u32 source_symbol = information & 0x00ffffff;
            bool external = (information & (1u << 27)) != 0;
            u32 relocation_type = information >> 28;
            u32 length = (information >> 25) & 0x3;
            if (source_offset < 0 || (u64)source_offset > section_size)
            {
                return result;
            }
            u32 destination_symbol = UINT32_MAX;
            if (external)
            {
                if (source_symbol >= symbol_count || symbol_map[source_symbol] == UINT32_MAX)
                {
                    return result;
                }
                destination_symbol = symbol_map[source_symbol];
            }
            else
            {
                if (!source_symbol || source_symbol > mach_section_count)
                {
                    return result;
                }
                u32 referenced_section = source_symbol - 1;
                destination_symbol = section_symbol_maps[referenced_section];
                if (destination_symbol == UINT32_MAX)
                {
                    destination_symbol = result.symbol_count++;
                    section_symbol_maps[referenced_section] = destination_symbol;
                    result.symbols[destination_symbol] = (ObjectSymbol){
                        .name = string_format(arena, S8(".Lmach_section.{u32}"), referenced_section),
                        .section = section_kinds[referenced_section],
                        .kind = OBJECT_SYMBOL_DATA,
                    };
                }
            }
            ObjectRelocationKind output_kind = OBJECT_RELOCATION_COUNT;
            s64 addend = 0;
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                if (relocation_type == 0 && length == 3)
                {
                    u64 stored = 0;
                    if (!object_read_u64(bytes, (u64)raw_offset + (u32)source_offset, &stored))
                    {
                        return result;
                    }
                    output_kind = OBJECT_RELOCATION_ABSOLUTE64;
                    addend = (s64)stored;
                }
                else if (relocation_type == 0 && length == 2)
                {
                    u32 stored = 0;
                    if (!object_read_u32(bytes, (u64)raw_offset + (u32)source_offset, &stored))
                    {
                        return result;
                    }
                    output_kind = OBJECT_RELOCATION_ABSOLUTE32;
                    addend = (s64)stored;
                }
                else if ((relocation_type == 1 || relocation_type == 2 || (relocation_type >= 6 && relocation_type <= 8) || relocation_type == 9) &&
                         length == 2)
                {
                    u32 stored = 0;
                    if (!object_read_u32(bytes, (u64)raw_offset + (u32)source_offset, &stored))
                    {
                        return result;
                    }
                    output_kind = relocation_type == 9 ? OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 : OBJECT_RELOCATION_X86_64_PC32;
                    addend = (s64)(s32)stored - 4;
                    if (relocation_type == 2)
                    {
                        result.symbols[destination_symbol].kind = OBJECT_SYMBOL_FUNCTION;
                    }
                }
            }
            else
            {
                output_kind = relocation_type == 0 && length == 3   ? OBJECT_RELOCATION_ABSOLUTE64
                              : relocation_type == 0 && length == 2 ? OBJECT_RELOCATION_ABSOLUTE32
                              : relocation_type == 2 && length == 2 ? OBJECT_RELOCATION_AARCH64_CALL26
                              : relocation_type == 8 && length == 2 ? OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21
                              : relocation_type == 9 && length == 2 ? OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12
                                                                    : OBJECT_RELOCATION_COUNT;
                if (relocation_type == 0 && length == 3)
                {
                    u64 stored = 0;
                    if (!object_read_u64(bytes, (u64)raw_offset + (u32)source_offset, &stored))
                    {
                        return result;
                    }
                    addend = (s64)stored;
                }
                if (relocation_type == 0 && length == 2)
                {
                    u32 stored = 0;
                    if (!object_read_u32(bytes, (u64)raw_offset + (u32)source_offset, &stored))
                    {
                        return result;
                    }
                    addend = (s64)stored;
                }
                if (relocation_type == 2 && length == 2)
                {
                    result.symbols[destination_symbol].kind = OBJECT_SYMBOL_FUNCTION;
                }
            }
            if (output_kind == OBJECT_RELOCATION_COUNT)
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
            result.relocations[result.relocation_count++] = (ObjectRelocation){
                .addend = addend,
                .offset = section_bases[section_index] + (u32)source_offset,
                .section = section_kinds[section_index],
                .symbol = destination_symbol,
                .kind = output_kind,
            };
        }
    }
    result.error = OBJECT_ERROR_NONE;
    return result;
}

ObjectFile object_read(Arena* arena, ByteSlice bytes, Target target)
{
    if (bytes.length && !bytes.pointer)
    {
        return (ObjectFile){
            .target = target,
            .error = OBJECT_ERROR_INVALID_INPUT,
        };
    }
    if (bytes.length >= 4 && memcmp(bytes.pointer,
                                    "\x7f"
                                    "ELF",
                                    4) == 0)
    {
        return object_read_elf64(arena, bytes, target);
    }
    if (bytes.length >= 2)
    {
        u16 machine = 0;
        memcpy(&machine, bytes.pointer, sizeof(machine));
        if (machine == 0x8664 || machine == 0xaa64)
        {
            return object_read_coff(arena, bytes, target);
        }
    }
    if (bytes.length >= 4)
    {
        u32 magic = 0;
        memcpy(&magic, bytes.pointer, sizeof(magic));
        if (magic == 0xfeedfacf)
        {
            return object_read_mach_o64(arena, bytes, target);
        }
    }
    return (ObjectFile){
        .target = target,
        .error = OBJECT_ERROR_UNSUPPORTED_TARGET,
    };
}

BUSTER_GLOBAL_LOCAL bool object_archive_decimal(u8 const* bytes, u64 length, u64* value)
{
    u64 result = 0;
    bool digit_found = false;
    for (u64 index = 0; index < length; index += 1)
    {
        u8 byte = bytes[index];
        if (byte == ' ')
        {
            continue;
        }
        if (byte < '0' || byte > '9' || result > (UINT64_MAX - (byte - '0')) / 10)
        {
            return false;
        }
        result = result * 10 + (byte - '0');
        digit_found = true;
    }
    if (!digit_found)
    {
        return false;
    }
    *value = result;
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_bytes_are_object(ByteSlice bytes)
{
    if (bytes.length >= 4 && (memcmp(bytes.pointer,
                                     "\x7f"
                                     "ELF",
                                     4) == 0 ||
                              memcmp(bytes.pointer, "\xcf\xfa\xed\xfe", 4) == 0))
    {
        return true;
    }
    if (bytes.length >= 2)
    {
        u16 machine = 0;
        memcpy(&machine, bytes.pointer, sizeof(machine));
        return machine == 0x8664 || machine == 0xaa64;
    }
    return false;
}

ObjectArchive object_archive_read(Arena* arena, ByteSlice bytes, Target target)
{
    ObjectArchive result = {
        .error = OBJECT_ERROR_INVALID_INPUT,
    };
    static char const archive_magic[] = "!<arch>\n";
    if (!arena || bytes.length < sizeof(archive_magic) - 1 || memcmp(bytes.pointer, archive_magic, sizeof(archive_magic) - 1) != 0)
    {
        return result;
    }
    u32 member_capacity = (u32)BUSTER_MIN(bytes.length / 60, (u64)UINT32_MAX);
    result.objects = arena_allocate(arena, ObjectFile, member_capacity);
    result.member_names = arena_allocate(arena, String8, member_capacity);
    String8 long_names = {0};
    u64 cursor = sizeof(archive_magic) - 1;
    while (cursor < bytes.length)
    {
        if (cursor > bytes.length || 60 > bytes.length - cursor || bytes.pointer[cursor + 58] != '`' || bytes.pointer[cursor + 59] != '\n')
        {
            return result;
        }
        u64 member_size = 0;
        if (!object_archive_decimal(bytes.pointer + cursor + 48, 10, &member_size))
        {
            return result;
        }
        u64 member_offset = cursor + 60;
        if (member_offset > bytes.length || member_size > bytes.length - member_offset)
        {
            return result;
        }
        String8 raw_name = {
            .pointer = (char8*)bytes.pointer + cursor,
            .length = 16,
        };
        while (raw_name.length && raw_name.pointer[raw_name.length - 1] == ' ')
        {
            raw_name.length -= 1;
        }
        String8 member_name = raw_name;
        u64 object_offset = member_offset;
        u64 object_size = member_size;
        bool metadata = false;
        if (string_equal(raw_name, S8("/")) || string_equal(raw_name, S8("__.SYMDEF")) || string_equal(raw_name, S8("__.SYMDEF SORTED")))
        {
            metadata = true;
        }
        else if (string_equal(raw_name, S8("//")))
        {
            long_names = (String8){
                .pointer = (char8*)bytes.pointer + member_offset,
                .length = member_size,
            };
            metadata = true;
        }
        else if (string_starts_with_sequence(raw_name, S8("#1/")))
        {
            u64 name_length = 0;
            if (!object_archive_decimal((u8*)raw_name.pointer + 3, raw_name.length - 3, &name_length) || name_length > object_size)
            {
                return result;
            }
            member_name = (String8){
                .pointer = (char8*)bytes.pointer + object_offset,
                .length = name_length,
            };
            object_offset += name_length;
            object_size -= name_length;
        }
        else if (raw_name.length > 1 && raw_name.pointer[0] == '/')
        {
            u64 name_offset = 0;
            if (!long_names.pointer || !object_archive_decimal((u8*)raw_name.pointer + 1, raw_name.length - 1, &name_offset) ||
                name_offset >= long_names.length)
            {
                return result;
            }
            u64 name_length = 0;
            while (name_offset + name_length < long_names.length && long_names.pointer[name_offset + name_length] != '\n' &&
                   long_names.pointer[name_offset + name_length] != 0)
            {
                name_length += 1;
            }
            if (name_length && long_names.pointer[name_offset + name_length - 1] == '/')
            {
                name_length -= 1;
            }
            member_name = (String8){
                .pointer = long_names.pointer + name_offset,
                .length = name_length,
            };
        }
        else if (member_name.length && member_name.pointer[member_name.length - 1] == '/')
        {
            member_name.length -= 1;
        }
        ByteSlice object_bytes = {
            .pointer = bytes.pointer + object_offset,
            .length = object_size,
        };
        if (!metadata && object_bytes_are_object(object_bytes))
        {
            ObjectFile object = object_read(arena, object_bytes, target);
            if (object.error != OBJECT_ERROR_NONE || result.object_count == member_capacity)
            {
                result.error = object.error;
                return result;
            }
            result.objects[result.object_count] = object;
            result.member_names[result.object_count] = string_duplicate_arena(arena, member_name, false);
            result.object_count += 1;
        }
        cursor = member_offset + member_size;
        if (cursor & 1)
        {
            cursor += 1;
        }
    }
    if (cursor != bytes.length)
    {
        return result;
    }
    result.error = OBJECT_ERROR_NONE;
    return result;
}

BUSTER_GLOBAL_LOCAL AnalysisEntity* object_entity_find(AnalysisResult* analysis, AnalysisEntityId entity)
{
    if (entity.module.value == analysis->module.id.value && entity.index.value < analysis->module.entity_count)
    {
        return analysis->module.entities + entity.index.value;
    }
    for (u32 import_index = 0; import_index < analysis->module.import_count; import_index += 1)
    {
        AnalysisResult* imported = analysis->module.imports[import_index].target;
        if (imported && imported->module.id.value == entity.module.value && entity.index.value < imported->module.entity_count)
        {
            return imported->module.entities + entity.index.value;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL String8 object_entity_name(Arena* arena, AnalysisResult* analysis, AnalysisEntityId entity, AnalysisInstantiationId instantiation)
{
    AnalysisEntity* definition = object_entity_find(analysis, entity);
    if (definition && definition->kind == ANALYSIS_ENTITY_CODE && definition->ast.code->exported)
    {
        return definition->name;
    }
    String8 name = definition ? definition->name : S8("external");
    return string_format(arena, S8("_B{u64}_{u64}_{u64}_{S8}"), (u64)entity.module.value, (u64)entity.index.value, (u64)instantiation.value, name);
}

bool object_section_kind_is_debug(ObjectSectionKind kind)
{
    return kind == OBJECT_SECTION_DEBUG_INFO || kind == OBJECT_SECTION_DEBUG_ABBREV || kind == OBJECT_SECTION_DEBUG_LINE ||
           kind == OBJECT_SECTION_DEBUG_STR || kind == OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS || kind == OBJECT_SECTION_DEBUG_CODEVIEW_TYPES;
}

BUSTER_GLOBAL_LOCAL void object_debug_sections_initialize(ObjectFile* object)
{
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        if (!object_section_kind_is_debug((ObjectSectionKind)kind))
        {
            continue;
        }
        object->sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .kind = (ObjectSectionKind)kind,
            .alignment = object_section_default_alignment((ObjectSectionKind)kind),
        };
    }
}

// Appends the built CodeView sections. Relocations resolve against the
// per-entry function symbols, which both module object builders place at
// symbol indices [0, entry_count).
BUSTER_GLOBAL_LOCAL void object_append_codeview(ObjectFile* object, CodeviewResult built)
{
    if (!built.valid)
    {
        return;
    }
    object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data = built.symbols;
    object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data = built.types;
    object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].alignment = 4;
    object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].alignment = 4;
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        CodeviewRelocation relocation = built.relocations[relocation_index];
        object->relocations[object->relocation_count++] = (ObjectRelocation){
            .offset = relocation.offset,
            .section = OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
            .symbol = relocation.function,
            .kind = relocation.kind == CODEVIEW_RELOCATION_SECREL32 ? OBJECT_RELOCATION_COFF_SECREL32 : OBJECT_RELOCATION_COFF_SECTION16,
        };
    }
}

BUSTER_GLOBAL_LOCAL ObjectSectionKind object_section_kind_for_dwarf(DwarfSectionKind kind)
{
    switch (kind)
    {
    case DWARF_SECTION_INFO:
        return OBJECT_SECTION_DEBUG_INFO;
    case DWARF_SECTION_ABBREV:
        return OBJECT_SECTION_DEBUG_ABBREV;
    case DWARF_SECTION_LINE:
        return OBJECT_SECTION_DEBUG_LINE;
    case DWARF_SECTION_STR:
        return OBJECT_SECTION_DEBUG_STR;
    case DWARF_SECTION_COUNT:
        break;
    }
    return OBJECT_SECTION_COUNT;
}

// Appends the built DWARF sections plus local base symbols carrying the
// relocations: 64-bit address slots resolve against the text base and 32-bit
// slots hold offsets into a sibling debug section that shift when objects are
// concatenated. Symbol and relocation arrays must have been allocated with
// room for OBJECT_DWARF_EXTRA_SYMBOLS and built.relocation_count extra
// entries.
enum
{
    OBJECT_DWARF_EXTRA_SYMBOLS = 1 + DWARF_SECTION_COUNT,
};

BUSTER_GLOBAL_LOCAL void object_append_dwarf(ObjectFile* object, DwarfResult built)
{
    if (!built.valid)
    {
        return;
    }
    for (u32 kind = 0; kind < DWARF_SECTION_COUNT; kind += 1)
    {
        object->sections[object_section_kind_for_dwarf((DwarfSectionKind)kind)].data = built.sections[kind];
    }
    u32 text_symbol = object->symbol_count++;
    object->symbols[text_symbol] = (ObjectSymbol){
        .name = S8(".text"),
        .section = OBJECT_SECTION_TEXT,
        .kind = OBJECT_SYMBOL_FUNCTION,
    };
    u32 debug_symbols[DWARF_SECTION_COUNT];
    for (u32 kind = 0; kind < DWARF_SECTION_COUNT; kind += 1)
    {
        ObjectSectionKind section_kind = object_section_kind_for_dwarf((DwarfSectionKind)kind);
        debug_symbols[kind] = object->symbol_count++;
        object->symbols[debug_symbols[kind]] = (ObjectSymbol){
            .name = object->sections[section_kind].name,
            .section = (u32)section_kind,
            .kind = OBJECT_SYMBOL_DATA,
        };
    }
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        DwarfRelocation relocation = built.relocations[relocation_index];
        object->relocations[object->relocation_count++] = (ObjectRelocation){
            .addend = relocation.addend,
            .offset = relocation.offset,
            .section = (u32)object_section_kind_for_dwarf(relocation.section),
            .symbol = relocation.address ? text_symbol : debug_symbols[relocation.target],
            .kind = relocation.address ? OBJECT_RELOCATION_ABSOLUTE64 : OBJECT_RELOCATION_ABSOLUTE32,
        };
    }
}

ObjectFile object_from_codegen_module(Arena* arena, AnalysisResult* analysis, CodegenModule* module, Target target)
{
    ObjectFile result = {
        .target = target,
    };
    if (!arena || !analysis || !module || module->error != CODEGEN_ERROR_NONE || (target.cpu_arch != CPU_ARCH_X86_64 && target.cpu_arch != CPU_ARCH_AARCH64))
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    u8* text = arena_allocate(arena, u8, module->code.length);
    if (module->code.length)
    {
        memcpy(text, module->code.pointer, module->code.length);
    }
    u32 read_only_alignment = 16;
    u32 writable_alignment = 16;
    u32 thread_local_alignment = 16;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        CodegenModuleGlobal global = module->globals[global_index];
        if (global.is_thread_local)
        {
            thread_local_alignment = BUSTER_MAX(thread_local_alignment, global.alignment);
        }
        else if (global.read_only)
        {
            read_only_alignment = BUSTER_MAX(read_only_alignment, global.alignment);
        }
        else
        {
            writable_alignment = BUSTER_MAX(writable_alignment, global.alignment);
        }
    }
    result.sections[OBJECT_SECTION_TEXT] = (ObjectSection){
        .name = S8(".text"),
        .data =
            {
                .pointer = text,
                .length = module->code.length,
            },
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 16,
    };
    result.sections[OBJECT_SECTION_READ_ONLY_DATA] = (ObjectSection){
        .name = S8(".rodata"),
        .data =
            {
                .pointer = module->read_only_data.pointer,
                .length = module->read_only_data.length,
            },
        .kind = OBJECT_SECTION_READ_ONLY_DATA,
        .alignment = read_only_alignment,
    };
    result.sections[OBJECT_SECTION_DATA] = (ObjectSection){
        .name = S8(".data"),
        .kind = OBJECT_SECTION_DATA,
        .alignment = writable_alignment,
    };
    result.sections[OBJECT_SECTION_THREAD_LOCAL_DATA] = (ObjectSection){
        .name = S8(".tdata"),
        .kind = OBJECT_SECTION_THREAD_LOCAL_DATA,
        .alignment = thread_local_alignment,
    };
    result.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO] = (ObjectSection){
        .name = S8(".tbss"),
        .kind = OBJECT_SECTION_THREAD_LOCAL_ZERO,
        .alignment = thread_local_alignment,
    };
    object_debug_sections_initialize(&result);
    DwarfResult dwarf = {0};
    CodeviewResult codeview = {0};
    if (module->debug_info && module->entry_count && analysis->module.source_count)
    {
        String8* file_paths = arena_allocate(arena, String8, analysis->module.source_count);
        for (u32 source_index = 0; source_index < analysis->module.source_count; source_index += 1)
        {
            file_paths[source_index] = analysis->module.sources[source_index].path;
        }
        DwarfFunction* functions = arena_allocate(arena, DwarfFunction, module->entry_count);
        for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
        {
            CodegenModuleEntry* entry = module->entries + entry_index;
            u64 end = entry_index + 1 < module->entry_count ? module->entries[entry_index + 1].offset : module->code.length;
            AnalysisEntity* definition = object_entity_find(analysis, entry->entity);
            functions[entry_index] = (DwarfFunction){
                .name = definition ? definition->name : object_entity_name(arena, analysis, entry->entity, entry->instantiation),
                .code_offset = entry->offset,
                .code_size = (u32)(end - entry->offset),
                .file = definition && definition->source.value < analysis->module.source_count ? definition->source.value : 0,
                .line = definition ? definition->range.line : 0,
            };
        }
        DwarfLineEntry* lines = arena_allocate(arena, DwarfLineEntry, module->line_entry_count);
        u32 function_cursor = 0;
        for (u32 line_index = 0; line_index < module->line_entry_count; line_index += 1)
        {
            CodegenLineEntry entry = module->line_entries[line_index];
            while (function_cursor + 1 < module->entry_count && entry.code_offset >= module->entries[function_cursor + 1].offset)
            {
                function_cursor += 1;
            }
            lines[line_index] = (DwarfLineEntry){
                .code_offset = entry.code_offset,
                .file = functions[function_cursor].file,
                .line = entry.line,
                .column = entry.column,
            };
        }
        if (target.os == OPERATING_SYSTEM_WINDOWS)
        {
            codeview = codeview_build(arena, (CodeviewInput){
                                                 .producer = S8("buster"),
                                                 .file_paths = file_paths,
                                                 .functions = functions,
                                                 .lines = lines,
                                                 .file_count = analysis->module.source_count,
                                                 .function_count = module->entry_count,
                                                 .line_count = module->line_entry_count,
                                                 .machine = target.cpu_arch == CPU_ARCH_AARCH64 ? CODEVIEW_MACHINE_ARM64 : CODEVIEW_MACHINE_X64,
                                             });
        }
        else
        {
            dwarf = dwarf_build(arena, (DwarfInput){
                                           .producer = S8("buster"),
                                           .comp_dir = S8("."),
                                           .file_paths = file_paths,
                                           .functions = functions,
                                           .lines = lines,
                                           .code_size = module->code.length,
                                           .file_count = analysis->module.source_count,
                                           .function_count = module->entry_count,
                                           .line_count = module->line_entry_count,
                                           .language = 0x000c,
                                       });
        }
    }
    u32 dwarf_relocation_count = (dwarf.valid ? dwarf.relocation_count : 0) + (codeview.valid ? codeview.relocation_count : 0);
    u32 symbol_capacity =
        module->entry_count + module->relocation_count + (module->data_relocation_count ? 1 : 0) + (dwarf.valid ? OBJECT_DWARF_EXTRA_SYMBOLS : 0);
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_capacity);
    for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
    {
        CodegenModuleEntry* entry = module->entries + entry_index;
        u64 end = entry_index + 1 < module->entry_count ? module->entries[entry_index + 1].offset : module->code.length;
        result.symbols[result.symbol_count++] = (ObjectSymbol){
            .name = object_entity_name(arena, analysis, entry->entity, entry->instantiation),
            .value = entry->offset,
            .size = end - entry->offset,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        };
    }
    result.relocations = arena_allocate(arena, ObjectRelocation, module->relocation_count + dwarf_relocation_count);
    for (u32 relocation_index = 0; relocation_index < module->relocation_count; relocation_index += 1)
    {
        CodegenModuleRelocation* source = module->relocations + relocation_index;
        u32 symbol_index = UINT32_MAX;
        for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
        {
            CodegenModuleEntry* entry = module->entries + entry_index;
            if (entry->entity.module.value == source->entity.module.value && entry->entity.index.value == source->entity.index.value &&
                entry->instantiation.value == source->instantiation.value)
            {
                symbol_index = entry_index;
                break;
            }
        }
        if (symbol_index == UINT32_MAX)
        {
            String8 name = object_entity_name(arena, analysis, source->entity, source->instantiation);
            for (u32 existing = 0; existing < result.symbol_count; existing += 1)
            {
                if (result.symbols[existing].section == OBJECT_SECTION_UNDEFINED && string_equal(result.symbols[existing].name, name))
                {
                    symbol_index = existing;
                    break;
                }
            }
            if (symbol_index == UINT32_MAX)
            {
                symbol_index = result.symbol_count++;
                result.symbols[symbol_index] = (ObjectSymbol){
                    .name = name,
                    .section = OBJECT_SECTION_UNDEFINED,
                    .kind = OBJECT_SYMBOL_FUNCTION,
                    .global = true,
                };
            }
        }
        ObjectRelocationKind kind = source->absolute  ? OBJECT_RELOCATION_ABSOLUTE64
                                    : source->aarch64 ? OBJECT_RELOCATION_AARCH64_CALL26
                                                      : OBJECT_RELOCATION_X86_64_PC32;
        result.relocations[result.relocation_count++] = (ObjectRelocation){
            .addend = kind == OBJECT_RELOCATION_X86_64_PC32 ? -4 : 0,
            .offset = source->offset,
            .section = OBJECT_SECTION_TEXT,
            .symbol = symbol_index,
            .kind = kind,
        };
        if (source->absolute)
        {
            if (source->offset + 8 > module->code.length)
            {
                result.error = OBJECT_ERROR_INVALID_INPUT;
                return result;
            }
            memset(text + source->offset, 0, 8);
        }
        else if (source->aarch64)
        {
            if (source->offset + 4 > module->code.length)
            {
                result.error = OBJECT_ERROR_INVALID_INPUT;
                return result;
            }
            u32 instruction = 0x94000000;
            memcpy(text + source->offset, &instruction, sizeof(instruction));
        }
        else
        {
            if (source->offset + 4 > module->code.length)
            {
                result.error = OBJECT_ERROR_INVALID_INPUT;
                return result;
            }
            memset(text + source->offset, 0, 4);
        }
    }
    if (module->data_relocation_count)
    {
        u32 data_symbol = result.symbol_count++;
        result.symbols[data_symbol] = (ObjectSymbol){
            .name = S8("$rodata"),
            .size = module->read_only_data.length,
            .section = OBJECT_SECTION_READ_ONLY_DATA,
            .kind = OBJECT_SYMBOL_DATA,
        };
        ObjectRelocation* relocations = arena_allocate(arena, ObjectRelocation, module->relocation_count + module->data_relocation_count + dwarf_relocation_count);
        if (result.relocation_count)
        {
            memcpy(relocations, result.relocations, (u64)result.relocation_count * sizeof(*relocations));
        }
        result.relocations = relocations;
        for (u32 index = 0; index < module->data_relocation_count; index += 1)
        {
            CodegenModuleDataRelocation* source = module->data_relocations + index;
            ObjectRelocationKind kind = source->kind == CODEGEN_DATA_RELOCATION_X86_64_PC32 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_ABSOLUTE64;
            result.relocations[result.relocation_count++] = (ObjectRelocation){
                .addend = (s64)source->data_offset + (kind == OBJECT_RELOCATION_X86_64_PC32 ? -4 : 0),
                .offset = source->code_offset,
                .section = OBJECT_SECTION_TEXT,
                .symbol = data_symbol,
                .kind = kind,
            };
        }
    }
    object_append_dwarf(&result, dwarf);
    object_append_codeview(&result, codeview);
    return result;
}

ObjectFile object_from_canonical_codegen_module(Arena* arena, IrProgram* program, CodegenModule* module, Target target)
{
    ObjectFile result = {
        .target = target,
    };
    if (!arena || !program || !module || module->error != CODEGEN_ERROR_NONE || module->data_relocation_count ||
        (target.cpu_arch != CPU_ARCH_X86_64 && target.cpu_arch != CPU_ARCH_AARCH64))
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    u8* text = arena_allocate(arena, u8, module->code.length);
    if (module->code.length)
    {
        memcpy(text, module->code.pointer, module->code.length);
    }
    u32 read_only_alignment = 16;
    u32 writable_alignment = 16;
    u32 thread_local_alignment = 16;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        CodegenModuleGlobal global = module->globals[global_index];
        if (global.is_thread_local)
        {
            thread_local_alignment = BUSTER_MAX(thread_local_alignment, global.alignment);
        }
        else if (global.read_only)
        {
            read_only_alignment = BUSTER_MAX(read_only_alignment, global.alignment);
        }
        else
        {
            writable_alignment = BUSTER_MAX(writable_alignment, global.alignment);
        }
    }
    result.sections[OBJECT_SECTION_TEXT] = (ObjectSection){
        .name = S8(".text"),
        .data =
            {
                .pointer = text,
                .length = module->code.length,
            },
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 16,
    };
    result.sections[OBJECT_SECTION_READ_ONLY_DATA] = (ObjectSection){
        .name = S8(".rodata"),
        .data = module->read_only_data,
        .kind = OBJECT_SECTION_READ_ONLY_DATA,
        .alignment = read_only_alignment,
    };
    result.sections[OBJECT_SECTION_DATA] = (ObjectSection){
        .name = S8(".data"),
        .data = module->writable_data,
        .kind = OBJECT_SECTION_DATA,
        .alignment = writable_alignment,
    };
    result.sections[OBJECT_SECTION_THREAD_LOCAL_DATA] = (ObjectSection){
        .name = S8(".tdata"),
        .data = module->thread_local_data,
        .kind = OBJECT_SECTION_THREAD_LOCAL_DATA,
        .alignment = thread_local_alignment,
    };
    result.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO] = (ObjectSection){
        .name = S8(".tbss"),
        .virtual_size = module->thread_local_zero_size,
        .kind = OBJECT_SECTION_THREAD_LOCAL_ZERO,
        .alignment = thread_local_alignment,
    };
    object_debug_sections_initialize(&result);
    DwarfResult dwarf = {0};
    CodeviewResult codeview = {0};
    if (module->debug_info && module->entry_count && program->sources.count)
    {
        String8* file_paths = arena_allocate(arena, String8, program->sources.count);
        for (u32 source_index = 0; source_index < program->sources.count; source_index += 1)
        {
            file_paths[source_index] = program->sources.sources[source_index].path;
        }
        IrSourceRange* declaration_sources = arena_allocate(arena, IrSourceRange, program->symbols.count);
        memset(declaration_sources, 0, (u64)program->symbols.count * sizeof(*declaration_sources));
        for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
        {
            IrModule* ir_module = program->modules + module_index;
            for (u32 function_index = 0; function_index < ir_module->function_count; function_index += 1)
            {
                IrFunction* function = ir_module->functions + function_index;
                if (function->symbol.value < program->symbols.count)
                {
                    declaration_sources[function->symbol.value] = function->source;
                }
            }
        }
        DwarfFunction* functions = arena_allocate(arena, DwarfFunction, module->entry_count);
        for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
        {
            CodegenModuleEntry* entry = module->entries + entry_index;
            u64 end = entry_index + 1 < module->entry_count ? module->entries[entry_index + 1].offset : module->code.length;
            IrSymbol* entry_symbol = ir_symbol_from_id(&program->symbols, entry->symbol);
            IrSourceRange declaration = entry->symbol.value < program->symbols.count ? declaration_sources[entry->symbol.value] : (IrSourceRange){0};
            functions[entry_index] = (DwarfFunction){
                .name = entry_symbol ? entry_symbol->name : S8("unknown"),
                .code_offset = entry->offset,
                .code_size = (u32)(end - entry->offset),
                .file = declaration.source.value < program->sources.count ? declaration.source.value : 0,
                .line = declaration.line,
            };
        }
        DwarfLineEntry* lines = arena_allocate(arena, DwarfLineEntry, module->line_entry_count);
        for (u32 line_index = 0; line_index < module->line_entry_count; line_index += 1)
        {
            CodegenLineEntry entry = module->line_entries[line_index];
            lines[line_index] = (DwarfLineEntry){
                .code_offset = entry.code_offset,
                .file = entry.source < program->sources.count ? entry.source : 0,
                .line = entry.line,
                .column = entry.column,
            };
        }
        if (target.os == OPERATING_SYSTEM_WINDOWS)
        {
            codeview = codeview_build(arena, (CodeviewInput){
                                                 .producer = S8("buster"),
                                                 .file_paths = file_paths,
                                                 .functions = functions,
                                                 .lines = lines,
                                                 .file_count = program->sources.count,
                                                 .function_count = module->entry_count,
                                                 .line_count = module->line_entry_count,
                                                 .machine = target.cpu_arch == CPU_ARCH_AARCH64 ? CODEVIEW_MACHINE_ARM64 : CODEVIEW_MACHINE_X64,
                                             });
        }
        else
        {
            dwarf = dwarf_build(arena, (DwarfInput){
                                           .producer = S8("buster"),
                                           .comp_dir = S8("."),
                                           .file_paths = file_paths,
                                           .functions = functions,
                                           .lines = lines,
                                           .code_size = module->code.length,
                                           .file_count = program->sources.count,
                                           .function_count = module->entry_count,
                                           .line_count = module->line_entry_count,
                                           .language = 0x000c,
                                       });
        }
    }
    u32 dwarf_relocation_count = (dwarf.valid ? dwarf.relocation_count : 0) + (codeview.valid ? codeview.relocation_count : 0);
    bool apple_thread_local = false;
    if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
    {
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            apple_thread_local |= module->globals[global_index].is_thread_local;
        }
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, module->entry_count + module->global_count + module->relocation_count + (apple_thread_local ? 1 : 0) +
                                                             (dwarf.valid ? OBJECT_DWARF_EXTRA_SYMBOLS : 0));
    for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
    {
        CodegenModuleEntry entry = module->entries[entry_index];
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, entry.symbol);
        if (!symbol)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
        u64 end = entry_index + 1 < module->entry_count ? module->entries[entry_index + 1].offset : module->code.length;
        result.symbols[result.symbol_count++] = (ObjectSymbol){
            .name = symbol->link_name.length ? symbol->link_name : symbol->name,
            .value = entry.offset,
            .size = end - entry.offset,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = symbol->linkage != IR_LINKAGE_INTERNAL,
        };
    }
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        CodegenModuleGlobal global = module->globals[global_index];
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, global.symbol);
        if (!symbol || symbol->kind != IR_SYMBOL_DATA)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
        ByteSlice section_data =
            global.zero_fill ?
                (ByteSlice){
                    .length =
                        module->
                            thread_local_zero_size,
                } :
            global.is_thread_local ?
                module->thread_local_data :
            global.read_only ?
                module->read_only_data :
                module->writable_data;
        if ((u64)global.offset + global.size > section_data.length)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
        result.symbols[result.symbol_count++] = (ObjectSymbol){
            .name = symbol->link_name.length ? symbol->link_name : symbol->name,
            .value = global.offset,
            .size = global.size,
            .section = global.zero_fill         ? OBJECT_SECTION_THREAD_LOCAL_ZERO
                       : global.is_thread_local ? OBJECT_SECTION_THREAD_LOCAL_DATA
                       : global.read_only       ? OBJECT_SECTION_READ_ONLY_DATA
                                                : OBJECT_SECTION_DATA,
            .kind = OBJECT_SYMBOL_DATA,
            .global = symbol->linkage != IR_LINKAGE_INTERNAL,
        };
    }
    if (apple_thread_local)
    {
        result.symbols[result.symbol_count++] = (ObjectSymbol){
            .name = S8("_tlv_bootstrap"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        };
    }
    result.relocations = arena_allocate(arena, ObjectRelocation, module->relocation_count + dwarf_relocation_count);
    for (u32 relocation_index = 0; relocation_index < module->relocation_count; relocation_index += 1)
    {
        CodegenModuleRelocation source = module->relocations[relocation_index];
        IrSymbol* target_symbol = ir_symbol_from_id(&program->symbols, source.symbol);
        ByteSlice source_data = source.source == CODEGEN_MODULE_RELOCATION_CODE                ? module->code
                                : source.source == CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA    ? module->read_only_data
                                : source.source == CODEGEN_MODULE_RELOCATION_DATA              ? module->writable_data
                                : source.source == CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA ? module->thread_local_data
                                                                                               : (ByteSlice){0};
        if (!target_symbol || source.source >= CODEGEN_MODULE_RELOCATION_SOURCE_COUNT || source.offset >= source_data.length)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
        u32 symbol_index = UINT32_MAX;
        for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
        {
            if (module->entries[entry_index].symbol.value == source.symbol.value)
            {
                symbol_index = entry_index;
                break;
            }
        }
        String8 name = target_symbol->link_name.length ? target_symbol->link_name : target_symbol->name;
        if (symbol_index == UINT32_MAX)
        {
            for (u32 existing = 0; existing < result.symbol_count; existing += 1)
            {
                if (result.symbols[existing].section != OBJECT_SECTION_UNDEFINED && string_equal(result.symbols[existing].name, name))
                {
                    symbol_index = existing;
                    break;
                }
            }
        }
        if (symbol_index == UINT32_MAX)
        {
            for (u32 existing = 0; existing < result.symbol_count; existing += 1)
            {
                if (result.symbols[existing].section == OBJECT_SECTION_UNDEFINED && string_equal(result.symbols[existing].name, name))
                {
                    symbol_index = existing;
                    break;
                }
            }
        }
        if (symbol_index == UINT32_MAX)
        {
            symbol_index = result.symbol_count++;
            result.symbols[symbol_index] = (ObjectSymbol){
                .name = name,
                .section = OBJECT_SECTION_UNDEFINED,
                .kind = target_symbol->kind == IR_SYMBOL_DATA ? OBJECT_SYMBOL_DATA : OBJECT_SYMBOL_FUNCTION,
                .global = true,
            };
        }
        ObjectRelocationKind kind =
            source.thread_local_index && source.aarch64 && source.thread_local_low              ? OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12
            : source.thread_local_index && source.aarch64                                       ? OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP
            : source.thread_local_index                                                         ? OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32
            : source.is_thread_local && target.os == OPERATING_SYSTEM_WINDOWS && source.aarch64 ? OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12
            : source.is_thread_local && target.os == OPERATING_SYSTEM_WINDOWS                   ? OBJECT_RELOCATION_PE_TLS_OFFSET32
            : source.is_thread_local && (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS) && source.aarch64 && source.thread_local_low
                ? OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12
            : source.is_thread_local && (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS) && source.aarch64
                ? OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21
            : source.is_thread_local && (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS) ? OBJECT_RELOCATION_X86_64_MACH_TLV_PC32
            : source.is_thread_local && source.aarch64 && source.thread_local_low ? OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12
            : source.is_thread_local && source.aarch64                            ? OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12
            : source.is_thread_local                                              ? OBJECT_RELOCATION_X86_64_TPOFF32
            : source.absolute                                                     ? OBJECT_RELOCATION_ABSOLUTE64
            : source.aarch64                                                      ? OBJECT_RELOCATION_AARCH64_CALL26
                                                                                  : OBJECT_RELOCATION_X86_64_PC32;
        result.relocations[result.relocation_count++] = (ObjectRelocation){
            .addend = source.addend + (kind == OBJECT_RELOCATION_X86_64_PC32 || kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ||
                                               kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32
                                           ? -4
                                           : 0),
            .offset = source.offset,
            .section = source.source == CODEGEN_MODULE_RELOCATION_CODE                ? OBJECT_SECTION_TEXT
                       : source.source == CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA    ? OBJECT_SECTION_READ_ONLY_DATA
                       : source.source == CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA ? OBJECT_SECTION_THREAD_LOCAL_DATA
                                                                                      : OBJECT_SECTION_DATA,
            .symbol = symbol_index,
            .kind = kind,
        };
    }
    object_append_dwarf(&result, dwarf);
    object_append_codeview(&result, codeview);
    return result;
}

BUSTER_GLOBAL_LOCAL u32 object_elf_relocation_type(CpuArch arch, ObjectRelocationKind kind)
{
    if (arch == CPU_ARCH_X86_64)
    {
        return kind == OBJECT_RELOCATION_X86_64_PC32       ? 2
               : kind == OBJECT_RELOCATION_X86_64_TPOFF32  ? 23
               : kind == OBJECT_RELOCATION_ABSOLUTE64      ? 1
               : kind == OBJECT_RELOCATION_ABSOLUTE32      ? 10
                                                           : 0;
    }
    return kind == OBJECT_RELOCATION_AARCH64_CALL26                 ? 283
           : kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 ? 549
           : kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12 ? 551
           : kind == OBJECT_RELOCATION_ABSOLUTE64                   ? 257
           : kind == OBJECT_RELOCATION_ABSOLUTE32                   ? 258
                                                                    : 0;
}

BUSTER_GLOBAL_LOCAL ObjectArtifact object_write_elf64(Arena* arena, ObjectFile* object)
{
    ObjectArtifact result = {
        .format = OBJECT_FORMAT_ELF64,
    };
    u64 capacity = object_writer_capacity(object);
    ObjectBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
    };
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_SECTION_HEADER_SIZE = 64,
        ELF_SYMBOL_SIZE = 24,
        ELF_RELOCATION_SIZE = 24,
    };
    u32 relocation_section_count = 0;
    u32* relocation_targets = arena_allocate(arena, u32, object->section_count);
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
        {
            if (object->relocations[relocation].section == section)
            {
                relocation_targets[relocation_section_count++] = section;
                break;
            }
        }
    }
    u32 relocation_section = object->section_count + 1;
    u32 section_count = object->section_count + relocation_section_count + 4;
    u32 symbol_section = relocation_section + relocation_section_count;
    u32 string_section = symbol_section + 1;
    u32 section_string_section = string_section + 1;
    u32* symbol_order = arena_allocate(arena, u32, object->symbol_count);
    u32* symbol_indices = arena_allocate(arena, u32, object->symbol_count);
    u32 ordered_symbol_count = 0;
    for (u32 pass = 0; pass < 2; pass += 1)
    {
        bool global = pass != 0;
        for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
        {
            if (object->symbols[symbol].global != global)
            {
                continue;
            }
            symbol_order[ordered_symbol_count] = symbol;
            symbol_indices[symbol] = ordered_symbol_count + 1;
            ordered_symbol_count += 1;
        }
    }
    object_buffer_zero(&buffer, ELF_HEADER_SIZE);
    u64* section_offsets = arena_allocate(arena, u64, section_count);
    u64* section_sizes = arena_allocate(arena, u64, section_count);
    u32* section_name_offsets = arena_allocate(arena, u32, section_count);
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        object_buffer_align(&buffer, object->sections[section].alignment);
        section_offsets[section + 1] = buffer.count;
        object_buffer_write(&buffer, object->sections[section].data.pointer, object->sections[section].data.length);
        section_sizes[section + 1] = BUSTER_MAX(object->sections[section].data.length, object->sections[section].virtual_size);
    }
    for (u32 relocation_section_index = 0; relocation_section_index < relocation_section_count; relocation_section_index += 1)
    {
        u32 target = relocation_targets[relocation_section_index];
        u32 output_section = relocation_section + relocation_section_index;
        object_buffer_align(&buffer, 8);
        section_offsets[output_section] = buffer.count;
        for (u32 index = 0; index < object->relocation_count; index += 1)
        {
            ObjectRelocation* relocation = object->relocations + index;
            if (relocation->section != target)
            {
                continue;
            }
            u32 type = object_elf_relocation_type(object->target.cpu_arch, relocation->kind);
            if (!type)
            {
                buffer.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                break;
            }
            u64 offset = buffer.count;
            object_buffer_zero(&buffer, ELF_RELOCATION_SIZE);
            object_write_u64_at(&buffer, offset, relocation->offset);
            object_write_u64_at(&buffer, offset + 8, ((u64)symbol_indices[relocation->symbol] << 32) | type);
            object_write_s64_at(&buffer, offset + 16, relocation->addend);
        }
        section_sizes[output_section] = buffer.count - section_offsets[output_section];
    }
    object_buffer_align(&buffer, 8);
    section_offsets[symbol_section] = buffer.count;
    object_buffer_zero(&buffer, ELF_SYMBOL_SIZE);
    u64 symbol_table_offset = buffer.count;
    object_buffer_zero(&buffer, (u64)object->symbol_count * ELF_SYMBOL_SIZE);
    section_sizes[symbol_section] = buffer.count - section_offsets[symbol_section];
    section_offsets[string_section] = buffer.count;
    u8 zero = 0;
    object_buffer_write(&buffer, &zero, 1);
    u32* symbol_name_offsets = arena_allocate(arena, u32, object->symbol_count);
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        symbol_name_offsets[symbol] = (u32)(buffer.count - section_offsets[string_section]);
        object_buffer_write(&buffer, object->symbols[symbol].name.pointer, object->symbols[symbol].name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    section_sizes[string_section] = buffer.count - section_offsets[string_section];
    section_offsets[section_string_section] = buffer.count;
    object_buffer_write(&buffer, &zero, 1);
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        section_name_offsets[section + 1] = (u32)(buffer.count - section_offsets[section_string_section]);
        object_buffer_write(&buffer, object->sections[section].name.pointer, object->sections[section].name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    for (u32 index = 0; index < relocation_section_count; index += 1)
    {
        u32 section = relocation_section + index;
        section_name_offsets[section] = (u32)(buffer.count - section_offsets[section_string_section]);
        String8 name = string_format(arena, S8(".rela{S8}"), object->sections[relocation_targets[index]].name);
        object_buffer_write(&buffer, name.pointer, name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    String8 generated_names[] = {
        S8_INITIALIZER(".symtab"),
        S8_INITIALIZER(".strtab"),
        S8_INITIALIZER(".shstrtab"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(generated_names); index += 1)
    {
        u32 section = symbol_section + index;
        section_name_offsets[section] = (u32)(buffer.count - section_offsets[section_string_section]);
        object_buffer_write(&buffer, generated_names[index].pointer, generated_names[index].length);
        object_buffer_write(&buffer, &zero, 1);
    }
    section_sizes[section_string_section] = buffer.count - section_offsets[section_string_section];
    for (u32 ordered_symbol = 0; ordered_symbol < object->symbol_count; ordered_symbol += 1)
    {
        u32 symbol = symbol_order[ordered_symbol];
        ObjectSymbol* source = object->symbols + symbol;
        u64 offset = symbol_table_offset + (u64)ordered_symbol * ELF_SYMBOL_SIZE;
        object_write_u32_at(&buffer, offset, symbol_name_offsets[symbol]);
        bool is_thread_local = source->section < object->section_count && (object->sections[source->section].kind == OBJECT_SECTION_THREAD_LOCAL_DATA ||
                                                                           object->sections[source->section].kind == OBJECT_SECTION_THREAD_LOCAL_ZERO);
        buffer.bytes[offset + 4] = (u8)((source->global ? 0x10 : 0) | (is_thread_local ? 6 : source->kind == OBJECT_SYMBOL_FUNCTION ? 2 : 1));
        buffer.bytes[offset + 5] = 0;
        object_write_u16_at(&buffer, offset + 6, source->section == OBJECT_SECTION_UNDEFINED ? 0 : (u16)(source->section + 1));
        object_write_u64_at(&buffer, offset + 8, source->value);
        object_write_u64_at(&buffer, offset + 16, source->size);
    }
    object_buffer_align(&buffer, 8);
    u64 section_header_offset = buffer.count;
    object_buffer_zero(&buffer, (u64)section_count * ELF_SECTION_HEADER_SIZE);
    for (u32 section = 1; section < section_count; section += 1)
    {
        u64 offset = section_header_offset + (u64)section * ELF_SECTION_HEADER_SIZE;
        object_write_u32_at(&buffer, offset, section_name_offsets[section]);
        u32 type = 1;
        u64 flags = 0;
        u64 alignment = 1;
        u64 entry_size = 0;
        u32 link = 0;
        u32 info = 0;
        if (section <= object->section_count)
        {
            ObjectSection* source = object->sections + section - 1;
            if (source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO)
            {
                type = 8;
            }
            flags = source->kind == OBJECT_SECTION_TEXT                                                                    ? 0x6
                    : source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA || source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? 0x403
                    : source->kind == OBJECT_SECTION_DATA                                                                  ? 0x3
                    : object_section_kind_is_debug(source->kind)                                                           ? 0x0
                                                                                                                           : 0x2;
            alignment = source->alignment;
        }
        else if (section >= relocation_section && section < symbol_section)
        {
            type = 4;
            alignment = 8;
            entry_size = ELF_RELOCATION_SIZE;
            link = symbol_section;
            info = relocation_targets[section - relocation_section] + 1;
        }
        else if (section == symbol_section)
        {
            type = 2;
            alignment = 8;
            entry_size = ELF_SYMBOL_SIZE;
            link = string_section;
            info = 1;
            for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
            {
                if (!object->symbols[symbol].global)
                {
                    info += 1;
                }
            }
        }
        else if (section == string_section || section == section_string_section)
        {
            type = 3;
        }
        object_write_u32_at(&buffer, offset + 4, type);
        object_write_u64_at(&buffer, offset + 8, flags);
        object_write_u64_at(&buffer, offset + 24, section_offsets[section]);
        object_write_u64_at(&buffer, offset + 32, section_sizes[section]);
        object_write_u32_at(&buffer, offset + 40, link);
        object_write_u32_at(&buffer, offset + 44, info);
        object_write_u64_at(&buffer, offset + 48, alignment);
        object_write_u64_at(&buffer, offset + 56, entry_size);
    }
    u8 identity[16] = {
        0x7f, 'E', 'L', 'F', 2, 1, 1, 0,
    };
    memcpy(buffer.bytes, identity, sizeof(identity));
    object_write_u16_at(&buffer, 16, 1);
    object_write_u16_at(&buffer, 18, object->target.cpu_arch == CPU_ARCH_X86_64 ? 62 : 183);
    object_write_u32_at(&buffer, 20, 1);
    object_write_u64_at(&buffer, 40, section_header_offset);
    object_write_u16_at(&buffer, 52, ELF_HEADER_SIZE);
    object_write_u16_at(&buffer, 58, ELF_SECTION_HEADER_SIZE);
    object_write_u16_at(&buffer, 60, (u16)section_count);
    object_write_u16_at(&buffer, 62, (u16)section_string_section);
    result.bytes = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.error = buffer.error;
    return result;
}

BUSTER_GLOBAL_LOCAL u16 object_coff_relocation_type(CpuArch arch, ObjectRelocationKind kind)
{
    if (arch == CPU_ARCH_X86_64)
    {
        return kind == OBJECT_RELOCATION_X86_64_PC32                ? 0x0004
               : kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ? 0x0004
               : kind == OBJECT_RELOCATION_PE_TLS_OFFSET32          ? 0x000b
               : kind == OBJECT_RELOCATION_ABSOLUTE64               ? 0x0001
               : kind == OBJECT_RELOCATION_ABSOLUTE32               ? 0x0002
               : kind == OBJECT_RELOCATION_COFF_SECREL32            ? 0x000b
               : kind == OBJECT_RELOCATION_COFF_SECTION16           ? 0x000a
                                                                    : 0;
    }
    return kind == OBJECT_RELOCATION_AARCH64_CALL26              ? 0x0003
           : kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP ? 0x0004
           : kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12 ? 0x0007
           : kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12   ? 0x000f
           : kind == OBJECT_RELOCATION_ABSOLUTE64                ? 0x000e
           : kind == OBJECT_RELOCATION_ABSOLUTE32                ? 0x0001
           : kind == OBJECT_RELOCATION_COFF_SECREL32             ? 0x0008
           : kind == OBJECT_RELOCATION_COFF_SECTION16            ? 0x0007
                                                                 : 0;
}

BUSTER_GLOBAL_LOCAL void object_coff_name_write(ObjectBuffer* buffer, u64 offset, String8 name, u32 string_offset)
{
    if (name.length <= 8)
    {
        if (name.length)
        {
            memcpy(buffer->bytes + offset, name.pointer, name.length);
        }
    }
    else
    {
        object_write_u32_at(buffer, offset, 0);
        object_write_u32_at(buffer, offset + 4, string_offset);
    }
}

BUSTER_GLOBAL_LOCAL ObjectArtifact object_write_coff(Arena* arena, ObjectFile* object)
{
    ObjectArtifact result = {
        .format = OBJECT_FORMAT_COFF,
    };
    u64 capacity = object_writer_capacity(object);
    ObjectBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
    };
    enum
    {
        COFF_HEADER_SIZE = 20,
        COFF_SECTION_SIZE = 40,
        COFF_RELOCATION_SIZE = 10,
        COFF_SYMBOL_SIZE = 18,
    };
    u32 section_count = object->section_count;
    object_buffer_zero(&buffer, COFF_HEADER_SIZE + (u64)section_count * COFF_SECTION_SIZE);
    u32* raw_offsets = arena_allocate(arena, u32, section_count);
    u32* relocation_offsets = arena_allocate(arena, u32, section_count);
    u16* relocation_counts = arena_allocate(arena, u16, section_count);
    memset(relocation_counts, 0, (u64)section_count * sizeof(*relocation_counts));
    for (u32 section = 0; section < section_count; section += 1)
    {
        object_buffer_align(&buffer, 4);
        raw_offsets[section] = (u32)buffer.count;
        object_buffer_write(&buffer, object->sections[section].data.pointer, object->sections[section].data.length);
        for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
        {
            ObjectRelocation* source = object->relocations + relocation;
            if (source->section != section)
            {
                continue;
            }
            s64 addend = source->addend;
            if (source->kind == OBJECT_RELOCATION_X86_64_PC32 || source->kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32)
            {
                addend += 4;
            }
            if (source->kind == OBJECT_RELOCATION_ABSOLUTE64)
            {
                object_write_s64_at(&buffer, raw_offsets[section] + source->offset, addend);
            }
            else if (addend)
            {
                object_write_u32_at(&buffer, raw_offsets[section] + source->offset, (u32)addend);
            }
        }
        relocation_offsets[section] = (u32)buffer.count;
        for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
        {
            ObjectRelocation* source = object->relocations + relocation;
            if (source->section != section)
            {
                continue;
            }
            u16 type = object_coff_relocation_type(object->target.cpu_arch, source->kind);
            if (!type || relocation_counts[section] == UINT16_MAX)
            {
                buffer.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                break;
            }
            u64 offset = buffer.count;
            object_buffer_zero(&buffer, COFF_RELOCATION_SIZE);
            object_write_u32_at(&buffer, offset, (u32)source->offset);
            object_write_u32_at(&buffer, offset + 4, source->symbol);
            object_write_u16_at(&buffer, offset + 8, type);
            relocation_counts[section] += 1;
        }
    }
    u32 symbol_table_offset = (u32)buffer.count;
    u64 symbols_offset = buffer.count;
    object_buffer_zero(&buffer, (u64)object->symbol_count * COFF_SYMBOL_SIZE);
    u32 string_table_offset = (u32)buffer.count;
    object_buffer_zero(&buffer, 4);
    u32* string_offsets = arena_allocate(arena, u32, object->symbol_count);
    u32* section_name_offsets = arena_allocate(arena, u32, section_count);
    u8 zero = 0;
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        if (object->symbols[symbol].name.length <= 8)
        {
            continue;
        }
        string_offsets[symbol] = (u32)(buffer.count - string_table_offset);
        object_buffer_write(&buffer, object->symbols[symbol].name.pointer, object->symbols[symbol].name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    for (u32 section = 0; section < section_count; section += 1)
    {
        if (object->sections[section].name.length <= 8)
        {
            continue;
        }
        section_name_offsets[section] = (u32)(buffer.count - string_table_offset);
        object_buffer_write(&buffer, object->sections[section].name.pointer, object->sections[section].name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    object_write_u32_at(&buffer, string_table_offset, (u32)(buffer.count - string_table_offset));
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        ObjectSymbol* source = object->symbols + symbol;
        u64 offset = symbols_offset + (u64)symbol * COFF_SYMBOL_SIZE;
        object_coff_name_write(&buffer, offset, source->name, string_offsets[symbol]);
        object_write_u32_at(&buffer, offset + 8, (u32)source->value);
        object_write_u16_at(&buffer, offset + 12, source->section == OBJECT_SECTION_UNDEFINED ? 0 : (u16)(source->section + 1));
        object_write_u16_at(&buffer, offset + 14, source->kind == OBJECT_SYMBOL_FUNCTION ? 0x20 : 0);
        buffer.bytes[offset + 16] = source->global ? 2 : 3;
        buffer.bytes[offset + 17] = 0;
    }
    object_write_u16_at(&buffer, 0, object->target.cpu_arch == CPU_ARCH_X86_64 ? 0x8664 : 0xaa64);
    object_write_u16_at(&buffer, 2, (u16)section_count);
    object_write_u32_at(&buffer, 8, symbol_table_offset);
    object_write_u32_at(&buffer, 12, object->symbol_count);
    for (u32 section = 0; section < section_count; section += 1)
    {
        ObjectSection* source = object->sections + section;
        u64 offset = COFF_HEADER_SIZE + (u64)section * COFF_SECTION_SIZE;
        if (source->name.length > 8)
        {
            // Names longer than the inline field live in the string table and
            // are referenced as "/<decimal offset>".
            String8 reference = string_format(arena, S8("/{u32}"), section_name_offsets[section]);
            memcpy(buffer.bytes + offset, reference.pointer, BUSTER_MIN(reference.length, 8));
        }
        else if (source->name.length)
        {
            memcpy(buffer.bytes + offset, source->name.pointer, source->name.length);
        }
        object_write_u32_at(&buffer, offset + 16, (u32)source->data.length);
        object_write_u32_at(&buffer, offset + 20, raw_offsets[section]);
        object_write_u32_at(&buffer, offset + 24, relocation_counts[section] ? relocation_offsets[section] : 0);
        object_write_u16_at(&buffer, offset + 32, relocation_counts[section]);
        u32 characteristics = source->kind == OBJECT_SECTION_TEXT             ? 0x60500020
                              : source->kind == OBJECT_SECTION_READ_ONLY_DATA ? 0x40500040
                              : object_section_kind_is_debug(source->kind)    ? 0x42100040
                                                                              : 0xc0500040;
        object_write_u32_at(&buffer, offset + 36, characteristics);
    }
    result.bytes = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.error = buffer.error;
    return result;
}

BUSTER_GLOBAL_LOCAL u32 object_mach_relocation_type(CpuArch arch, ObjectRelocationKind kind)
{
    if (kind == OBJECT_RELOCATION_ABSOLUTE64 || kind == OBJECT_RELOCATION_ABSOLUTE32)
    {
        return 0;
    }
    if (arch == CPU_ARCH_X86_64 && (kind == OBJECT_RELOCATION_X86_64_PC32 || kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32))
    {
        return kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 ? 9 : 2;
    }
    if (arch == CPU_ARCH_AARCH64)
    {
        return kind == OBJECT_RELOCATION_AARCH64_CALL26                ? 2
               : kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21    ? 8
               : kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 ? 9
                                                                       : UINT32_MAX;
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL void object_mach_name_write(u8* destination, u64 capacity, String8 name)
{
    u64 length = BUSTER_MIN(name.length, capacity);
    if (length)
    {
        memcpy(destination, name.pointer, length);
    }
}

BUSTER_GLOBAL_LOCAL ObjectArtifact object_write_mach_o64(Arena* arena, ObjectFile* object)
{
    ObjectArtifact result = {
        .format = OBJECT_FORMAT_MACH_O64,
    };
    u64 capacity = object_writer_capacity(object);
    ObjectBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
    };
    enum
    {
        MACH_HEADER_SIZE = 32,
        MACH_SEGMENT_COMMAND_SIZE = 72,
        MACH_SECTION_SIZE = 80,
        MACH_SYMTAB_COMMAND_SIZE = 24,
        MACH_RELOCATION_SIZE = 8,
        MACH_SYMBOL_SIZE = 16,
    };
    u32 section_count = object->section_count;
    u32 segment_size = MACH_SEGMENT_COMMAND_SIZE + section_count * MACH_SECTION_SIZE;
    u32 commands_size = segment_size + MACH_SYMTAB_COMMAND_SIZE;
    object_buffer_zero(&buffer, MACH_HEADER_SIZE + commands_size);
    u32* section_offsets = arena_allocate(arena, u32, section_count);
    u64* section_addresses = arena_allocate(arena, u64, section_count);
    u32* relocation_offsets = arena_allocate(arena, u32, section_count);
    u32* relocation_counts = arena_allocate(arena, u32, section_count);
    memset(relocation_counts, 0, (u64)section_count * sizeof(*relocation_counts));
    u64 segment_virtual_size = 0;
    for (u32 section = 0; section < section_count; section += 1)
    {
        u64 alignment = object->sections[section].alignment;
        u64 effective_alignment = alignment ? alignment : 1;
        segment_virtual_size = (segment_virtual_size + effective_alignment - 1) & ~(effective_alignment - 1);
        section_addresses[section] = segment_virtual_size;
        segment_virtual_size +=
            object->sections[section].kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? object->sections[section].virtual_size : object->sections[section].data.length;
        object_buffer_align(&buffer, object->sections[section].alignment);
        section_offsets[section] = (u32)buffer.count;
        object_buffer_write(&buffer, object->sections[section].data.pointer, object->sections[section].data.length);
    }
    for (u32 section = 0; section < section_count; section += 1)
    {
        object_buffer_align(&buffer, 4);
        relocation_offsets[section] = (u32)buffer.count;
        for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
        {
            ObjectRelocation* source = object->relocations + relocation;
            if (source->section != section)
            {
                continue;
            }
            s64 addend = source->addend;
            if (source->kind == OBJECT_RELOCATION_X86_64_PC32)
            {
                addend += 4;
            }
            if (source->kind == OBJECT_RELOCATION_ABSOLUTE64)
            {
                object_write_s64_at(&buffer, section_offsets[section] + source->offset, addend);
            }
            else if (addend)
            {
                object_write_u32_at(&buffer, section_offsets[section] + source->offset, (u32)addend);
            }
            u32 type = object_mach_relocation_type(object->target.cpu_arch, source->kind);
            if (type == UINT32_MAX)
            {
                buffer.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                break;
            }
            u64 offset = buffer.count;
            object_buffer_zero(&buffer, MACH_RELOCATION_SIZE);
            object_write_u32_at(&buffer, offset, (u32)source->offset);
            bool pc_relative = source->kind != OBJECT_RELOCATION_ABSOLUTE64 && source->kind != OBJECT_RELOCATION_ABSOLUTE32 &&
                               source->kind != OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12;
            u32 word = (source->symbol & 0x00ffffff) | (pc_relative ? 1u << 24 : 0) | ((source->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 3u : 2u) << 25) |
                       (1u << 27) | (type << 28);
            object_write_u32_at(&buffer, offset + 4, word);
            relocation_counts[section] += 1;
        }
    }
    object_buffer_align(&buffer, 8);
    u32 symbol_offset = (u32)buffer.count;
    object_buffer_zero(&buffer, (u64)object->symbol_count * MACH_SYMBOL_SIZE);
    u32 string_offset = (u32)buffer.count;
    u8 zero = 0;
    object_buffer_write(&buffer, &zero, 1);
    u32* symbol_name_offsets = arena_allocate(arena, u32, object->symbol_count);
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        symbol_name_offsets[symbol] = (u32)(buffer.count - string_offset);
        object_buffer_write(&buffer, "_", 1);
        object_buffer_write(&buffer, object->symbols[symbol].name.pointer, object->symbols[symbol].name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        ObjectSymbol* source = object->symbols + symbol;
        u64 offset = symbol_offset + (u64)symbol * MACH_SYMBOL_SIZE;
        object_write_u32_at(&buffer, offset, symbol_name_offsets[symbol]);
        buffer.bytes[offset + 4] = source->section == OBJECT_SECTION_UNDEFINED ? 0x01 : (u8)(0x0e | (source->global ? 1 : 0));
        buffer.bytes[offset + 5] = source->section == OBJECT_SECTION_UNDEFINED ? 0 : (u8)(source->section + 1);
        object_write_u64_at(&buffer, offset + 8, source->value + (source->section == OBJECT_SECTION_UNDEFINED ? 0 : section_addresses[source->section]));
    }
    object_write_u32_at(&buffer, 0, 0xfeedfacf);
    object_write_u32_at(&buffer, 4, object->target.cpu_arch == CPU_ARCH_X86_64 ? 0x01000007 : 0x0100000c);
    object_write_u32_at(&buffer, 8, object->target.cpu_arch == CPU_ARCH_X86_64 ? 3 : 0);
    object_write_u32_at(&buffer, 12, 1);
    object_write_u32_at(&buffer, 16, 2);
    object_write_u32_at(&buffer, 20, commands_size);
    u64 segment_offset = MACH_HEADER_SIZE;
    object_write_u32_at(&buffer, segment_offset, 0x19);
    object_write_u32_at(&buffer, segment_offset + 4, segment_size);
    object_write_u32_at(&buffer, segment_offset + 64, section_count);
    u64 segment_file_offset = section_count ? section_offsets[0] : 0;
    u64 segment_file_end = segment_file_offset;
    for (u32 section = 0; section < section_count; section += 1)
    {
        segment_file_end = BUSTER_MAX(segment_file_end, (u64)section_offsets[section] + object->sections[section].data.length);
    }
    u64 segment_file_size = segment_file_end - segment_file_offset;
    object_write_u64_at(&buffer, segment_offset + 32, segment_virtual_size);
    object_write_u64_at(&buffer, segment_offset + 40, segment_file_offset);
    object_write_u64_at(&buffer, segment_offset + 48, segment_file_size);
    for (u32 section = 0; section < section_count; section += 1)
    {
        ObjectSection* source = object->sections + section;
        u64 offset = segment_offset + MACH_SEGMENT_COMMAND_SIZE + (u64)section * MACH_SECTION_SIZE;
        String8 section_name = source->kind == OBJECT_SECTION_TEXT                ? S8("__text")
                               : source->kind == OBJECT_SECTION_READ_ONLY_DATA    ? S8("__const")
                               : source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA ? S8("__thread_data")
                               : source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? S8("__thread_bss")
                               : source->kind == OBJECT_SECTION_DEBUG_INFO        ? S8("__debug_info")
                               : source->kind == OBJECT_SECTION_DEBUG_ABBREV     ? S8("__debug_abbrev")
                               : source->kind == OBJECT_SECTION_DEBUG_LINE       ? S8("__debug_line")
                               : source->kind == OBJECT_SECTION_DEBUG_STR        ? S8("__debug_str")
                                                                                  : S8("__data");
        String8 segment_name =
            (source->kind == OBJECT_SECTION_DATA || source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA || source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO)
                ? S8("__DATA")
            : object_section_kind_is_debug(source->kind) ? S8("__DWARF")
                                                         : S8("__TEXT");
        object_mach_name_write(buffer.bytes + offset, 16, section_name);
        object_mach_name_write(buffer.bytes + offset + 16, 16, segment_name);
        object_write_u64_at(&buffer, offset + 32, section_addresses[section]);
        object_write_u64_at(&buffer, offset + 40, source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? source->virtual_size : source->data.length);
        object_write_u32_at(&buffer, offset + 48, source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? 0 : section_offsets[section]);
        u32 alignment = 0;
        u32 value = source->alignment;
        while (value > 1)
        {
            alignment += 1;
            value >>= 1;
        }
        object_write_u32_at(&buffer, offset + 52, alignment);
        object_write_u32_at(&buffer, offset + 56, relocation_counts[section] ? relocation_offsets[section] : 0);
        object_write_u32_at(&buffer, offset + 60, relocation_counts[section]);
        object_write_u32_at(&buffer, offset + 64,
                            source->kind == OBJECT_SECTION_TEXT                ? 0x80000400
                            : source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA ? 0x11
                            : source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? 0x12
                            : object_section_kind_is_debug(source->kind)       ? 0x02000000
                                                                               : 0);
    }
    u64 symtab_command = segment_offset + segment_size;
    object_write_u32_at(&buffer, symtab_command, 0x2);
    object_write_u32_at(&buffer, symtab_command + 4, MACH_SYMTAB_COMMAND_SIZE);
    object_write_u32_at(&buffer, symtab_command + 8, symbol_offset);
    object_write_u32_at(&buffer, symtab_command + 12, object->symbol_count);
    object_write_u32_at(&buffer, symtab_command + 16, string_offset);
    object_write_u32_at(&buffer, symtab_command + 20, (u32)(buffer.count - string_offset));
    result.bytes = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.error = buffer.error;
    return result;
}

ObjectArtifact object_write(Arena* arena, ObjectFile* object, ObjectFormat format)
{
    ObjectArtifact result = {
        .format = format,
        .error = OBJECT_ERROR_INVALID_INPUT,
    };
    if (!arena || !object || object->error != OBJECT_ERROR_NONE || format >= OBJECT_FORMAT_COUNT)
    {
        return result;
    }
    if ((object->section_count && !object->sections) || (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations) ||
        (object->target.cpu_arch != CPU_ARCH_X86_64 && object->target.cpu_arch != CPU_ARCH_AARCH64))
    {
        return result;
    }
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        ObjectSection* source = object->sections + section;
        if ((source->data.length && !source->data.pointer) || (source->alignment && (source->alignment & (source->alignment - 1))))
        {
            return result;
        }
    }
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        if (object->symbols[symbol].section != OBJECT_SECTION_UNDEFINED && object->symbols[symbol].section >= object->section_count)
        {
            return result;
        }
    }
    for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
    {
        ObjectRelocation* source = object->relocations + relocation;
        u64 relocation_size = source->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (source->section >= object->section_count || source->symbol >= object->symbol_count || source->kind >= OBJECT_RELOCATION_COUNT ||
            source->offset + relocation_size > object->sections[source->section].data.length)
        {
            return result;
        }
    }
    if (format == OBJECT_FORMAT_ELF64)
    {
        return object_write_elf64(arena, object);
    }
    if (format == OBJECT_FORMAT_COFF)
    {
        return object_write_coff(arena, object);
    }
    return object_write_mach_o64(arena, object);
}

ObjectExecutable object_link_executable(ObjectFile* object)
{
    ObjectExecutable result = {0};
    if (!object || object->error != OBJECT_ERROR_NONE || object->section_count <= OBJECT_SECTION_TEXT || object->section_count > OBJECT_SECTION_COUNT)
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 image_size = 0;
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        u64 alignment = object->sections[section].alignment;
        if (!alignment)
        {
            alignment = 1;
        }
        image_size = (image_size + alignment - 1) & ~(alignment - 1);
        section_offsets[section] = image_size;
        image_size += object->sections[section].data.length;
    }
    u64 page_size = os_get_page_size();
    u64 allocation_size = (image_size + page_size - 1) & ~(page_size - 1);
    void* address = os_reserve(0, allocation_size, (ProtectionFlags){.read = 1, .write = 1}, (MapFlags){.priv = 1, .anonymous = 1});
    if (!address)
    {
        result.error = OBJECT_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        if (object->sections[section].data.length)
        {
            memcpy((u8*)address + section_offsets[section], object->sections[section].data.pointer, object->sections[section].data.length);
        }
    }
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = object->relocations + index;
        if (relocation->section < object->section_count && object_section_kind_is_debug(object->sections[relocation->section].kind))
        {
            continue;
        }
        if (relocation->section != OBJECT_SECTION_TEXT || relocation->symbol >= object->symbol_count)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        ObjectSymbol* symbol = object->symbols + relocation->symbol;
        if (symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            result.error = OBJECT_ERROR_UNRESOLVED_SYMBOL;
            break;
        }
        u8* patch = (u8*)address + relocation->offset;
        if (symbol->section >= object->section_count)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        u8* target = (u8*)address + section_offsets[symbol->section] + symbol->value;
        if (relocation->kind == OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 displacement = target - (patch + 4) + relocation->addend + 4;
            if (displacement < INT32_MIN || displacement > INT32_MAX)
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
            s32 value = (s32)displacement;
            memcpy(patch, &value, sizeof(value));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26)
        {
            s64 displacement = target - patch + relocation->addend;
            s64 words = displacement / 4;
            if (displacement % 4 || words < -(1 << 25) || words >= (1 << 25))
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
            u32 instruction = 0x94000000 | ((u32)words & 0x03ffffff);
            memcpy(patch, &instruction, sizeof(instruction));
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            u64 value = (u64)(uintptr_t)target + (u64)relocation->addend;
            memcpy(patch, &value, sizeof(value));
        }
        else
        {
            result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
            break;
        }
    }
    if (result.error != OBJECT_ERROR_NONE)
    {
        os_unreserve(address, allocation_size);
        return result;
    }
    if (!os_commit(address, allocation_size,
                   (ProtectionFlags){
                       .read = 1,
                       .execute = 1,
                   },
                   false) ||
        !os_flush_instruction_cache(address, image_size))
    {
        os_unreserve(address, allocation_size);
        result.error = OBJECT_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    result.address = (u8*)address + section_offsets[OBJECT_SECTION_TEXT];
    result.allocation_size = allocation_size;
    return result;
}

void object_release_executable(ObjectExecutable executable)
{
    if (executable.address && executable.allocation_size)
    {
        os_unreserve(executable.address, executable.allocation_size);
    }
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool object_bytes_contain(ByteSlice bytes, String8 value)
{
    if (value.length > bytes.length)
    {
        return false;
    }
    for (u64 index = 0; index + value.length <= bytes.length; index += 1)
    {
        if (memcmp(bytes.pointer + index, value.pointer, value.length) == 0)
        {
            return true;
        }
    }
    return false;
}

UnitTestResult object_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};
    u8 x86_text[] = {
        0xe8, 0, 0, 0, 0, 0xc3, 0xb8, 42, 0, 0, 0, 0xc3,
    };
    u8 aarch64_text[] = {
        0xfd, 0x7b, 0xbf, 0xa9, 0x00, 0x00, 0x00, 0x94, 0xfd, 0x7b, 0xc1, 0xa8, 0xc0, 0x03, 0x5f, 0xd6, 0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6,
    };
    u8 read_only_data[] = {
        'h', 'e', 'l', 'l', 'o', 0,
    };
    u8 writable_data[8] = {0};
    ObjectSection sections[] = {
        {
            .name = S8(".text"),
            .data = BUSTER_ARRAY_TO_SLICE(x86_text),
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 16,
        },
        {
            .name = S8(".rodata"),
            .data = BUSTER_ARRAY_TO_SLICE(read_only_data),
            .kind = OBJECT_SECTION_READ_ONLY_DATA,
            .alignment = 1,
        },
        {
            .name = S8(".data"),
            .data = BUSTER_ARRAY_TO_SLICE(writable_data),
            .kind = OBJECT_SECTION_DATA,
            .alignment = 8,
        },
    };
    ObjectSymbol symbols[] = {
        {
            .name = S8("object_entry"),
            .size = 6,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("object_callee"),
            .value = 6,
            .size = 6,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("object_message"),
            .size = sizeof(read_only_data),
            .section = OBJECT_SECTION_READ_ONLY_DATA,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
        {
            .name = S8("external_function"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation relocation = {
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile object = {
        .sections = sections,
        .symbols = symbols,
        .relocations = &relocation,
        .target =
            {
                .cpu_arch = CPU_ARCH_X86_64,
                .os = OPERATING_SYSTEM_LINUX,
            },
        .section_count = BUSTER_ARRAY_LENGTH(sections),
        .symbol_count = BUSTER_ARRAY_LENGTH(symbols),
        .relocation_count = 1,
    };
    ObjectFormat formats[] = {
        OBJECT_FORMAT_ELF64,
        OBJECT_FORMAT_COFF,
        OBJECT_FORMAT_MACH_O64,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(formats); index += 1)
    {
        ObjectArtifact artifact = object_write(arguments->arena, &object, formats[index]);
        BUSTER_TEST(arguments, artifact.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, artifact.bytes.length > 64);
        BUSTER_TEST(arguments, object_bytes_contain(artifact.bytes, S8("object_entry")));
        BUSTER_TEST(arguments, object_bytes_contain(artifact.bytes, S8("external_function")));
        BUSTER_TEST(arguments, object_bytes_contain(artifact.bytes, S8("hello")));
    }
    ObjectRelocation section_relocations[] = {
        relocation,
        {
            .addend = 2,
            .offset = 0,
            .section = OBJECT_SECTION_DATA,
            .symbol = 2,
            .kind = OBJECT_RELOCATION_ABSOLUTE64,
        },
    };
    ObjectFile section_relocation_object = object;
    section_relocation_object.relocations = section_relocations;
    section_relocation_object.relocation_count = BUSTER_ARRAY_LENGTH(section_relocations);
    ObjectArtifact section_relocation_elf = object_write(arguments->arena, &section_relocation_object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, section_relocation_elf.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, object_bytes_contain(section_relocation_elf.bytes, S8(".rela.data")));
    ObjectArtifact elf = object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64);
    bool elf_magic_matches = false;
    if (elf.bytes.pointer && elf.bytes.length >= 4)
    {
        elf_magic_matches = elf.bytes.pointer[0] == 0x7f && elf.bytes.pointer[1] == 'E' && elf.bytes.pointer[2] == 'L' && elf.bytes.pointer[3] == 'F';
    }
    BUSTER_TEST(arguments, elf_magic_matches);
    ObjectFile elf_roundtrip = object_read(arguments->arena, elf.bytes, object.target);
    BUSTER_TEST(arguments, elf_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, elf_roundtrip.section_count == OBJECT_SECTION_COUNT);
    BUSTER_TEST(arguments, elf_roundtrip.symbol_count == object.symbol_count);
    BUSTER_TEST(arguments, elf_roundtrip.relocation_count == object.relocation_count);
    bool elf_sections_valid =
        elf_roundtrip.sections && elf_roundtrip.section_count > OBJECT_SECTION_TEXT && elf_roundtrip.sections[OBJECT_SECTION_TEXT].data.pointer;
    BUSTER_TEST(arguments, elf_sections_valid);
    if (elf_sections_valid)
    {
        BUSTER_TEST(arguments, elf_roundtrip.sections[OBJECT_SECTION_TEXT].data.length == sizeof(x86_text));
        BUSTER_TEST(arguments, memcmp(elf_roundtrip.sections[OBJECT_SECTION_TEXT].data.pointer, x86_text, sizeof(x86_text)) == 0);
    }
    bool elf_relocation_valid = elf_roundtrip.relocations && elf_roundtrip.relocation_count && elf_roundtrip.symbols &&
                                elf_roundtrip.relocations[0].symbol < elf_roundtrip.symbol_count;
    BUSTER_TEST(arguments, elf_relocation_valid);
    if (elf_relocation_valid)
    {
        BUSTER_TEST(arguments, elf_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);
        BUSTER_TEST(arguments, elf_roundtrip.relocations[0].addend == -4);
        BUSTER_STRING_TEST(arguments, elf_roundtrip.symbols[elf_roundtrip.relocations[0].symbol].name, S8("object_callee"));
    }
    ObjectArtifact coff = object_write(arguments->arena, &object, OBJECT_FORMAT_COFF);
    ObjectFile coff_roundtrip = object_read(arguments->arena, coff.bytes,
                                            (Target){
                                                .cpu_arch = CPU_ARCH_X86_64,
                                                .os = OPERATING_SYSTEM_WINDOWS,
                                            });
    BUSTER_TEST(arguments, coff_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, coff_roundtrip.symbol_count == object.symbol_count);
    BUSTER_TEST(arguments, coff_roundtrip.relocation_count == object.relocation_count);
    bool coff_relocation_valid = coff_roundtrip.relocations && coff_roundtrip.relocation_count && coff_roundtrip.symbols &&
                                 coff_roundtrip.relocations[0].symbol < coff_roundtrip.symbol_count;
    BUSTER_TEST(arguments, coff_relocation_valid);
    if (coff_relocation_valid)
    {
        BUSTER_TEST(arguments, coff_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);
        BUSTER_TEST(arguments, coff_roundtrip.relocations[0].addend == -4);
        BUSTER_STRING_TEST(arguments, coff_roundtrip.symbols[coff_roundtrip.relocations[0].symbol].name, S8("object_callee"));
    }
    u16 coff_machine = 0;
    if (coff.bytes.pointer && coff.bytes.length >= 2)
    {
        memcpy(&coff_machine, coff.bytes.pointer, 2);
    }
    BUSTER_TEST(arguments, coff_machine == 0x8664);
    ObjectArtifact mach = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    ObjectFile mach_roundtrip = object_read(arguments->arena, mach.bytes,
                                            (Target){
                                                .cpu_arch = CPU_ARCH_X86_64,
                                                .os = OPERATING_SYSTEM_MACOS,
                                            });
    BUSTER_TEST(arguments, mach_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, mach_roundtrip.symbol_count == object.symbol_count);
    BUSTER_TEST(arguments, mach_roundtrip.relocation_count == object.relocation_count);
    bool mach_relocation_valid = mach_roundtrip.relocations && mach_roundtrip.relocation_count && mach_roundtrip.symbols &&
                                 mach_roundtrip.relocations[0].symbol < mach_roundtrip.symbol_count;
    BUSTER_TEST(arguments, mach_relocation_valid);
    if (mach_relocation_valid)
    {
        BUSTER_TEST(arguments, mach_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);
        BUSTER_TEST(arguments, mach_roundtrip.relocations[0].addend == -4);
        BUSTER_STRING_TEST(arguments, mach_roundtrip.symbols[mach_roundtrip.relocations[0].symbol].name, S8("object_callee"));
    }
    u32 mach_magic = 0;
    if (mach.bytes.pointer && mach.bytes.length >= 4)
    {
        memcpy(&mach_magic, mach.bytes.pointer, 4);
    }
    BUSTER_TEST(arguments, mach_magic == 0xfeedfacf);
    sections[0].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(aarch64_text);
    object.target.cpu_arch = CPU_ARCH_AARCH64;
    relocation = (ObjectRelocation){
        .offset = 4,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
    symbols[0].size = 16;
    symbols[1].value = 16;
    symbols[1].size = 8;
    ObjectArtifact aarch64_elf = object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, aarch64_elf.error == OBJECT_ERROR_NONE);
    u16 aarch64_elf_machine = 0;
    if (aarch64_elf.bytes.pointer && aarch64_elf.bytes.length >= 20)
    {
        memcpy(&aarch64_elf_machine, aarch64_elf.bytes.pointer + 18, 2);
    }
    BUSTER_TEST(arguments, aarch64_elf_machine == 183);
    ObjectArtifact aarch64_coff = object_write(arguments->arena, &object, OBJECT_FORMAT_COFF);
    BUSTER_TEST(arguments, aarch64_coff.error == OBJECT_ERROR_NONE);
    u16 aarch64_coff_machine = 0;
    if (aarch64_coff.bytes.pointer && aarch64_coff.bytes.length >= 2)
    {
        memcpy(&aarch64_coff_machine, aarch64_coff.bytes.pointer, 2);
    }
    BUSTER_TEST(arguments, aarch64_coff_machine == 0xaa64);
    ObjectArtifact aarch64_mach = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, aarch64_mach.error == OBJECT_ERROR_NONE);
    u32 aarch64_mach_cpu = 0;
    if (aarch64_mach.bytes.pointer && aarch64_mach.bytes.length >= 8)
    {
        memcpy(&aarch64_mach_cpu, aarch64_mach.bytes.pointer + 4, 4);
    }
    BUSTER_TEST(arguments, aarch64_mach_cpu == 0x0100000c);
    sections[0].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(x86_text);
    object.target.cpu_arch = CPU_ARCH_X86_64;
    relocation = (ObjectRelocation){
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    symbols[0].size = 6;
    symbols[1].value = 6;
    symbols[1].size = 6;
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    ObjectExecutable executable = object_link_executable(&object);
    BUSTER_TEST(arguments, executable.error == OBJECT_ERROR_NONE);
    if (executable.address)
    {
        u64 (*entry)(void) = 0;
        memcpy(&entry, &executable.address, sizeof(entry));
        BUSTER_TEST(arguments, entry() == 42);
        object_release_executable(executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    sections[0].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(aarch64_text);
    object.target.cpu_arch = CPU_ARCH_AARCH64;
    relocation = (ObjectRelocation){
        .offset = 4,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
    symbols[0].size = 16;
    symbols[1].value = 16;
    symbols[1].size = 8;
    ObjectExecutable executable = object_link_executable(&object);
    BUSTER_TEST(arguments, executable.error == OBJECT_ERROR_NONE);
    if (executable.address)
    {
        u64 (*entry)(void) = 0;
        memcpy(&entry, &executable.address, sizeof(entry));
        BUSTER_TEST(arguments, entry() == 42);
        object_release_executable(executable);
    }
#else
    BUSTER_UNUSED(aarch64_text);
#endif
    AstCode defined_code = {
        .name = S8("defined_function"),
        .has_body = true,
    };
    AnalysisEntity defined_entity = {
        .name = defined_code.name,
        .id =
            {
                .module = {.value = 11},
                .index = {.value = 0},
            },
        .kind = ANALYSIS_ENTITY_CODE,
        .ast.code = &defined_code,
    };
    AnalysisResult separate_analysis = {
        .module =
            {
                .entities = &defined_entity,
                .id = {.value = 11},
                .entity_count = 1,
            },
    };
    u8 separate_code[] = {
        0xe8, 0, 0, 0, 0, 0xc3,
    };
    CodegenModuleEntry separate_entry = {
        .entity = defined_entity.id,
    };
    CodegenModuleRelocation separate_relocation = {
        .entity =
            {
                .module = {.value = 12},
                .index = {.value = 3},
            },
        .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
        .offset = 1,
    };
    CodegenModule separate_module = {
        .code = (ByteSlice)BUSTER_ARRAY_TO_SLICE(separate_code),
        .entries = &separate_entry,
        .relocations = &separate_relocation,
        .abi = CODEGEN_ABI_X86_64_SYSTEM_V,
        .entry_count = 1,
        .relocation_count = 1,
    };
    ObjectFile separate_object = object_from_codegen_module(arguments->arena, &separate_analysis, &separate_module,
                                                            (Target){
                                                                .cpu_arch = CPU_ARCH_X86_64,
                                                                .os = OPERATING_SYSTEM_LINUX,
                                                            });
    BUSTER_TEST(arguments, separate_object.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, separate_object.symbol_count == 2);
    bool separate_symbol_is_undefined = false;
    if (separate_object.symbols && separate_object.symbol_count >= 2)
    {
        separate_symbol_is_undefined = separate_object.symbols[1].section == OBJECT_SECTION_UNDEFINED;
    }
    BUSTER_TEST(arguments, separate_symbol_is_undefined);
    BUSTER_TEST(arguments, separate_object.relocation_count == 1);
    ObjectArtifact separate_elf = object_write(arguments->arena, &separate_object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, separate_elf.error == OBJECT_ERROR_NONE);
    return result;
}
#endif
