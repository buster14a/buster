#include <buster/lib/compiler/object/object.h>

#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/integer.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL void object_debug_module_set(Arena* arena, ObjectFile* object, String8 name, u64 code_size);

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
    if (offset > buffer->capacity || sizeof(value) > buffer->capacity - offset)
    {
        buffer->error = OBJECT_ERROR_CAPACITY;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void object_write_u32_at(ObjectBuffer* buffer, u64 offset, u32 value)
{
    if (offset > buffer->capacity || sizeof(value) > buffer->capacity - offset)
    {
        buffer->error = OBJECT_ERROR_CAPACITY;
        return;
    }
    memcpy(buffer->bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void object_write_u64_at(ObjectBuffer* buffer, u64 offset, u64 value)
{
    if (offset > buffer->capacity || sizeof(value) > buffer->capacity - offset)
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

// Mach-O's PAGE relocations are emitted against instructions whose immediate
// field is zero.  Keep the instruction predicates here in lockstep with
// LLVM's AArch64 PAGEOFF12 fixup rules: ADD (immediate) uses byte granularity,
// while unsigned LD/ST (immediate) scales the low-page offset by access size.
BUSTER_GLOBAL_LOCAL bool object_mach_page21_instruction_valid(u32 instruction)
{
    return (instruction & UINT32_C(0xffffffe0)) == UINT32_C(0x90000000);
}

BUSTER_GLOBAL_LOCAL bool object_mach_pageoff12_shift(u32 instruction, u32* shift)
{
    if (!shift)
    {
        return false;
    }
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
    // The direct PAGEOFF form used by codegen is a 64-bit, non-setting ADD
    // with no LSL #12.  Registers are intentionally unconstrained: a PAGE
    // relocation does not imply that this instruction consumes a PAGE21's
    // destination register.
    if ((instruction & UINT32_C(0xffc00000)) == UINT32_C(0x91000000) &&
        !(instruction & (UINT32_C(0xfff) << 10)))
    {
        *shift = 0;
        return true;
    }
    return false;
}

// Checked target+addend arithmetic shared by the in-memory object linker.
// Relocation addends are signed even when the resulting address is unsigned;
// never rely on unsigned wraparound here because malformed objects must fail
// before a patch is committed.
BUSTER_GLOBAL_LOCAL bool object_address_addend(u64 address, s64 addend, u64* result)
{
    if (!result)
    {
        return false;
    }
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

BUSTER_GLOBAL_LOCAL bool object_apply_aarch64_mach_page_relocation(ObjectRelocationKind kind, u8* patch, u64 place, u64 target, s64 addend)
{
    if (!patch || (place & 3))
    {
        return false;
    }
    u64 address = 0;
    if (!object_address_addend(target, addend, &address))
    {
        return false;
    }
    u32 instruction = 0;
    memcpy(&instruction, patch, sizeof(instruction));
    if (kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21)
    {
        if (!object_mach_page21_instruction_valid(instruction))
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
        if (!object_mach_pageoff12_shift(instruction, &shift) || ((address & 0xfff) & ((1u << shift) - 1)))
        {
            return false;
        }
        instruction &= ~(UINT32_C(0xfff) << 10);
        instruction |= (u32)((address & 0xfff) >> shift) << 10;
        memcpy(patch, &instruction, sizeof(instruction));
        return true;
    }
    return false;
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

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_memory_destination(ObjectAssemblyBuffer* buffer, u32 register_index, bool wide)
{
    if ((register_index & 31) == 31)
    {
        object_assembly_append_string(buffer, wide ? S8("xzr") : S8("wzr"));
    }
    else
    {
        object_assembly_append_aarch64_register_width(buffer, register_index, wide);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_add_register(ObjectAssemblyBuffer* buffer, u32 register_index, bool wide)
{
    if ((register_index & 31) == 31)
    {
        object_assembly_append_string(buffer, S8("sp"));
    }
    else
    {
        object_assembly_append_aarch64_register_width(buffer, register_index, wide);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_memory_register(ObjectAssemblyBuffer* buffer, u32 register_index)
{
    if ((register_index & 31) == 31)
    {
        object_assembly_append_string(buffer, S8("sp"));
    }
    else
    {
        object_assembly_append_aarch64_register(buffer, register_index);
    }
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_fp_register(ObjectAssemblyBuffer* buffer, u32 register_index, u32 size, bool vector128)
{
    static String8 const scalar_names[] = {S8("b"), S8("h"), S8("s"), S8("d")};
    object_assembly_append_string(buffer, vector128 ? S8("q") : scalar_names[size & 3]);
    object_assembly_append_u64_decimal(buffer, register_index & 31);
}

BUSTER_GLOBAL_LOCAL void object_assembly_append_aarch64_prefetch_operation(ObjectAssemblyBuffer* buffer, u32 operation)
{
    static String8 const names[] = {
        S8("pldl1keep"), S8("pldl1strm"), S8("pldl2keep"), S8("pldl2strm"),
        S8("pldl3keep"), S8("pldl3strm"), {0}, {0},
        S8("plil1keep"), S8("plil1strm"), S8("plil2keep"), S8("plil2strm"),
        S8("plil3keep"), S8("plil3strm"), {0}, {0},
        S8("pstl1keep"), S8("pstl1strm"), S8("pstl2keep"), S8("pstl2strm"),
        S8("pstl3keep"), S8("pstl3strm"),
    };
    if (operation < BUSTER_ARRAY_LENGTH(names) && names[operation].length)
    {
        object_assembly_append_string(buffer, names[operation]);
    }
    else
    {
        object_assembly_append_string(buffer, S8("#"));
        object_assembly_append_u64_decimal(buffer, operation);
    }
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
        case OBJECT_SECTION_ZERO:
            return S8("\t.section __DATA,__bss,zerofill\n");
        case OBJECT_SECTION_THREAD_LOCAL_DATA:
            return S8("\t.section __DATA,__thread_data\n");
        case OBJECT_SECTION_THREAD_LOCAL_ZERO:
            return S8("\t.section __DATA,__thread_bss\n");
        case OBJECT_SECTION_UNWIND:
            return S8("\t.section __TEXT,__eh_frame,coalesced,no_toc+strip_static_syms+live_support\n");
        case OBJECT_SECTION_WINDOWS_PDATA:
            return S8("\t.section __DATA,__pdata\n");
        case OBJECT_SECTION_WINDOWS_XDATA:
            return S8("\t.section __DATA,__xdata\n");
        case OBJECT_SECTION_DEBUG_INFO:
            return S8("\t.section __DWARF,__debug_info\n");
        case OBJECT_SECTION_DEBUG_ABBREV:
            return S8("\t.section __DWARF,__debug_abbrev\n");
        case OBJECT_SECTION_DEBUG_LINE:
            return S8("\t.section __DWARF,__debug_line\n");
        case OBJECT_SECTION_DEBUG_STR:
            return S8("\t.section __DWARF,__debug_str\n");
        case OBJECT_SECTION_DEBUG_LOC:
            return S8("\t.section __DWARF,__debug_loc\n");
        case OBJECT_SECTION_DEBUG_RANGES:
            return S8("\t.section __DWARF,__debug_ranges\n");
        case OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS:
            return S8("\t.section __DWARF,__debug$S\n");
        case OBJECT_SECTION_DEBUG_CODEVIEW_TYPES:
            return S8("\t.section __DWARF,__debug$T\n");
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
    case OBJECT_SECTION_ZERO:
        return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI ? S8("\t.section .bss\n")
                                                                                           : S8("\t.section .bss,\"aw\",@nobits\n");
    case OBJECT_SECTION_THREAD_LOCAL_DATA:
        return S8("\t.section .tdata\n");
    case OBJECT_SECTION_THREAD_LOCAL_ZERO:
        return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI ? S8("\t.section .tbss\n")
                                                                                           : S8("\t.section .tbss,\"awT\",@nobits\n");
    case OBJECT_SECTION_UNWIND:
        return S8("\t.section .eh_frame,\"a\",@progbits\n");
    case OBJECT_SECTION_WINDOWS_PDATA:
        return S8("\t.section .pdata\n");
    case OBJECT_SECTION_WINDOWS_XDATA:
        return S8("\t.section .xdata\n");
    case OBJECT_SECTION_DEBUG_INFO:
        return S8("\t.section .debug_info\n");
    case OBJECT_SECTION_DEBUG_ABBREV:
        return S8("\t.section .debug_abbrev\n");
    case OBJECT_SECTION_DEBUG_LINE:
        return S8("\t.section .debug_line\n");
    case OBJECT_SECTION_DEBUG_STR:
        return S8("\t.section .debug_str\n");
    case OBJECT_SECTION_DEBUG_LOC:
        return S8("\t.section .debug_loc\n");
    case OBJECT_SECTION_DEBUG_RANGES:
        return S8("\t.section .debug_ranges\n");
    case OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS:
        return S8("\t.section .debug$S\n");
    case OBJECT_SECTION_DEBUG_CODEVIEW_TYPES:
        return S8("\t.section .debug$T\n");
    case OBJECT_SECTION_COUNT:
        break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL u32 object_assembly_relocation_size(ObjectRelocationKind kind)
{
    return kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : kind == OBJECT_RELOCATION_COFF_SECTION16 ? 2 : 4;
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
    if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12)
    {
        u32 shift = 0;
        if (!object_mach_pageoff12_shift(word, &shift))
        {
            return false;
        }
    }
    if ((word & UINT32_C(0x7f000000)) == UINT32_C(0x11000000))
    {
        bool wide = (word & UINT32_C(0x80000000)) != 0;
        object_assembly_append_string(buffer, S8("\tadd "));
        object_assembly_append_aarch64_add_register(buffer, word, wide);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_add_register(buffer, word >> 5, wide);
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_relocation_value(buffer, object, target, relocation, modifier, modifier_after);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    if ((word & UINT32_C(0x3b000000)) == UINT32_C(0x39000000))
    {
        u32 size = (word >> 30) & 3;
        bool vector_register = (word & UINT32_C(0x04000000)) != 0;
        bool vector128 = vector_register && (word & UINT32_C(0x00800000)) != 0;
        bool load = (word & (UINT32_C(1) << 22)) != 0;
        if (vector_register)
        {
            object_assembly_append_string(buffer, load ? S8("\tldr ") : S8("\tstr "));
            object_assembly_append_aarch64_fp_register(buffer, word, size, vector128);
        }
        else if (size == 3 && (word & UINT32_C(0x00800000)) != 0 && !load)
        {
            object_assembly_append_string(buffer, S8("\tprfm "));
            object_assembly_append_aarch64_prefetch_operation(buffer, word & 31);
        }
        else if ((word & UINT32_C(0x00800000)) != 0)
        {
            String8 mnemonic = size == 0 ? S8("\tldrsb ") : size == 1 ? S8("\tldrsh ") : size == 2 ? S8("\tldrsw ") : (String8){0};
            if (!mnemonic.length)
            {
                return false;
            }
            object_assembly_append_string(buffer, mnemonic);
            object_assembly_append_aarch64_memory_destination(buffer, word, !load);
        }
        else
        {
            String8 mnemonic = !load ? (size == 0 ? S8("\tstrb ") : size == 1 ? S8("\tstrh ") : S8("\tstr "))
                                     : size == 0 ? S8("\tldrb ")
                                     : size == 1 ? S8("\tldrh ")
                                                  : S8("\tldr ");
            object_assembly_append_string(buffer, mnemonic);
            object_assembly_append_aarch64_memory_destination(buffer, word, size == 3);
        }
        object_assembly_append_string(buffer, S8(", ["));
        object_assembly_append_aarch64_memory_register(buffer, word >> 5);
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
    case OBJECT_RELOCATION_ABSOLUTE32:
        object_assembly_append_string(buffer, S8("\t.long "));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_COFF_SECREL32:
        object_assembly_append_string(buffer, S8("\t.long "));
        object_assembly_append_x86_relocation_value(buffer, object, target, relocation, S8("@SECREL32"));
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_COFF_SECTION16:
        object_assembly_append_string(buffer, S8("\t.short "));
        object_assembly_append_x86_relocation_value(buffer, object, target, relocation, S8("@SECT"));
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_COFF_ADDR32NB:
        object_assembly_append_string(buffer, S8("\t.rva "));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    case OBJECT_RELOCATION_X86_64_PC32:
    case OBJECT_RELOCATION_AARCH64_PREL32:
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
    case OBJECT_RELOCATION_AARCH64_JUMP26:
    {
        object_assembly_append_string(buffer, relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26 ? S8("\tbl ") : S8("\tb "));
        object_assembly_append_relocation_value(buffer, object, target, relocation, section_data);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    case OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21:
    case OBJECT_RELOCATION_AARCH64_MACH_PAGE21:
    case OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP:
    {
        u32 word = 0;
        memcpy(&word, section_data.pointer + relocation->offset, sizeof(word));
        if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 && !object_mach_page21_instruction_valid(word))
        {
            return false;
        }
        object_assembly_append_string(buffer, S8("\tadrp "));
        if ((word & 31) == 31)
        {
            object_assembly_append_string(buffer, S8("xzr"));
        }
        else
        {
            object_assembly_append_aarch64_register(buffer, word);
        }
        object_assembly_append_string(buffer, S8(", "));
        object_assembly_append_aarch64_relocation_value(
            buffer, object, target, relocation,
            relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 ? S8("@TLVPPAGE")
            : relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 ? S8("@PAGE")
                                                                         : (String8){0}, true);
        object_assembly_append_string(buffer, S8("\n"));
        return true;
    }
    case OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12:
    case OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12:
    case OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12:
    case OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12:
    {
        String8 modifier = relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 ? S8("@TLVPPAGEOFF")
                           : relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12      ? S8("@PAGEOFF")
                           : relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12          ? S8(":secrel_lo12:")
                                                                                                      : S8(":lo12:");
        return object_assembly_emit_aarch64_immediate_relocation(buffer, object, target, relocation, section_data, modifier,
                                                                 relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 ||
                                                                     relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12);
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
    object_assembly_append_string(buffer, width == 8 ? S8("mm") : width == 32 ? S8("ymm") : width == 64 ? S8("zmm") : S8("xmm"));
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
        object_assembly_append_string(buffer, width == 64 ? S8("\tcdqe\n") : width == 16 ? S8("\tcbw\n") : S8("\tcwde\n"));
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
            bool mmx = !prefix.operand16 && !prefix.rep && !prefix.repne &&
                       (extended == 0x6f || extended == 0x7f || extended == 0xef || extended == 0xfc || extended == 0xfe);
            u32 vector_width = mmx ? 8 : 16;
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
    u64 data_length = object_section_kind_is_zero_fill(section->kind) ? section->virtual_size : section->data.length;
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
    if (object_section_kind_is_zero_fill(section->kind))
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

BUSTER_GLOBAL_LOCAL bool object_reader_arena_position_after(Arena* arena, u64 position, u64 size, u64 alignment, u64* result)
{
    if (!arena || !result || !alignment || (alignment & (alignment - 1)) || position > arena->reserved_size || position > UINT64_MAX - (alignment - 1))
    {
        return false;
    }
    u64 aligned_position = (position + alignment - 1) & ~(alignment - 1);
    if (aligned_position > arena->reserved_size || size > arena->reserved_size - aligned_position)
    {
        return false;
    }
    *result = aligned_position + size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_reader_arena_can_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    u64 position = 0;
    return arena && object_reader_arena_position_after(arena, arena->position, size, alignment, &position);
}

BUSTER_GLOBAL_LOCAL bool object_reader_arena_can_allocate_count(Arena* arena, u64 count, u64 element_size, u64 alignment)
{
    if (element_size && count > UINT64_MAX / element_size)
    {
        return false;
    }
    return object_reader_arena_can_allocate_bytes(arena, count * element_size, alignment);
}

BUSTER_GLOBAL_LOCAL bool object_reader_section_sizes_valid(Arena* arena, u64 const* sizes)
{
    u64 total = 0;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        if (sizes[kind] > UINT64_MAX - total)
        {
            return false;
        }
        total += sizes[kind];
    }
    u64 data_total = 0;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        if (!object_section_kind_is_zero_fill((ObjectSectionKind)kind))
        {
            if (sizes[kind] > UINT64_MAX - data_total)
            {
                return false;
            }
            data_total += sizes[kind];
        }
    }
    return object_reader_arena_can_allocate_bytes(arena, data_total, BUSTER_ALIGN_OF(u8));
}

BUSTER_GLOBAL_LOCAL u64 object_reader_hash_u64(u64 value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

BUSTER_GLOBAL_LOCAL bool object_read_string_checked(ByteSlice bytes, u64 table_offset, u64 table_size, u32 string_offset, String8* result)
{
    if (string_offset >= table_size || table_offset > bytes.length || table_size > bytes.length - table_offset)
    {
        return false;
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
        return false;
    }
    *result = (String8){
        .pointer = (char8*)bytes.pointer + offset,
        .length = length,
    };
    return true;
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
    case OBJECT_SECTION_ZERO:
        return S8(".bss");
    case OBJECT_SECTION_THREAD_LOCAL_DATA:
        return S8(".tdata");
    case OBJECT_SECTION_THREAD_LOCAL_ZERO:
        return S8(".tbss");
    case OBJECT_SECTION_UNWIND:
        return S8(".eh_frame");
    case OBJECT_SECTION_WINDOWS_PDATA:
        return S8(".pdata");
    case OBJECT_SECTION_WINDOWS_XDATA:
        return S8(".xdata");
    case OBJECT_SECTION_DEBUG_INFO:
        return S8(".debug_info");
    case OBJECT_SECTION_DEBUG_ABBREV:
        return S8(".debug_abbrev");
    case OBJECT_SECTION_DEBUG_LINE:
        return S8(".debug_line");
    case OBJECT_SECTION_DEBUG_STR:
        return S8(".debug_str");
    case OBJECT_SECTION_DEBUG_LOC:
        return S8(".debug_loc");
    case OBJECT_SECTION_DEBUG_RANGES:
        return S8(".debug_ranges");
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
    if (object_section_kind_is_debug(kind))
    {
        return 1;
    }
    if (kind == OBJECT_SECTION_UNWIND)
    {
        return 8;
    }
    return kind == OBJECT_SECTION_WINDOWS_PDATA || kind == OBJECT_SECTION_WINDOWS_XDATA ? 4 : 16;
}

bool object_section_kind_is_zero_fill(ObjectSectionKind kind)
{
    return kind == OBJECT_SECTION_ZERO || kind == OBJECT_SECTION_THREAD_LOCAL_ZERO;
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
    if (string_equal(name, S8(".debug_loc")) || string_equal(name, S8("__debug_loc")))
    {
        return OBJECT_SECTION_DEBUG_LOC;
    }
    if (string_equal(name, S8(".debug_ranges")) || string_equal(name, S8("__debug_ranges")))
    {
        return OBJECT_SECTION_DEBUG_RANGES;
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
        ELF_REL_SIZE = 16,
        ELF_RELA_SIZE = 24,
        ELF_SHN_LORESERVE = 0xff00,
    };
    u16 type = 0;
    u16 machine = 0;
    u64 section_table = 0;
    u16 section_entry_size = 0;
    u16 section_count = 0;
    u16 section_string_index = 0;
    if (!arena || !bytes.pointer || bytes.length < ELF_HEADER_SIZE ||
        memcmp(bytes.pointer,
               "\x7f"
               "ELF",
               4) != 0 ||
        bytes.pointer[4] != 2 || bytes.pointer[5] != 1 || !object_read_u16(bytes, 16, &type) || type != 1 || !object_read_u16(bytes, 18, &machine) ||
        !object_read_u64(bytes, 40, &section_table) || !object_read_u16(bytes, 58, &section_entry_size) || !object_read_u16(bytes, 60, &section_count) ||
        !object_read_u16(bytes, 62, &section_string_index) || section_entry_size != ELF_SECTION_HEADER_SIZE || !section_count ||
        section_string_index >= section_count || section_table < ELF_HEADER_SIZE || section_table > bytes.length ||
        (u64)section_count * ELF_SECTION_HEADER_SIZE > bytes.length - section_table ||
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
            !section_string_size || section_string_size > bytes.length - section_string_offset ||
            bytes.pointer[section_string_offset] != 0)
        {
            return result;
        }
    }
    if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return result;
    }
    u32* section_kinds = arena_allocate(arena, u32, section_count);
    if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(u64), BUSTER_ALIGN_OF(u64)))
    {
        return result;
    }
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
        String8 name = {0};
        if (!object_read_string_checked(bytes, section_string_offset, section_string_size, name_offset, &name))
        {
            return result;
        }
        bool unwind = string_equal(name, S8(".eh_frame"));
        bool unwind_type = section_type == 1 || (target.cpu_arch == CPU_ARCH_X86_64 && section_type == 0x70000001);
        bool ignored = string_starts_with_sequence(name, S8(".gcc_except_table")) || string_starts_with_sequence(name, S8(".ARM.exidx")) ||
                       string_starts_with_sequence(name, S8(".ARM.extab"));
        ObjectSectionKind debug_kind = flags & 0x2 ? OBJECT_SECTION_COUNT : object_debug_section_kind_from_name(name);
        if ((!(flags & 0x2) && debug_kind == OBJECT_SECTION_COUNT) || (unwind ? !unwind_type : section_type != 1 && section_type != 8) || ignored)
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
        ObjectSectionKind kind = unwind                             ? OBJECT_SECTION_UNWIND
                                 : debug_kind != OBJECT_SECTION_COUNT ? debug_kind
                                 : flags & 0x400                    ? (section_type == 8 ? OBJECT_SECTION_THREAD_LOCAL_ZERO : OBJECT_SECTION_THREAD_LOCAL_DATA)
                                 : flags & 0x4                      ? OBJECT_SECTION_TEXT
                                 : section_type == 8                ? OBJECT_SECTION_ZERO
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
    if (!object_reader_section_sizes_valid(arena, section_sizes))
    {
        return result;
    }
    if (!object_reader_arena_can_allocate_count(arena, OBJECT_SECTION_COUNT, sizeof(ObjectSection), BUSTER_ALIGN_OF(ObjectSection)))
    {
        return result;
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        bool zero_fill = object_section_kind_is_zero_fill((ObjectSectionKind)kind);
        if (!zero_fill && !object_reader_arena_can_allocate_count(arena, section_sizes[kind], sizeof(u8), BUSTER_ALIGN_OF(u8)))
        {
            return result;
        }
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
                size > bytes.length - offset || size / ELF_SYMBOL_SIZE > UINT32_MAX || link >= section_count)
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
            !object_read_u64(bytes, section + 32, &string_size) || !string_size || string_offset > bytes.length ||
            string_size > bytes.length - string_offset || bytes.pointer[string_offset] != 0)
        {
            return result;
        }
    }
    if (!object_reader_arena_can_allocate_count(arena, symbol_count, sizeof(ObjectSymbol), BUSTER_ALIGN_OF(ObjectSymbol)))
    {
        return result;
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_count);
    if (!object_reader_arena_can_allocate_count(arena, symbol_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return result;
    }
    u32* symbol_map = arena_allocate(arena, u32, symbol_count);
    u64 symbol_name_bytes = 0;
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
        // Absolute, common, processor-specific, and SHN_XINDEX symbols do
        // not identify one of the ordinary section headers represented by an
        // ObjectFile.  Keep them unsupported-but-skippable, as the previous
        // reader did, while still rejecting malformed ordinary indexes.
        if (section_index >= ELF_SHN_LORESERVE)
        {
            continue;
        }
        if (section_index != 0 && section_index >= section_count)
        {
            return result;
        }
        if (section_index != 0 && section_kinds[section_index] == UINT32_MAX)
        {
            continue;
        }
        String8 name = {0};
        if (!object_read_string_checked(bytes, string_offset, string_size, name_offset, &name))
        {
            return result;
        }
        if (name.length > UINT64_MAX - symbol_name_bytes)
        {
            return result;
        }
        symbol_name_bytes += name.length;
        if (!name.length)
        {
            String8 prefix = S8(".Lobject.");
            if (!object_reader_arena_can_allocate_bytes(arena, prefix.length + 10, BUSTER_ALIGN_OF(char8)))
            {
                return result;
            }
            name = string_format(arena, S8(".Lobject.{u32}"), source_index);
        }
        if (!object_reader_arena_can_allocate_bytes(arena, name.length, BUSTER_ALIGN_OF(char8)))
        {
            return result;
        }
        ObjectSymbol* destination = &result.symbols[result.symbol_count];
        u64 symbol_value = 0;
        if (section_index)
        {
            ObjectSectionKind symbol_kind = (ObjectSectionKind)section_kinds[section_index];
            u64 symbol_base = section_bases[section_index];
            u64 section_virtual_size = result.sections[symbol_kind].virtual_size;
            if (symbol_base > section_virtual_size)
            {
                return result;
            }
            u64 section_remaining = section_virtual_size - symbol_base;
            if (value > section_remaining || size > section_remaining - value || value > UINT64_MAX - symbol_base || size > UINT64_MAX - value)
            {
                return result;
            }
            symbol_value = symbol_base + value;
        }
        *destination = (ObjectSymbol){
            .name = string_duplicate_arena(arena, name, false),
            .value = symbol_value,
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
        u64 offset = 0;
        u64 size = 0;
        u64 entry_size = 0;
        u32 target_section = 0;
        if (!object_read_u32(bytes, section + 4, &section_type) || !object_read_u64(bytes, section + 24, &offset) ||
            !object_read_u64(bytes, section + 32, &size) ||
            !object_read_u32(bytes, section + 44, &target_section) || !object_read_u64(bytes, section + 56, &entry_size))
        {
            return result;
        }
        if (section_type != 4 && section_type != 9)
        {
            continue;
        }
        u64 relocation_size = section_type == 4 ? ELF_RELA_SIZE : ELF_REL_SIZE;
        if (entry_size != relocation_size || size % relocation_size || offset > bytes.length || size > bytes.length - offset ||
            size / relocation_size > UINT32_MAX || target_section >= section_count)
        {
            return result;
        }
        if (section_kinds[target_section] == UINT32_MAX)
        {
            continue;
        }
        if (size / relocation_size > UINT32_MAX - relocation_capacity)
        {
            return result;
        }
        relocation_capacity += (u32)(size / relocation_size);
    }
    if (!object_reader_arena_can_allocate_count(arena, relocation_capacity, sizeof(ObjectRelocation), BUSTER_ALIGN_OF(ObjectRelocation)))
    {
        return result;
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
        if ((section_type != 4 && section_type != 9) || target_section >= section_count || section_kinds[target_section] == UINT32_MAX)
        {
            continue;
        }
        if (linked_symbols != symbol_section || offset > bytes.length || size > bytes.length - offset)
        {
            return result;
        }
        u64 relocation_size = section_type == 4 ? ELF_RELA_SIZE : ELF_REL_SIZE;
        u32 count = (u32)(size / relocation_size);
        for (u32 relocation_index = 0; relocation_index < count; relocation_index += 1)
        {
            u64 relocation = offset + (u64)relocation_index * relocation_size;
            u64 source_offset = 0;
            u64 information = 0;
            s64 addend = 0;
            if (!object_read_u64(bytes, relocation, &source_offset) || !object_read_u64(bytes, relocation + 8, &information) ||
                (section_type == 4 && !object_read_s64(bytes, relocation + 16, &addend)))
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
                       : relocation_type == 261                           ? OBJECT_RELOCATION_AARCH64_PREL32
                       : relocation_type == 282                           ? OBJECT_RELOCATION_AARCH64_JUMP26
                       : relocation_type == 283                           ? OBJECT_RELOCATION_AARCH64_CALL26
                       : relocation_type == 549                           ? OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12
                       : relocation_type == 551                           ? OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12
                                                                          : OBJECT_RELOCATION_COUNT;
                if (relocation_type == 282 || relocation_type == 283)
                {
                    result.symbols[symbol_map[source_symbol]].kind = OBJECT_SYMBOL_FUNCTION;
                }
            }
            u64 relocation_width = kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
            ObjectSection* target_section_data = &result.sections[section_kinds[target_section]];
            if (kind == OBJECT_RELOCATION_AARCH64_PREL32 &&
                (target_section_data->alignment < 4 || source_offset > UINT64_MAX - section_bases[target_section] ||
                 ((section_bases[target_section] + source_offset) & 3)))
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
            if (kind == OBJECT_RELOCATION_COUNT || section_bases[target_section] > target_section_data->virtual_size ||
                source_offset > target_section_data->virtual_size - section_bases[target_section] ||
                relocation_width > target_section_data->virtual_size - section_bases[target_section] - source_offset)
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
            if (kind == OBJECT_RELOCATION_AARCH64_CALL26 || kind == OBJECT_RELOCATION_AARCH64_JUMP26)
            {
                u64 instruction_offset = section_bases[target_section] + source_offset;
                u32 instruction = 0;
                u32 canonical = 0;
                A64MCInst decoded = {0};
                A64Opcode opcode = kind == OBJECT_RELOCATION_AARCH64_CALL26 ? A64_OPCODE_BL : A64_OPCODE_B;
                if ((instruction_offset & 3) || target_section_data->alignment < 4 || instruction_offset > target_section_data->data.length ||
                    sizeof(instruction) > target_section_data->data.length - instruction_offset ||
                    !object_read_u32(target_section_data->data, instruction_offset, &instruction) || !a64_mc_decode(instruction, &decoded) ||
                    decoded.opcode != opcode || !a64_pc_relative_patch(opcode, instruction, 0, &canonical))
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                if (section_type == 9)
                {
                    addend = decoded.operands[0].value;
                }
                memcpy(target_section_data->data.pointer + instruction_offset, &canonical, sizeof(canonical));
            }
            else if (section_type == 9)
            {
                u64 value_offset = section_bases[target_section] + source_offset;
                if (kind == OBJECT_RELOCATION_ABSOLUTE64)
                {
                    u64 stored = 0;
                    if (!object_read_u64(target_section_data->data, value_offset, &stored))
                    {
                        return result;
                    }
                    memcpy(&addend, &stored, sizeof(addend));
                }
                else if (kind == OBJECT_RELOCATION_ABSOLUTE32)
                {
                    u32 stored = 0;
                    if (!object_read_u32(target_section_data->data, value_offset, &stored))
                    {
                        return result;
                    }
                    addend = (s64)(u64)stored;
                }
                else if (kind == OBJECT_RELOCATION_X86_64_PC32 || kind == OBJECT_RELOCATION_X86_64_TPOFF32 ||
                         kind == OBJECT_RELOCATION_AARCH64_PREL32)
                {
                    u32 stored = 0;
                    if (!object_read_u32(target_section_data->data, value_offset, &stored) ||
                        !a64_signed_scaled_immediate_decode(stored, 32, 0, &addend))
                    {
                        return result;
                    }
                }
                else
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
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

BUSTER_GLOBAL_LOCAL String8 object_read_coff_name(ByteSlice bytes, u64 offset, u64 string_offset, u64 string_size, bool strict_long_name, bool* valid)
{
    *valid = false;
    if (offset > bytes.length || 8 > bytes.length - offset)
    {
        return (String8){0};
    }
    if (bytes.pointer[offset] == '/')
    {
        u32 parsed = 0;
        u64 index = 1;
        bool any = false;
        while (index < 8 && bytes.pointer[offset + index] >= '0' && bytes.pointer[offset + index] <= '9')
        {
            any = true;
            u32 digit = (u32)(bytes.pointer[offset + index] - '0');
            if (parsed > (UINT32_MAX - digit) / 10)
            {
                return (String8){0};
            }
            parsed = parsed * 10 + digit;
            index += 1;
        }
        bool padding = true;
        while (index < 8)
        {
            padding &= bytes.pointer[offset + index] == 0;
            index += 1;
        }
        if (any && (padding || strict_long_name))
        {
            if (!padding)
            {
                return (String8){0};
            }
            String8 result = {0};
            *valid = object_read_string_checked(bytes, string_offset, string_size, parsed, &result);
            return result;
        }
    }
    u32 zero = 0;
    u32 name_offset = 0;
    memcpy(&zero, bytes.pointer + offset, sizeof(zero));
    memcpy(&name_offset, bytes.pointer + offset + 4, sizeof(name_offset));
    if (!zero)
    {
        String8 result = {0};
        *valid = object_read_string_checked(bytes, string_offset, string_size, name_offset, &result);
        return result;
    }
    u64 length = 0;
    while (length < 8 && bytes.pointer[offset + length])
    {
        length += 1;
    }
    *valid = true;
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
    if (!arena || !bytes.pointer || bytes.length < COFF_HEADER_SIZE || !object_read_u16(bytes, 0, &machine) || !object_read_u16(bytes, 2, &section_count) ||
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
    if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return result;
    }
    u32* section_kinds = arena_allocate(arena, u32, section_count);
    if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(u64), BUSTER_ALIGN_OF(u64)))
    {
        return result;
    }
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
            !object_read_u32(bytes, section + 36, &characteristics) ||
            ((characteristics & 0x80) != 0 && raw_offset && raw_size) ||
            (raw_offset && (raw_offset > bytes.length || raw_size > bytes.length - raw_offset)) ||
            (relocation_count && (relocation_offset > bytes.length || (u64)relocation_count * COFF_RELOCATION_SIZE > bytes.length - relocation_offset)) ||
            relocation_count > UINT32_MAX - relocation_capacity)
        {
            return result;
        }
        bool name_valid = false;
        String8 name = object_read_coff_name(bytes, section, string_offset, string_size, true, &name_valid);
        if (!name_valid)
        {
            return result;
        }
        bool is_thread_local =
            string_starts_with_sequence(name, S8(".tls")) || string_starts_with_sequence(name, S8(".tdata")) || string_starts_with_sequence(name, S8(".tbss"));
        // CodeView sections are discardable but must survive a round trip
        // through a relocatable object, so they are classified before the
        // discardable/removable filter.
        ObjectSectionKind debug_kind = object_debug_section_kind_from_name(name);
        bool ignored = debug_kind == OBJECT_SECTION_COUNT && (characteristics & 0x02000800) != 0;
        if (ignored)
        {
            section_kinds[section_index] = UINT32_MAX;
            continue;
        }
        bool zero_fill = !raw_offset || (characteristics & 0x80) != 0;
        ObjectSectionKind kind = string_equal(name, S8(".pdata")) ? OBJECT_SECTION_WINDOWS_PDATA
                                 : string_equal(name, S8(".xdata")) ? OBJECT_SECTION_WINDOWS_XDATA
                                 : debug_kind != OBJECT_SECTION_COUNT ? debug_kind
                                 : is_thread_local                  ? (zero_fill ? OBJECT_SECTION_THREAD_LOCAL_ZERO : OBJECT_SECTION_THREAD_LOCAL_DATA)
                                 : characteristics & 0x20           ? OBJECT_SECTION_TEXT
                                 : zero_fill                         ? OBJECT_SECTION_ZERO
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
    if (!object_reader_section_sizes_valid(arena, section_sizes))
    {
        return result;
    }
    if (!object_reader_arena_can_allocate_count(arena, OBJECT_SECTION_COUNT, sizeof(ObjectSection), BUSTER_ALIGN_OF(ObjectSection)))
    {
        return result;
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        bool zero_fill = object_section_kind_is_zero_fill((ObjectSectionKind)kind);
        if (!zero_fill && !object_reader_arena_can_allocate_count(arena, section_sizes[kind], sizeof(u8), BUSTER_ALIGN_OF(u8)))
        {
            return result;
        }
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
    if (!object_reader_arena_can_allocate_count(arena, symbol_count, sizeof(ObjectSymbol), BUSTER_ALIGN_OF(ObjectSymbol)))
    {
        return result;
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_count);
    if (!object_reader_arena_can_allocate_count(arena, symbol_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return result;
    }
    u32* symbol_map = arena_allocate(arena, u32, symbol_count);
    u64 symbol_name_bytes = 0;
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
        if (section_number > 0 && section_number > section_count)
        {
            return result;
        }
        if (section_number > 0 && section_kinds[(u16)section_number - 1] == UINT32_MAX)
        {
            source_index += (u32)auxiliary_count + 1;
            continue;
        }
        if (section_number >= 0)
        {
            u16 section_index = section_number ? (u16)section_number - 1 : 0;
            ObjectSectionKind symbol_kind = section_number ? (ObjectSectionKind)section_kinds[section_index] : OBJECT_SECTION_COUNT;
            u64 symbol_base = section_number ? section_bases[section_index] : 0;
            if (section_number && (symbol_base > section_sizes[symbol_kind] || (u64)value > section_sizes[symbol_kind] - symbol_base ||
                                  (u64)value > UINT64_MAX - symbol_base))
            {
                return result;
            }
            bool name_valid = false;
            String8 name = object_read_coff_name(bytes, source, string_offset, string_size, false, &name_valid);
            if (!name_valid)
            {
                return result;
            }
            if (name.length > UINT64_MAX - symbol_name_bytes)
            {
                return result;
            }
            symbol_name_bytes += name.length;
            if (!name.length)
            {
                String8 prefix = S8(".Lcoff.");
                if (!object_reader_arena_can_allocate_bytes(arena, prefix.length + 10, BUSTER_ALIGN_OF(char8)))
                {
                    return result;
                }
                name = string_format(arena, S8(".Lcoff.{u32}"), source_index);
            }
            if (!object_reader_arena_can_allocate_bytes(arena, name.length, BUSTER_ALIGN_OF(char8)))
            {
                return result;
            }
            u32 destination_index = result.symbol_count++;
            result.symbols[destination_index] = (ObjectSymbol){
                .name = string_duplicate_arena(arena, name, false),
                .value = section_number ? symbol_base + value : 0,
                .section = section_number ? section_kinds[section_index] : OBJECT_SECTION_UNDEFINED,
                .kind = symbol_type & 0x20 ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
                .global = storage == 2 || storage == 105,
            };
            symbol_map[source_index] = destination_index;
        }
        source_index += (u32)auxiliary_count + 1;
    }
    if (!object_reader_arena_can_allocate_count(arena, relocation_capacity, sizeof(ObjectRelocation), BUSTER_ALIGN_OF(ObjectRelocation)))
    {
        return result;
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
            u32 relocation_width = 0;
            if (codeview_section)
            {
                relocation_width = relocation_type == (target.cpu_arch == CPU_ARCH_X86_64 ? 0x000b : 0x0008) ? 4
                                   : relocation_type == (target.cpu_arch == CPU_ARCH_X86_64 ? 0x000a : 0x0007) ? 2
                                                                                                               : 0;
            }
            else if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                relocation_width = relocation_type == 1 ? 8
                                   : relocation_type == 3 || (relocation_type >= 4 && relocation_type <= 9) || relocation_type == 0xb ? 4
                                                                                                                                    : 0;
            }
            else
            {
                relocation_width = relocation_type == 0xe ? 8
                                   : relocation_type == 2 || relocation_type == 3 || relocation_type == 4 || relocation_type == 7 ||
                                             relocation_type == 0xf
                                         ? 4
                                         : 0;
            }
            if (relocation_width && (source_offset > raw_size || relocation_width > raw_size - source_offset || !raw_offset))
            {
                return result;
            }
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
                else if (relocation_type == 3)
                {
                    u32 stored = 0;
                    if (!object_read_u32(bytes, (u64)raw_offset + source_offset, &stored))
                    {
                        return result;
                    }
                    kind = OBJECT_RELOCATION_COFF_ADDR32NB;
                    addend = stored;
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
                kind = relocation_type == 2     ? OBJECT_RELOCATION_COFF_ADDR32NB
                       : relocation_type == 3   ? OBJECT_RELOCATION_AARCH64_CALL26
                       : relocation_type == 4   ? OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP
                       : relocation_type == 7   ? OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12
                       : relocation_type == 0xf ? OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12
                       : relocation_type == 0xe ? OBJECT_RELOCATION_ABSOLUTE64
                                                : OBJECT_RELOCATION_COUNT;
                if (relocation_type == 0xe)
                {
                    u64 stored = 0;
                    if (!object_read_u64(bytes, (u64)raw_offset + source_offset, &stored))
                    {
                        return result;
                    }
                    addend = (s64)stored;
                }
                else if (relocation_type == 2)
                {
                    u32 stored = 0;
                    if (!object_read_u32(bytes, (u64)raw_offset + source_offset, &stored))
                    {
                        return result;
                    }
                    addend = stored;
                }
                else if (relocation_type == 3)
                {
                    u32 stored = 0;
                    A64MCInst decoded = {0};
                    if (((section_bases[section_index] + source_offset) & 3) || result.sections[section_kinds[section_index]].alignment < 4 ||
                        !object_read_u32(bytes, (u64)raw_offset + source_offset, &stored) || !a64_mc_decode(stored, &decoded) ||
                        (decoded.opcode != A64_OPCODE_B && decoded.opcode != A64_OPCODE_BL) || decoded.operands[0].value != 0)
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    kind = decoded.opcode == A64_OPCODE_B ? OBJECT_RELOCATION_AARCH64_JUMP26 : OBJECT_RELOCATION_AARCH64_CALL26;
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

BUSTER_GLOBAL_LOCAL bool object_mach_compact_action_append(CodegenFunctionDescriptor* descriptor, u32 capacity, u32 code_offset,
                                                           CodegenUnwindActionKind kind, u8 register_index, u32 value)
{
    if (descriptor->unwind_action_count >= capacity)
    {
        return false;
    }
    descriptor->unwind_actions[descriptor->unwind_action_count++] = (CodegenUnwindAction){
        .code_offset = code_offset,
        .value = value,
        .kind = kind,
        .register_index = register_index,
    };
    return true;
}

bool object_mach_compact_decode(Arena* arena, ByteSlice text, u32 function_offset, u32 function_size, u32 encoding, Target target,
                                                   CodegenFunctionDescriptor* descriptor)
{
    enum
    {
        ACTION_CAPACITY = 32,
        X64_RBP_REGISTER = 5,
    };
    if (!arena || !descriptor || function_offset > text.length || function_size > text.length - function_offset || (encoding & 0xf0000000))
    {
        return false;
    }
    if (!object_reader_arena_can_allocate_count(arena, ACTION_CAPACITY, sizeof(CodegenUnwindAction), BUSTER_ALIGN_OF(CodegenUnwindAction)))
    {
        return false;
    }
    *descriptor = (CodegenFunctionDescriptor){
        .unwind_actions = arena_allocate(arena, CodegenUnwindAction, ACTION_CAPACITY),
        .code_offset = function_offset,
        .code_size = function_size,
    };
    u8* code = text.pointer + function_offset;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        // The frame-pointer compact form does not encode prolog positions.
        // Accept the canonical push/move sequence and reject register saves,
        // whose RBP-relative locations cannot be represented by the neutral
        // SP-relative descriptor.
        if ((encoding & 0x0f000000) != 0x01000000 || (encoding & 0x00007fff) || function_size < 4 || code[0] != 0x55 || code[1] != 0x48 ||
            !((code[2] == 0x89 && code[3] == 0xe5) || (code[2] == 0x8b && code[3] == 0xec)))
        {
            return false;
        }
        bool valid = object_mach_compact_action_append(descriptor, ACTION_CAPACITY, 1, CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_RBP_REGISTER, 0);
        valid = object_mach_compact_action_append(descriptor, ACTION_CAPACITY, 4, CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_RBP_REGISTER, 0) && valid;
        descriptor->prolog_size = 4;
        return valid;
    }
    if (target.cpu_arch != CPU_ARCH_AARCH64 || function_size % 4)
    {
        return false;
    }
    u32 mode = encoding & 0x0f000000;
    if (mode == 0x02000000)
    {
        if (encoding & 0x00000fff)
        {
            return false;
        }
        u32 expected_stack = ((encoding & 0x00fff000) >> 12) * 16;
        u32 stack_size = 0;
        u32 cursor = 0;
        while (stack_size < expected_stack && cursor + 4 <= function_size)
        {
            u32 instruction = 0;
            memcpy(&instruction, code + cursor, sizeof(instruction));
            if ((instruction & UINT32_C(0xff8003ff)) != UINT32_C(0xd10003ff))
            {
                return false;
            }
            u32 amount = (instruction >> 10) & 0xfff;
            if (instruction & (1u << 22))
            {
                amount <<= 12;
            }
            if (!amount || amount > expected_stack - stack_size)
            {
                return false;
            }
            cursor += 4;
            stack_size += amount;
            if (!object_mach_compact_action_append(descriptor, ACTION_CAPACITY, cursor, CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, amount))
            {
                return false;
            }
        }
        descriptor->prolog_size = cursor;
        return stack_size == expected_stack;
    }
    if (mode != 0x04000000 || (encoding & 0x00000f00) || (encoding & 0x000000e0))
    {
        return false;
    }
    u32 expected_pairs = encoding & 0x1f;
    u32 seen_pairs = 0;
    u32 stack_size = 0;
    u32 cursor = 0;
    bool frame_pair = false;
    while (cursor + 4 <= function_size)
    {
        u32 instruction = 0;
        memcpy(&instruction, code + cursor, sizeof(instruction));
        u32 next_cursor = cursor + 4;
        if ((instruction & UINT32_C(0xff8003ff)) == UINT32_C(0xd10003ff))
        {
            u32 amount = (instruction >> 10) & 0xfff;
            if (instruction & (1u << 22))
            {
                amount <<= 12;
            }
            if (!amount || stack_size > UINT32_MAX - amount ||
                !object_mach_compact_action_append(descriptor, ACTION_CAPACITY, next_cursor, CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, amount))
            {
                return false;
            }
            stack_size += amount;
            cursor = next_cursor;
            continue;
        }
        u32 pair_form = instruction & UINT32_C(0xffc00000);
        if (pair_form == UINT32_C(0xa9800000) || pair_form == UINT32_C(0xa9000000))
        {
            if (((instruction >> 5) & 31) != 31)
            {
                return false;
            }
            s32 signed_immediate = (s32)((instruction >> 15) & 0x7f);
            if (signed_immediate & 0x40)
            {
                signed_immediate -= 0x80;
            }
            signed_immediate *= 8;
            if (pair_form == UINT32_C(0xa9800000))
            {
                if (signed_immediate >= 0 || (u32)-signed_immediate > UINT32_MAX - stack_size ||
                    !object_mach_compact_action_append(descriptor, ACTION_CAPACITY, next_cursor, CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0,
                                                       (u32)-signed_immediate))
                {
                    return false;
                }
                stack_size += (u32)-signed_immediate;
                signed_immediate = 0;
            }
            if (signed_immediate < 0 || (u32)signed_immediate > stack_size || (u32)signed_immediate + 16 > stack_size)
            {
                return false;
            }
            u8 first = (u8)(instruction & 31);
            u8 second = (u8)((instruction >> 10) & 31);
            if ((first == 29 && second == 30) || (first == 30 && second == 29))
            {
                frame_pair = true;
            }
            else
            {
                u8 low = BUSTER_MIN(first, second);
                u32 pair_bit = low == 19 && BUSTER_MAX(first, second) == 20   ? 1u
                               : low == 21 && BUSTER_MAX(first, second) == 22 ? 2u
                               : low == 23 && BUSTER_MAX(first, second) == 24 ? 4u
                               : low == 25 && BUSTER_MAX(first, second) == 26 ? 8u
                               : low == 27 && BUSTER_MAX(first, second) == 28 ? 16u
                                                                             : 0;
                if (!pair_bit || (seen_pairs & pair_bit))
                {
                    return false;
                }
                seen_pairs |= pair_bit;
            }
            bool valid = object_mach_compact_action_append(descriptor, ACTION_CAPACITY, next_cursor, CODEGEN_UNWIND_ACTION_SAVE_REGISTER, first,
                                                           (u32)signed_immediate);
            valid = object_mach_compact_action_append(descriptor, ACTION_CAPACITY, next_cursor, CODEGEN_UNWIND_ACTION_SAVE_REGISTER, second,
                                                      (u32)signed_immediate + 8) && valid;
            if (!valid)
            {
                return false;
            }
            cursor = next_cursor;
            continue;
        }
        if ((instruction & UINT32_C(0xff8003ff)) == UINT32_C(0x910003fd))
        {
            u32 frame_offset = ((instruction >> 10) & 0xfff) << ((instruction & (1u << 22)) ? 12 : 0);
            if (!frame_pair || seen_pairs != expected_pairs || frame_offset > stack_size ||
                !object_mach_compact_action_append(descriptor, ACTION_CAPACITY, next_cursor, CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, 29, frame_offset))
            {
                return false;
            }
            descriptor->prolog_size = next_cursor;
            return true;
        }
        return false;
    }
    return false;
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
    if (!arena || !bytes.pointer || bytes.length < MACH_HEADER_SIZE || !object_read_u32(bytes, 0, &magic) || magic != 0xfeedfacf ||
        !object_read_u32(bytes, 4, &cpu_type) ||
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
    u64 command_table_end = MACH_HEADER_SIZE + commands_size;
    bool symbol_command_seen = false;
    for (u32 command_index = 0; command_index < command_count; command_index += 1)
    {
        u32 kind = 0;
        u32 size = 0;
        if (!object_read_u32(bytes, command, &kind) || !object_read_u32(bytes, command + 4, &size) || size < 8 || command > command_table_end ||
            size > command_table_end - command || command > bytes.length || size > bytes.length - command)
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
            if (size < 24 || symbol_command_seen || !object_read_u32(bytes, command + 8, &symbol_offset) || !object_read_u32(bytes, command + 12, &symbol_count) ||
                !object_read_u32(bytes, command + 16, &string_offset) || !object_read_u32(bytes, command + 20, &string_size))
            {
                return result;
            }
            symbol_command_seen = true;
        }
        command += size;
    }
    if (command != command_table_end || !mach_section_count || !symbol_command_seen || !symbol_offset || symbol_offset > bytes.length ||
        (u64)symbol_count * MACH_SYMBOL_SIZE > bytes.length - symbol_offset ||
        !string_size || string_offset > bytes.length || string_size > bytes.length - string_offset || bytes.pointer[string_offset] != 0)
    {
        return result;
    }
    if (!object_reader_arena_can_allocate_count(arena, mach_section_count, sizeof(u64), BUSTER_ALIGN_OF(u64)))
    {
        return result;
    }
    u64* mach_sections = arena_allocate(arena, u64, mach_section_count);
    if (!object_reader_arena_can_allocate_count(arena, mach_section_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return result;
    }
    u32* section_kinds = arena_allocate(arena, u32, mach_section_count);
    if (!object_reader_arena_can_allocate_count(arena, mach_section_count, sizeof(u64), BUSTER_ALIGN_OF(u64)))
    {
        return result;
    }
    u64* section_bases = arena_allocate(arena, u64, mach_section_count);
    if (!object_reader_arena_can_allocate_count(arena, mach_section_count, sizeof(u64), BUSTER_ALIGN_OF(u64)))
    {
        return result;
    }
    u64* section_addresses = arena_allocate(arena, u64, mach_section_count);
    if (!object_reader_arena_can_allocate_count(arena, mach_section_count, sizeof(bool), BUSTER_ALIGN_OF(bool)))
    {
        return result;
    }
    bool* compact_sections = arena_allocate(arena, bool, mach_section_count);
    if (!object_reader_arena_can_allocate_count(arena, mach_section_count, sizeof(bool), BUSTER_ALIGN_OF(bool)))
    {
        return result;
    }
    bool* eh_frame_sections = arena_allocate(arena, bool, mach_section_count);
    memset(compact_sections, 0, (u64)mach_section_count * sizeof(*compact_sections));
    memset(eh_frame_sections, 0, (u64)mach_section_count * sizeof(*eh_frame_sections));
    u64 section_sizes[OBJECT_SECTION_COUNT] = {0};
    u32 section_alignments[OBJECT_SECTION_COUNT];
    for (u32 alignment_kind = 0; alignment_kind < OBJECT_SECTION_COUNT; alignment_kind += 1)
    {
        section_alignments[alignment_kind] = 1;
    }
    u32 relocation_capacity = 0;
    u32 compact_entry_count = 0;
    bool has_compact = false;
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
                bool compact_unwind = string_equal(name, S8("__compact_unwind"));
                bool eh_frame = string_equal(name, S8("__eh_frame"));
                if (compact_unwind && (section_size % 32 || section_size / 32 > UINT32_MAX - compact_entry_count))
                {
                    return result;
                }
                has_compact |= compact_unwind;
                compact_entry_count += compact_unwind ? (u32)(section_size / 32) : 0;
                // The __DWARF sections must keep their debug identity so that
                // their contents and relocations do not land in read-only data.
                ObjectSectionKind debug_kind = object_debug_section_kind_from_name(name);
                ObjectSectionKind output_kind = eh_frame                         ? OBJECT_SECTION_UNWIND
                                                : debug_kind != OBJECT_SECTION_COUNT ? debug_kind
                                                : is_thread_local ? (zero_fill ? OBJECT_SECTION_THREAD_LOCAL_ZERO : OBJECT_SECTION_THREAD_LOCAL_DATA)
                                                : (flags & 0x80000000) || string_equal(name, S8("__text")) ? OBJECT_SECTION_TEXT
                                                : zero_fill || string_starts_with_sequence(name, S8("__bss")) ? OBJECT_SECTION_ZERO
                                                : string_starts_with_sequence(name, S8("__data")) ? OBJECT_SECTION_DATA
                                                                                                     : OBJECT_SECTION_READ_ONLY_DATA;
                u32 alignment = 1u << alignment_power;
                u64 output_size = compact_unwind ? 0 : section_size;
                u64 base = align_forward(section_sizes[output_kind], alignment);
                if (base < section_sizes[output_kind] || output_size > UINT64_MAX - base)
                {
                    return result;
                }
                mach_sections[output_section_index] = section;
                section_kinds[output_section_index] = (u32)output_kind;
                section_bases[output_section_index] = base;
                section_addresses[output_section_index] = address;
                compact_sections[output_section_index] = compact_unwind;
                eh_frame_sections[output_section_index] = eh_frame;
                section_sizes[output_kind] = base + output_size;
                section_alignments[output_kind] = BUSTER_MAX(section_alignments[output_kind], alignment);
                relocation_capacity += relocation_count;
                output_section_index += 1;
            }
        }
        command += size;
    }
    if (has_compact)
    {
        if (compact_entry_count > UINT32_MAX - relocation_capacity)
        {
            return result;
        }
        section_sizes[OBJECT_SECTION_UNWIND] = 0;
        relocation_capacity += compact_entry_count;
    }
    if (!object_reader_section_sizes_valid(arena, section_sizes))
    {
        return result;
    }
    u64 symbol_capacity = (u64)symbol_count + mach_section_count + compact_entry_count;
    if (symbol_capacity > UINT32_MAX)
    {
        return result;
    }
    if (!object_reader_arena_can_allocate_count(arena, OBJECT_SECTION_COUNT, sizeof(ObjectSection), BUSTER_ALIGN_OF(ObjectSection)))
    {
        return result;
    }
    result.sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    result.section_count = OBJECT_SECTION_COUNT;
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        bool zero_fill = object_section_kind_is_zero_fill((ObjectSectionKind)kind);
        if (!zero_fill && !object_reader_arena_can_allocate_count(arena, section_sizes[kind], sizeof(u8), BUSTER_ALIGN_OF(u8)))
        {
            return result;
        }
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
        if (section_type != 1 && section_type != 0x12 && size && !compact_sections[section_index] && !(has_compact && eh_frame_sections[section_index]))
        {
            ObjectSectionKind kind = (ObjectSectionKind)section_kinds[section_index];
            memcpy(result.sections[kind].data.pointer + section_bases[section_index], bytes.pointer + offset, size);
        }
    }
    if (!object_reader_arena_can_allocate_count(arena, symbol_capacity, sizeof(ObjectSymbol), BUSTER_ALIGN_OF(ObjectSymbol)))
    {
        return result;
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_capacity);
    if (!object_reader_arena_can_allocate_count(arena, symbol_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return result;
    }
    u32* symbol_map = arena_allocate(arena, u32, symbol_count);
    u64 symbol_name_bytes = 0;
    for (u32 source_index = 0; source_index < symbol_count; source_index += 1)
    {
        symbol_map[source_index] = UINT32_MAX;
        u64 source = symbol_offset + (u64)source_index * MACH_SYMBOL_SIZE;
        u32 name_offset = 0;
        u16 description = 0;
        u64 value = 0;
        if (!object_read_u32(bytes, source, &name_offset) || !object_read_u16(bytes, source + 6, &description) ||
            !object_read_u64(bytes, source + 8, &value))
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
        if (kind == 0x0e && (!section_number || section_number > mach_section_count))
        {
            return result;
        }
        if (kind == 0x0e && section_kinds[section_number - 1] == UINT32_MAX)
        {
            continue;
        }
        if (kind != 0 && kind != 0x0e)
        {
            continue;
        }
        String8 name = {0};
        if (!object_read_string_checked(bytes, string_offset, string_size, name_offset, &name))
        {
            return result;
        }
        if (name.length > UINT64_MAX - symbol_name_bytes)
        {
            return result;
        }
        symbol_name_bytes += name.length;
        if (name.length && name.pointer[0] == '_')
        {
            name.pointer += 1;
            name.length -= 1;
        }
        if (!name.length)
        {
            String8 prefix = S8(".Lmach.");
            if (!object_reader_arena_can_allocate_bytes(arena, prefix.length + 10, BUSTER_ALIGN_OF(char8)))
            {
                return result;
            }
            name = string_format(arena, S8(".Lmach.{u32}"), source_index);
        }
        if (!object_reader_arena_can_allocate_bytes(arena, name.length, BUSTER_ALIGN_OF(char8)))
        {
            return result;
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
            u64 relative_value = value - section_addresses[section_index];
            ObjectSectionKind symbol_kind = (ObjectSectionKind)section_kinds[section_index];
            u64 symbol_base = section_bases[section_index];
            if (symbol_base > result.sections[symbol_kind].virtual_size || relative_value > result.sections[symbol_kind].virtual_size - symbol_base ||
                relative_value > UINT64_MAX - symbol_base)
            {
                return result;
            }
            section_value = symbol_base + relative_value;
        }
        u32 reference_kind = description & 0x7;
        // Mach-O's reference type is a state/visibility flag, not a symbol
        // language-kind bit.  Keep the standard values and reject unknown or
        // state-inconsistent combinations instead of silently treating them
        // as data.  LLVM commonly leaves defined symbols at NON_LAZY (0),
        // while our writer emits DEFINED (2), so both are accepted for N_SECT.
        if (reference_kind > 5 ||
            (kind == 0 && (reference_kind == 2 || reference_kind == 3)) ||
            (kind == 0x0e && (reference_kind == 1 || reference_kind == 4 || reference_kind == 5)))
        {
            return result;
        }
        // PAGE/PAGEOFF references can name either a function address or data,
        // so ordinary undefined symbols remain data until a BRANCH26
        // relocation below upgrades them.  Genuine lazy-symbol-pointer
        // references carry explicit function evidence in n_desc (1 or 5).
        bool function_symbol = (kind == 0 && (reference_kind == 1 || reference_kind == 5)) ||
                               (kind == 0x0e && section_kinds[section_number - 1] == OBJECT_SECTION_TEXT);
        result.symbols[destination_index] = (ObjectSymbol){
            .name = string_duplicate_arena(arena, name, false),
            .value = section_value,
            .section = kind == 0x0e ? section_kinds[section_number - 1] : OBJECT_SECTION_UNDEFINED,
            .kind = function_symbol ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
            .global = (symbol_type & 1) != 0,
        };
        symbol_map[source_index] = destination_index;
    }
    if (!object_reader_arena_can_allocate_count(arena, mach_section_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return result;
    }
    u32* section_symbol_maps = arena_allocate(arena, u32, mach_section_count);
    for (u32 section_index = 0; section_index < mach_section_count; section_index += 1)
    {
        section_symbol_maps[section_index] = UINT32_MAX;
    }
    if (!object_reader_arena_can_allocate_count(arena, relocation_capacity, sizeof(ObjectRelocation), BUSTER_ALIGN_OF(ObjectRelocation)))
    {
        return result;
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
        if (compact_sections[section_index] || (has_compact && eh_frame_sections[section_index]))
        {
            continue;
        }
        bool pending_addend = false;
        s64 pending_addend_value = 0;
        u32 pending_addend_offset = 0;
        for (u32 relocation_index = 0; relocation_index < relocation_count; relocation_index += 1)
        {
            u64 relocation = relocation_offset + (u64)relocation_index * MACH_RELOCATION_SIZE;
            u32 source_offset_u32 = 0;
            u32 information = 0;
            object_read_u32(bytes, relocation, &source_offset_u32);
            object_read_u32(bytes, relocation + 4, &information);
            s32 source_offset = (s32)source_offset_u32;
            u32 source_symbol = information & 0x00ffffff;
            bool pc_relative = (information & (1u << 24)) != 0;
            bool external = (information & (1u << 27)) != 0;
            u32 relocation_type = information >> 28;
            u32 length = (information >> 25) & 0x3;
            if (source_offset < 0 || (u64)source_offset > section_size)
            {
                return result;
            }
            u32 relocation_width = 1u << length;
            if (object_section_kind_is_zero_fill((ObjectSectionKind)section_kinds[section_index]) ||
                relocation_width > section_size - (u64)source_offset)
            {
                return result;
            }
            ObjectSectionKind current_section_kind = (ObjectSectionKind)section_kinds[section_index];
            if (target.cpu_arch == CPU_ARCH_AARCH64 && relocation_type == 1 && length == 2 &&
                !object_section_kind_is_zero_fill(current_section_kind))
            {
                u32 next_source_offset = 0;
                u32 next_information = 0;
                if (relocation_index + 1 >= relocation_count || !external || pc_relative || (source_offset_u32 & 3) ||
                    section_bases[section_index] > UINT64_MAX - source_offset_u32 ||
                    ((section_bases[section_index] + source_offset_u32) & 3) ||
                    result.sections[section_kinds[section_index]].alignment < 4 ||
                    !object_read_u32(bytes, relocation + MACH_RELOCATION_SIZE, &next_source_offset) ||
                    !object_read_u32(bytes, relocation + MACH_RELOCATION_SIZE + 4, &next_information) || next_source_offset != source_offset_u32 ||
                    (next_information >> 28) != 0 || ((next_information >> 25) & 0x3) != 2 || !(next_information & (1u << 27)) ||
                    (next_information & (1u << 24)))
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                u32 subtractor_source_symbol = source_symbol;
                u32 target_source_symbol = next_information & 0x00ffffff;
                if (subtractor_source_symbol >= symbol_count || target_source_symbol >= symbol_count ||
                    symbol_map[subtractor_source_symbol] == UINT32_MAX || symbol_map[target_source_symbol] == UINT32_MAX)
                {
                    return result;
                }
                ObjectSymbol* subtractor = &result.symbols[symbol_map[subtractor_source_symbol]];
                u64 place = section_bases[section_index] + (u64)source_offset_u32;
                if (place < section_bases[section_index] || subtractor->section != (u32)current_section_kind || subtractor->value != place)
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                u32 stored = 0;
                if (!object_read_u32(bytes, (u64)raw_offset + (u32)source_offset, &stored))
                {
                    return result;
                }
                result.relocations[result.relocation_count++] = (ObjectRelocation){
                    .addend = (s32)stored,
                    .offset = place,
                    .section = (u32)current_section_kind,
                    .symbol = symbol_map[target_source_symbol],
                    .kind = OBJECT_RELOCATION_AARCH64_PREL32,
                };
                relocation_index += 1;
                continue;
            }
            if (target.cpu_arch == CPU_ARCH_AARCH64 && relocation_type == 10)
            {
                u32 next_source_offset = 0;
                u32 next_information = 0;
                if (pending_addend || pc_relative || external || length != 2 || relocation_index + 1 >= relocation_count ||
                    !a64_signed_scaled_immediate_decode(source_symbol, 24, 0, &pending_addend_value) ||
                    !object_read_u32(bytes, relocation + MACH_RELOCATION_SIZE, &next_source_offset) ||
                    !object_read_u32(bytes, relocation + MACH_RELOCATION_SIZE + 4, &next_information) || next_source_offset != source_offset_u32 ||
                    ((next_information >> 28) != 2 && (next_information >> 28) != 3 && (next_information >> 28) != 4 &&
                     (next_information >> 28) != 8 && (next_information >> 28) != 9))
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                pending_addend = true;
                pending_addend_offset = source_offset_u32;
                continue;
            }
            if (pending_addend && (target.cpu_arch != CPU_ARCH_AARCH64 || source_offset_u32 != pending_addend_offset ||
                                   (relocation_type != 2 && relocation_type != 3 && relocation_type != 4 && relocation_type != 8 && relocation_type != 9)))
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
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
                if (section_kinds[referenced_section] == UINT32_MAX)
                {
                    return result;
                }
                destination_symbol = section_symbol_maps[referenced_section];
                if (destination_symbol == UINT32_MAX)
                {
                    String8 prefix = S8(".Lmach_section.");
                    if (!object_reader_arena_can_allocate_bytes(arena, prefix.length + 10, BUSTER_ALIGN_OF(char8)))
                    {
                        return result;
                    }
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
                              : relocation_type == 0 && length == 2 && object_section_kind_is_debug(current_section_kind)
                                  ? OBJECT_RELOCATION_ABSOLUTE32
                              : relocation_type == 2 && length == 2 ? OBJECT_RELOCATION_AARCH64_CALL26
                              : relocation_type == 3 && length == 2 ? OBJECT_RELOCATION_AARCH64_MACH_PAGE21
                              : relocation_type == 4 && length == 2 ? OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12
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
                if (relocation_type == 0 && length == 2 && object_section_kind_is_debug(current_section_kind))
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
                    u32 stored = 0;
                    A64MCInst decoded = {0};
                    if (!pc_relative || !external || ((section_bases[section_index] + source_offset_u32) & 3) ||
                        result.sections[section_kinds[section_index]].alignment < 4 ||
                        !object_read_u32(bytes, (u64)raw_offset + (u32)source_offset, &stored) ||
                        !a64_mc_decode(stored, &decoded) || (decoded.opcode != A64_OPCODE_B && decoded.opcode != A64_OPCODE_BL) ||
                        decoded.operands[0].value != 0)
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    output_kind = decoded.opcode == A64_OPCODE_B ? OBJECT_RELOCATION_AARCH64_JUMP26 : OBJECT_RELOCATION_AARCH64_CALL26;
                    addend = pending_addend ? pending_addend_value : 0;
                    pending_addend = false;
                    result.symbols[destination_symbol].kind = OBJECT_SYMBOL_FUNCTION;
                }
                if (relocation_type == 3 && length == 2)
                {
                    u32 stored = 0;
                    if (!pc_relative || !external || (source_offset_u32 & 3) || result.sections[section_kinds[section_index]].alignment < 4 ||
                        !object_read_u32(bytes, (u64)raw_offset + source_offset_u32, &stored) || !object_mach_page21_instruction_valid(stored))
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    addend = pending_addend ? pending_addend_value : 0;
                    pending_addend = false;
                }
                if (relocation_type == 4 && length == 2)
                {
                    u32 stored = 0;
                    u32 shift = 0;
                    if (pc_relative || !external || (source_offset_u32 & 3) || result.sections[section_kinds[section_index]].alignment < 4 ||
                        !object_read_u32(bytes, (u64)raw_offset + source_offset_u32, &stored) ||
                        !object_mach_pageoff12_shift(stored, &shift))
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    addend = pending_addend ? pending_addend_value : 0;
                    pending_addend = false;
                }
                if (relocation_type == 8 && length == 2)
                {
                    u32 stored = 0;
                    if (!pc_relative || !external || (source_offset_u32 & 3) || result.sections[section_kinds[section_index]].alignment < 4 ||
                        !object_read_u32(bytes, (u64)raw_offset + source_offset_u32, &stored) || !object_mach_page21_instruction_valid(stored))
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    addend = pending_addend ? pending_addend_value : 0;
                    pending_addend = false;
                }
                if (relocation_type == 9 && length == 2)
                {
                    u32 stored = 0;
                    u32 shift = 0;
                    if (pc_relative || !external || (source_offset_u32 & 3) || result.sections[section_kinds[section_index]].alignment < 4 ||
                        !object_read_u32(bytes, (u64)raw_offset + source_offset_u32, &stored) || !object_mach_pageoff12_shift(stored, &shift) || shift != 3 ||
                        (stored & UINT32_C(0xffc00000)) != UINT32_C(0xf9400000))
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    addend = pending_addend ? pending_addend_value : 0;
                    pending_addend = false;
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
        if (pending_addend)
        {
            result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
            return result;
        }
    }
    u32* text_symbol_map = 0;
    u64 text_symbol_map_capacity = 0;
    if (has_compact)
    {
        u64 desired_capacity = (u64)result.symbol_count * 2 + 1;
        text_symbol_map_capacity = 1;
        while (text_symbol_map_capacity < desired_capacity)
        {
            if (text_symbol_map_capacity > UINT64_MAX / 2)
            {
                return result;
            }
            text_symbol_map_capacity *= 2;
        }
        if (!object_reader_arena_can_allocate_count(arena, text_symbol_map_capacity, sizeof(u32), BUSTER_ALIGN_OF(u32)))
        {
            return result;
        }
        text_symbol_map = arena_allocate(arena, u32, text_symbol_map_capacity);
        memset(text_symbol_map, 0xff, sizeof(*text_symbol_map) * text_symbol_map_capacity);
        for (u32 symbol_index = 0; symbol_index < result.symbol_count; symbol_index += 1)
        {
            if (result.symbols[symbol_index].section != OBJECT_SECTION_TEXT)
            {
                continue;
            }
            u64 bucket = object_reader_hash_u64(result.symbols[symbol_index].value) & (text_symbol_map_capacity - 1);
            for (u64 probe = 0; probe < text_symbol_map_capacity; probe += 1)
            {
                u32 candidate = text_symbol_map[bucket];
                if (candidate == UINT32_MAX)
                {
                    text_symbol_map[bucket] = symbol_index;
                    break;
                }
                if (result.symbols[candidate].value == result.symbols[symbol_index].value)
                {
                    break;
                }
                bucket = (bucket + 1) & (text_symbol_map_capacity - 1);
            }
        }
    }
    if (has_compact)
    {
        if (!object_reader_arena_can_allocate_count(arena, compact_entry_count, sizeof(CodegenFunctionDescriptor), BUSTER_ALIGN_OF(CodegenFunctionDescriptor)))
        {
            return result;
        }
        CodegenFunctionDescriptor* functions = arena_allocate(arena, CodegenFunctionDescriptor, compact_entry_count);
        if (!object_reader_arena_can_allocate_count(arena, compact_entry_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
        {
            return result;
        }
        u32* function_symbols = arena_allocate(arena, u32, compact_entry_count);
        u32 function_count = 0;
        for (u32 section_index = 0; section_index < mach_section_count; section_index += 1)
        {
            if (!compact_sections[section_index])
            {
                continue;
            }
            u64 section = mach_sections[section_index];
            u64 section_size = 0;
            u32 raw_offset = 0;
            u32 relocation_offset = 0;
            u32 relocation_count = 0;
            object_read_u64(bytes, section + 40, &section_size);
            object_read_u32(bytes, section + 48, &raw_offset);
            object_read_u32(bytes, section + 56, &relocation_offset);
            object_read_u32(bytes, section + 60, &relocation_count);
            u32 entry_count = (u32)(section_size / 32);
            if (relocation_count != entry_count)
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
            if (entry_count && !object_reader_arena_can_allocate_count(arena, entry_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
            {
                return result;
            }
            u32* compact_relocation_map = entry_count ? arena_allocate(arena, u32, entry_count) : 0;
            for (u32 entry_index = 0; entry_index < entry_count; entry_index += 1)
            {
                compact_relocation_map[entry_index] = UINT32_MAX;
            }
            for (u32 relocation_index = 0; relocation_index < relocation_count; relocation_index += 1)
            {
                u64 relocation = relocation_offset + (u64)relocation_index * MACH_RELOCATION_SIZE;
                u32 source_offset = 0;
                if (!object_read_u32(bytes, relocation, &source_offset) || source_offset % 32 != 0 || source_offset / 32 >= entry_count)
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                u32 compact_entry_index = source_offset / 32;
                if (compact_relocation_map[compact_entry_index] != UINT32_MAX)
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                compact_relocation_map[compact_entry_index] = relocation_index;
            }
            for (u32 entry_index = 0; entry_index < entry_count; entry_index += 1)
            {
                u32 entry_offset = entry_index * 32;
                u64 entry = (u64)raw_offset + entry_offset;
                u32 function_size = 0;
                u32 encoding = 0;
                u64 personality = 0;
                u64 lsda = 0;
                if (!object_read_u32(bytes, entry + 8, &function_size) || !object_read_u32(bytes, entry + 12, &encoding) ||
                    !object_read_u64(bytes, entry + 16, &personality) || !object_read_u64(bytes, entry + 24, &lsda) || personality || lsda)
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                u32 relocation_index = compact_relocation_map[entry_index];
                u32 information = 0;
                if (relocation_index == UINT32_MAX ||
                    !object_read_u32(bytes, relocation_offset + (u64)relocation_index * MACH_RELOCATION_SIZE + 4, &information) ||
                    (information >> 28) != 0 || ((information >> 25) & 0x3) != 3 || (information & (1u << 24)))
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                u32 source_symbol = information & 0x00ffffff;
                bool external = (information & (1u << 27)) != 0;
                u64 stored = 0;
                object_read_u64(bytes, entry, &stored);
                u64 function_offset = 0;
                if (external)
                {
                    if (source_symbol >= symbol_count || symbol_map[source_symbol] == UINT32_MAX)
                    {
                        return result;
                    }
                    ObjectSymbol* symbol = &result.symbols[symbol_map[source_symbol]];
                    if (symbol->section != OBJECT_SECTION_TEXT || stored > UINT64_MAX - symbol->value)
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    function_offset = symbol->value + stored;
                }
                else
                {
                    if (!source_symbol || source_symbol > mach_section_count)
                    {
                        return result;
                    }
                    u32 referenced_section = source_symbol - 1;
                    if (section_kinds[referenced_section] != OBJECT_SECTION_TEXT || stored < section_addresses[referenced_section])
                    {
                        result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                        return result;
                    }
                    u64 local_offset = stored - section_addresses[referenced_section];
                    if (local_offset > UINT64_MAX - section_bases[referenced_section])
                    {
                        return result;
                    }
                    function_offset = section_bases[referenced_section] + local_offset;
                }
                if (function_offset > UINT32_MAX || function_offset > result.sections[OBJECT_SECTION_TEXT].data.length ||
                    function_size > result.sections[OBJECT_SECTION_TEXT].data.length - function_offset ||
                    !object_mach_compact_decode(arena, result.sections[OBJECT_SECTION_TEXT].data, (u32)function_offset, function_size, encoding, target,
                                                &functions[function_count]))
                {
                    result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    return result;
                }
                u32 function_symbol = UINT32_MAX;
                u64 bucket = object_reader_hash_u64(function_offset) & (text_symbol_map_capacity - 1);
                for (u64 probe = 0; probe < text_symbol_map_capacity; probe += 1)
                {
                    u32 symbol_index = text_symbol_map[bucket];
                    if (symbol_index == UINT32_MAX)
                    {
                        break;
                    }
                    if (result.symbols[symbol_index].value == function_offset)
                    {
                        function_symbol = symbol_index;
                        result.symbols[symbol_index].kind = OBJECT_SYMBOL_FUNCTION;
                        break;
                    }
                    bucket = (bucket + 1) & (text_symbol_map_capacity - 1);
                }
                if (function_symbol == UINT32_MAX)
                {
                    String8 prefix = S8(".Lmach_unwind.");
                    if (!object_reader_arena_can_allocate_bytes(arena, prefix.length + 10, BUSTER_ALIGN_OF(char8)))
                    {
                        return result;
                    }
                    function_symbol = result.symbol_count++;
                    result.symbols[function_symbol] = (ObjectSymbol){
                        .name = string_format(arena, S8(".Lmach_unwind.{u32}"), function_count),
                        .value = function_offset,
                        .size = function_size,
                        .section = OBJECT_SECTION_TEXT,
                        .kind = OBJECT_SYMBOL_FUNCTION,
                    };
                }
                function_symbols[function_count++] = function_symbol;
            }
        }
        u64 cfi_capacity = 64;
        for (u32 function_index = 0; function_index < function_count; function_index += 1)
        {
            u64 action_bytes = (u64)functions[function_index].unwind_action_count * 24;
            if (action_bytes > UINT64_MAX - 64 || cfi_capacity > UINT64_MAX - 64 - action_bytes)
            {
                return result;
            }
            cfi_capacity += 64 + action_bytes;
        }
        u64 cfi_position = 0;
        if (!object_reader_arena_position_after(arena, arena->position, cfi_capacity, BUSTER_ALIGN_OF(u8), &cfi_position) ||
            !object_reader_arena_position_after(arena, cfi_position, (u64)function_count * sizeof(DwarfCfiRelocation), BUSTER_ALIGN_OF(DwarfCfiRelocation), &cfi_position))
        {
            return result;
        }
        DwarfCfiResult cfi = dwarf_cfi_build(arena, (DwarfCfiInput){
                                                        .functions = functions,
                                                        .target = target,
                                                        .function_count = function_count,
                                                    });
        if (!cfi.valid || cfi.relocation_count != function_count)
        {
            result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
            return result;
        }
        result.sections[OBJECT_SECTION_UNWIND].data = cfi.bytes;
        result.sections[OBJECT_SECTION_UNWIND].virtual_size = cfi.bytes.length;
        result.sections[OBJECT_SECTION_UNWIND].alignment = object_section_default_alignment(OBJECT_SECTION_UNWIND);
        for (u32 relocation_index = 0; relocation_index < cfi.relocation_count; relocation_index += 1)
        {
            DwarfCfiRelocation relocation = cfi.relocations[relocation_index];
            result.relocations[result.relocation_count++] = (ObjectRelocation){
                .offset = relocation.offset,
                .section = OBJECT_SECTION_UNWIND,
                .symbol = function_symbols[relocation.function],
                .kind = target.cpu_arch == CPU_ARCH_X86_64 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_PREL32,
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
            ObjectFile result = object_read_coff(arena, bytes, target);
            if (result.error == OBJECT_ERROR_NONE)
            {
                // COFF does not carry the source-module boundary used by the
                // PDB DBI stream.  A standalone read object is nevertheless
                // one translation unit, so retain a deterministic synthetic
                // module rather than collapsing its CodeView into the linker's
                // anonymous stream.
                if (result.sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.length &&
                    (!object_reader_arena_can_allocate_count(arena, 1, sizeof(ObjectDebugModule), BUSTER_ALIGN_OF(ObjectDebugModule)) ||
                     !object_reader_arena_can_allocate_bytes(arena, S8("object.obj").length, BUSTER_ALIGN_OF(char8))))
                {
                    result.error = OBJECT_ERROR_INVALID_INPUT;
                    return result;
                }
                object_debug_module_set(arena, &result, S8("object.obj"), result.sections[OBJECT_SECTION_TEXT].data.length);
            }
            return result;
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
    if (length && !bytes)
    {
        return false;
    }
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
    if (bytes.length && !bytes.pointer)
    {
        return false;
    }
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
    if (!arena || !bytes.pointer || bytes.length < sizeof(archive_magic) - 1 || memcmp(bytes.pointer, archive_magic, sizeof(archive_magic) - 1) != 0)
    {
        return result;
    }
    u64 member_capacity_u64 = bytes.length / 60;
    if (member_capacity_u64 > UINT32_MAX)
    {
        return result;
    }
    u32 member_capacity = (u32)member_capacity_u64;
    if (!object_reader_arena_can_allocate_count(arena, member_capacity, sizeof(ObjectFile), BUSTER_ALIGN_OF(ObjectFile)))
    {
        return result;
    }
    result.objects = arena_allocate(arena, ObjectFile, member_capacity);
    if (!object_reader_arena_can_allocate_count(arena, member_capacity, sizeof(String8), BUSTER_ALIGN_OF(String8)))
    {
        return result;
    }
    result.member_names = arena_allocate(arena, String8, member_capacity);
    String8 long_names = {0};
    u64 member_name_bytes = 0;
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
            if (name_offset && long_names.pointer[name_offset - 1] != '\n' && long_names.pointer[name_offset - 1] != 0)
            {
                return result;
            }
            u64 name_length = 0;
            u64 name_remaining = long_names.length - name_offset;
            while (name_length < name_remaining && long_names.pointer[name_offset + name_length] != '\n' &&
                   long_names.pointer[name_offset + name_length] != 0)
            {
                name_length += 1;
            }
            if (name_length == name_remaining)
            {
                return result;
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
            if (member_name.length > UINT64_MAX - member_name_bytes)
            {
                return result;
            }
            member_name_bytes += member_name.length;
            if (!object_reader_arena_can_allocate_bytes(arena, member_name.length, BUSTER_ALIGN_OF(char8)))
            {
                return result;
            }
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
            if (cursor >= bytes.length || bytes.pointer[cursor] != '\n')
            {
                return result;
            }
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

#if BUSTER_FUZZ_AVAILABLE
enum
{
    OBJECT_FUZZ_MAX_GENERATED_BYTES = BUSTER_MB(1),
    OBJECT_FUZZ_MUTATION_COUNT = 24,
};

BUSTER_GLOBAL_LOCAL bool object_fuzz_archive_decimal_field(u8* field, u32 width, u64 value)
{
    memset(field, ' ', width);
    u32 cursor = width;
    do
    {
        if (!cursor)
        {
            return false;
        }
        field[--cursor] = (u8)('0' + value % 10);
        value /= 10;
    } while (value);
    return true;
}

BUSTER_GLOBAL_LOCAL ByteSlice object_fuzz_archive_make(Arena* arena, ByteSlice member, u32 archive_kind)
{
    String8 name = S8("seed.o");
    String8 long_name = S8("long-seed-object.o/\n");
    bool bsd_name = archive_kind == 1;
    bool gnu_long_name = archive_kind == 2;
    u64 first_member_size = gnu_long_name ? long_name.length : member.length + (bsd_name ? name.length : 0);
    u64 total_size = sizeof("!<arch>\n") - 1 + 60 + first_member_size + (first_member_size & 1) +
                     (gnu_long_name ? 60 + member.length + (member.length & 1) : 0);
    if (!arena || !member.pointer || member.length > OBJECT_FUZZ_MAX_GENERATED_BYTES || first_member_size > UINT64_MAX - 68 ||
        total_size > OBJECT_FUZZ_MAX_GENERATED_BYTES)
    {
        return (ByteSlice){0};
    }
    u8* bytes = arena_allocate(arena, u8, total_size);
    memset(bytes, ' ', total_size);
    memcpy(bytes, "!<arch>\n", sizeof("!<arch>\n") - 1);
    u8* header = bytes + sizeof("!<arch>\n") - 1;
    if (gnu_long_name)
    {
        memcpy(header, "//", 2);
    }
    else if (bsd_name)
    {
        memcpy(header, "#1/6", 4);
    }
    else
    {
        memcpy(header, "seed.o/", 7);
    }
    header[58] = '`';
    header[59] = '\n';
    if (!object_fuzz_archive_decimal_field(header + 48, 10, first_member_size))
    {
        return (ByteSlice){0};
    }
    u8* payload = header + 60;
    if (gnu_long_name)
    {
        memcpy(payload, long_name.pointer, long_name.length);
        payload += long_name.length;
        if (first_member_size & 1)
        {
            *payload++ = '\n';
        }
        header = payload;
        memcpy(header, "/0", 2);
        header[58] = '`';
        header[59] = '\n';
        if (!object_fuzz_archive_decimal_field(header + 48, 10, member.length))
        {
            return (ByteSlice){0};
        }
        payload = header + 60;
        memcpy(payload, member.pointer, member.length);
        if (member.length & 1)
        {
            payload[member.length] = '\n';
        }
        return (ByteSlice){.pointer = bytes, .length = total_size};
    }
    if (bsd_name)
    {
        memcpy(payload, name.pointer, name.length);
        payload += name.length;
    }
    memcpy(payload, member.pointer, member.length);
    if (first_member_size & 1)
    {
        bytes[sizeof("!<arch>\n") - 1 + 60 + first_member_size] = '\n';
    }
    return (ByteSlice){.pointer = bytes, .length = total_size};
}

BUSTER_GLOBAL_LOCAL void object_fuzz_write_u16(u8* bytes, u64 length, u64 offset, u16 value)
{
    if (offset <= length && sizeof(value) <= length - offset)
    {
        memcpy(bytes + offset, &value, sizeof(value));
    }
}

BUSTER_GLOBAL_LOCAL void object_fuzz_write_u8(u8* bytes, u64 length, u64 offset, u8 value)
{
    if (offset < length)
    {
        bytes[offset] = value;
    }
}

BUSTER_GLOBAL_LOCAL void object_fuzz_write_u32(u8* bytes, u64 length, u64 offset, u32 value)
{
    if (offset <= length && sizeof(value) <= length - offset)
    {
        memcpy(bytes + offset, &value, sizeof(value));
    }
}

BUSTER_GLOBAL_LOCAL void object_fuzz_write_u64(u8* bytes, u64 length, u64 offset, u64 value)
{
    if (offset <= length && sizeof(value) <= length - offset)
    {
        memcpy(bytes + offset, &value, sizeof(value));
    }
}

BUSTER_GLOBAL_LOCAL bool object_fuzz_elf_find_section(ByteSlice bytes, u32 type, u64* header, u64* offset, u64* size)
{
    if (!bytes.pointer || bytes.length < 64)
    {
        return false;
    }
    u64 section_table = 0;
    u16 section_count = 0;
    if (!object_read_u64(bytes, 40, &section_table) || !object_read_u16(bytes, 60, &section_count) || section_table > bytes.length ||
        (u64)section_count * 64 > bytes.length - section_table)
    {
        return false;
    }
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * 64;
        u32 section_type = 0;
        u64 section_offset = 0;
        u64 section_size = 0;
        if (!object_read_u32(bytes, section + 4, &section_type) || !object_read_u64(bytes, section + 24, &section_offset) ||
            !object_read_u64(bytes, section + 32, &section_size))
        {
            return false;
        }
        if (section_type == type)
        {
            if (section_offset > bytes.length || section_size > bytes.length - section_offset)
            {
                return false;
            }
            if (header)
            {
                *header = section;
            }
            if (offset)
            {
                *offset = section_offset;
            }
            if (size)
            {
                *size = section_size;
            }
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool object_fuzz_mach_find_command(ByteSlice bytes, u32 kind, u64* result)
{
    if (!bytes.pointer || bytes.length < 32)
    {
        return false;
    }
    u32 command_count = 0;
    if (!object_read_u32(bytes, 16, &command_count))
    {
        return false;
    }
    u64 command = 32;
    for (u32 command_index = 0; command_index < command_count; command_index += 1)
    {
        u32 command_kind = 0;
        u32 command_size = 0;
        if (command > bytes.length || 8 > bytes.length - command || !object_read_u32(bytes, command, &command_kind) ||
            !object_read_u32(bytes, command + 4, &command_size) || command_size < 8 || command_size > bytes.length - command)
        {
            return false;
        }
        if (command_kind == kind)
        {
            *result = command;
            return true;
        }
        command += command_size;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL ByteSlice object_fuzz_mutate(Arena* arena, ByteSlice seed, const u8* input, u64 input_size, ObjectFormat format, bool archive,
                                                  u32 mutation_index)
{
    ByteSlice result = seed;
    if (!seed.pointer || !seed.length)
    {
        return result;
    }
    if (mutation_index == 0)
    {
        result.length -= 1;
        return result;
    }
    if (mutation_index == 1)
    {
        result.length = BUSTER_MAX((u64)1, seed.length / 2);
        return result;
    }
    u8* bytes = arena_allocate(arena, u8, seed.length);
    if (!bytes)
    {
        return result;
    }
    memcpy(bytes, seed.pointer, seed.length);
    result.pointer = bytes;
    u64 input_value = 0;
    if (input_size)
    {
        u64 input_index = ((u64)mutation_index * 17 + 5) % input_size;
        for (u32 byte_index = 0; byte_index < 8; byte_index += 1)
        {
            input_value = (input_value << 8) | input[(input_index + byte_index) % input_size];
        }
    }
    if (mutation_index == 2 || mutation_index == 3 || mutation_index == 7)
    {
        u8 value = input_size ? (u8)input_value : 0xa5;
        u64 offset = input_size ? input_value % seed.length : (u64)mutation_index % seed.length;
        if (mutation_index == 2)
        {
            bytes[offset] ^= value ? value : 0xa5;
        }
        else if (mutation_index == 3)
        {
            u64 count = BUSTER_MIN((u64)8, seed.length - offset);
            for (u64 index = 0; index < count; index += 1)
            {
                bytes[offset + index] = (u8)(value + index * 17 + mutation_index);
            }
        }
        else
        {
            result.length = BUSTER_MAX((u64)1, (u64)value % seed.length);
        }
        return result;
    }
    if (archive)
    {
        if (mutation_index == 4)
        {
            memset(bytes + 8 + 48, '9', BUSTER_MIN((u64)10, seed.length > 56 ? seed.length - 56 : 0));
        }
        else if (mutation_index == 5)
        {
            memset(bytes + 8, '/', BUSTER_MIN((u64)16, seed.length > 8 ? seed.length - 8 : 0));
        }
        else if (mutation_index == 6)
        {
            u64 offset = 8 + 58;
            if (offset < seed.length)
            {
                bytes[offset] ^= 1;
            }
        }
        else if (mutation_index == 8)
        {
            if (seed.length >= 8)
            {
                memset(bytes + 8, ' ', BUSTER_MIN((u64)16, seed.length - 8));
            }
            object_fuzz_write_u8(bytes, seed.length, 8, '/');
            object_fuzz_write_u8(bytes, seed.length, 9, '1');
        }
        else if (mutation_index == 9)
        {
            if (seed.length >= 56)
            {
                memset(bytes + 8 + 48, '9', BUSTER_MIN((u64)10, seed.length - 56));
            }
        }
        else if (mutation_index == 10)
        {
            object_fuzz_write_u8(bytes, seed.length, 8 + 58, '`' ^ 1);
        }
        else if (mutation_index == 11)
        {
            object_fuzz_write_u8(bytes, seed.length, 8 + 59, 'x');
        }
        else if (mutation_index == 12)
        {
            object_fuzz_write_u8(bytes, seed.length, 8 + 60, 'x');
        }
        else if (mutation_index == 13)
        {
            if (seed.length > 68)
            {
                u64 table_offset = input_size ? input_value % BUSTER_MIN((u64)16, seed.length - 68) : 0;
                object_fuzz_write_u8(bytes, seed.length, 8 + 60 + table_offset, 0);
            }
        }
        else if (mutation_index == 14)
        {
            if (seed.length > 8)
            {
                u64 truncation = input_size ? input_value % BUSTER_MIN((u64)32, seed.length - 8) : 1;
                result.length = BUSTER_MAX((u64)8, seed.length - truncation);
            }
        }
        else
        {
            u64 offset = input_size ? input_value % seed.length : (u64)mutation_index % seed.length;
            bytes[offset] ^= (u8)(0x5a + mutation_index);
        }
        return result;
    }
    switch (format)
    {
    case OBJECT_FORMAT_ELF64:
        if (mutation_index == 4)
        {
            object_fuzz_write_u64(bytes, seed.length, 40, UINT64_MAX);
        }
        else if (mutation_index == 5)
        {
            object_fuzz_write_u16(bytes, seed.length, 60, UINT16_MAX);
        }
        else if (mutation_index == 6)
        {
            object_fuzz_write_u64(bytes, seed.length, 48, UINT64_MAX);
        }
        else if (mutation_index == 8)
        {
            u64 symbol_offset = 0;
            u64 symbol_size = 0;
            if (object_fuzz_elf_find_section(seed, 2, 0, &symbol_offset, &symbol_size) && symbol_size >= 48)
            {
                object_fuzz_write_u64(bytes, seed.length, symbol_offset + 24 + 16, UINT64_MAX);
            }
        }
        else if (mutation_index == 9)
        {
            u64 symbol_offset = 0;
            u64 symbol_size = 0;
            if (object_fuzz_elf_find_section(seed, 2, 0, &symbol_offset, &symbol_size) && symbol_size >= 48)
            {
                object_fuzz_write_u16(bytes, seed.length, symbol_offset + 24 + 6, UINT16_MAX);
            }
        }
        else if (mutation_index == 10)
        {
            u64 relocation_offset = 0;
            u64 relocation_size = 0;
            if (object_fuzz_elf_find_section(seed, 4, 0, &relocation_offset, &relocation_size) && relocation_size >= 24)
            {
                object_fuzz_write_u64(bytes, seed.length, relocation_offset + 8, UINT64_MAX);
            }
        }
        else if (mutation_index == 11)
        {
            u64 string_offset = 0;
            u64 string_size = 0;
            if (object_fuzz_elf_find_section(seed, 3, 0, &string_offset, &string_size) && string_size)
            {
                object_fuzz_write_u8(bytes, seed.length, string_offset + string_size - 1, 'x');
            }
        }
        else if (mutation_index == 12)
        {
            u64 symbol_header = 0;
            if (object_fuzz_elf_find_section(seed, 2, &symbol_header, 0, 0))
            {
                object_fuzz_write_u32(bytes, seed.length, symbol_header + 40, UINT32_MAX);
            }
        }
        else if (mutation_index == 13)
        {
            u64 relocation_offset = 0;
            u64 relocation_size = 0;
            if (object_fuzz_elf_find_section(seed, 4, 0, &relocation_offset, &relocation_size) && relocation_size >= 24)
            {
                object_fuzz_write_u64(bytes, seed.length, relocation_offset, UINT64_MAX);
            }
        }
        else if (mutation_index == 14)
        {
            u64 section_offset = 0;
            u64 section_size = 0;
            if (object_fuzz_elf_find_section(seed, 1, 0, &section_offset, &section_size) && section_size)
            {
                object_fuzz_write_u8(bytes, seed.length, section_offset + (input_size ? input_value % section_size : 0), (u8)input_value);
            }
        }
        else if (mutation_index == 15)
        {
            u64 section_header = 0;
            if (object_fuzz_elf_find_section(seed, 4, &section_header, 0, 0))
            {
                object_fuzz_write_u64(bytes, seed.length, section_header + 32, UINT64_MAX);
            }
        }
        else if (mutation_index == 16)
        {
            u64 symbol_offset = 0;
            u64 symbol_size = 0;
            if (object_fuzz_elf_find_section(seed, 2, 0, &symbol_offset, &symbol_size) && symbol_size >= 48)
            {
                object_fuzz_write_u16(bytes, seed.length, symbol_offset + 24 + 6, 0x7fff);
            }
        }
        else if (mutation_index == 17)
        {
            u64 symbol_offset = 0;
            u64 symbol_size = 0;
            if (object_fuzz_elf_find_section(seed, 2, 0, &symbol_offset, &symbol_size) && symbol_size >= 48)
            {
                object_fuzz_write_u32(bytes, seed.length, symbol_offset + 24, UINT32_MAX);
            }
        }
        else if (mutation_index == 18)
        {
            u64 relocation_header = 0;
            if (object_fuzz_elf_find_section(seed, 4, &relocation_header, 0, 0))
            {
                object_fuzz_write_u64(bytes, seed.length, relocation_header + 56, UINT64_MAX);
            }
        }
        else if (mutation_index == 19)
        {
            u64 relocation_header = 0;
            if (object_fuzz_elf_find_section(seed, 4, &relocation_header, 0, 0))
            {
                object_fuzz_write_u32(bytes, seed.length, relocation_header + 40, UINT32_MAX);
            }
        }
        else if (mutation_index == 20)
        {
            u64 section_header = 0;
            if (object_fuzz_elf_find_section(seed, 1, &section_header, 0, 0))
            {
                object_fuzz_write_u32(bytes, seed.length, section_header, UINT32_MAX);
            }
        }
        else if (mutation_index == 21)
        {
            u64 section_header = 0;
            if (object_fuzz_elf_find_section(seed, 1, &section_header, 0, 0))
            {
                object_fuzz_write_u64(bytes, seed.length, section_header + 48, UINT64_MAX);
            }
        }
        else if (mutation_index == 22)
        {
            u64 section_header = 0;
            if (object_fuzz_elf_find_section(seed, 1, &section_header, 0, 0))
            {
                object_fuzz_write_u64(bytes, seed.length, section_header + 8, UINT64_MAX);
            }
        }
        else
        {
            object_fuzz_write_u16(bytes, seed.length, 62, (u16)input_value);
        }
        break;
    case OBJECT_FORMAT_COFF:
        if (mutation_index == 4)
        {
            object_fuzz_write_u32(bytes, seed.length, 8, UINT32_MAX);
        }
        else if (mutation_index == 5)
        {
            object_fuzz_write_u16(bytes, seed.length, 2, UINT16_MAX);
        }
        else if (mutation_index == 6)
        {
            object_fuzz_write_u32(bytes, seed.length, 12, UINT32_MAX);
        }
        else if (mutation_index == 8)
        {
            u32 symbol_offset = 0;
            u32 symbol_count = 0;
            if (object_read_u32(seed, 8, &symbol_offset) && object_read_u32(seed, 12, &symbol_count) && symbol_count > 1)
            {
                object_fuzz_write_u32(bytes, seed.length, (u64)symbol_offset + 18 + 4, UINT32_MAX);
            }
        }
        else if (mutation_index == 9)
        {
            object_fuzz_write_u8(bytes, seed.length, 20, '/');
            object_fuzz_write_u8(bytes, seed.length, 21, '9');
            object_fuzz_write_u8(bytes, seed.length, 22, '9');
            object_fuzz_write_u8(bytes, seed.length, 23, '9');
            object_fuzz_write_u8(bytes, seed.length, 24, '9');
            object_fuzz_write_u8(bytes, seed.length, 25, '9');
            object_fuzz_write_u8(bytes, seed.length, 26, '9');
        }
        else if (mutation_index == 10)
        {
            u32 symbol_offset = 0;
            u32 symbol_count = 0;
            if (object_read_u32(seed, 8, &symbol_offset) && object_read_u32(seed, 12, &symbol_count) &&
                (u64)symbol_offset + (u64)symbol_count * 18 < seed.length)
            {
                object_fuzz_write_u8(bytes, seed.length, (u64)symbol_offset + (u64)symbol_count * 18 + 3, 'x');
            }
        }
        else if (mutation_index == 11)
        {
            u32 relocation_offset = 0;
            u16 relocation_count = 0;
            if (object_read_u32(seed, 20 + 24, &relocation_offset) && object_read_u16(seed, 20 + 32, &relocation_count) && relocation_count)
            {
                object_fuzz_write_u32(bytes, seed.length, (u64)relocation_offset + 4, UINT32_MAX);
            }
        }
        else if (mutation_index == 12)
        {
            object_fuzz_write_u32(bytes, seed.length, 20 + 20, UINT32_MAX);
        }
        else if (mutation_index == 13)
        {
            object_fuzz_write_u32(bytes, seed.length, 20 + 24, UINT32_MAX);
        }
        else if (mutation_index == 14)
        {
            object_fuzz_write_u32(bytes, seed.length, 20 + 16, UINT32_MAX);
        }
        else if (mutation_index == 15)
        {
            object_fuzz_write_u32(bytes, seed.length, 20 + 36, 0x02000800);
        }
        else if (mutation_index == 16)
        {
            u32 symbol_offset = 0;
            if (object_read_u32(seed, 8, &symbol_offset))
            {
                object_fuzz_write_u16(bytes, seed.length, (u64)symbol_offset + 18 + 12, UINT16_MAX);
            }
        }
        else if (mutation_index == 17)
        {
            u32 symbol_offset = 0;
            if (object_read_u32(seed, 8, &symbol_offset))
            {
                object_fuzz_write_u32(bytes, seed.length, (u64)symbol_offset + 18 + 8, UINT32_MAX);
            }
        }
        else if (mutation_index == 18)
        {
            u32 relocation_offset = 0;
            u16 relocation_count = 0;
            if (object_read_u32(seed, 20 + 24, &relocation_offset) && object_read_u16(seed, 20 + 32, &relocation_count) && relocation_count)
            {
                object_fuzz_write_u16(bytes, seed.length, (u64)relocation_offset + 8, UINT16_MAX);
            }
        }
        else if (mutation_index == 19)
        {
            u32 symbol_offset = 0;
            u32 symbol_count = 0;
            if (object_read_u32(seed, 8, &symbol_offset) && object_read_u32(seed, 12, &symbol_count) &&
                (u64)symbol_offset + (u64)symbol_count * 18 + 4 <= seed.length)
            {
                object_fuzz_write_u32(bytes, seed.length, (u64)symbol_offset + (u64)symbol_count * 18, UINT32_MAX);
            }
        }
        else if (mutation_index == 20)
        {
            object_fuzz_write_u8(bytes, seed.length, 20, (u8)input_value);
        }
        else if (mutation_index == 21)
        {
            u32 symbol_offset = 0;
            if (object_read_u32(seed, 8, &symbol_offset))
            {
                object_fuzz_write_u8(bytes, seed.length, (u64)symbol_offset + 18 + 17, UINT8_MAX);
            }
        }
        else if (mutation_index == 22)
        {
            object_fuzz_write_u16(bytes, seed.length, 2, UINT16_MAX);
        }
        else
        {
            object_fuzz_write_u32(bytes, seed.length, 20, (u32)input_value);
        }
        break;
    case OBJECT_FORMAT_MACH_O64:
        if (mutation_index == 4)
        {
            object_fuzz_write_u32(bytes, seed.length, 20, UINT32_MAX);
        }
        else if (mutation_index == 5)
        {
            object_fuzz_write_u32(bytes, seed.length, 16, UINT32_MAX);
        }
        else if (mutation_index == 6)
        {
            object_fuzz_write_u32(bytes, seed.length, 32 + 8, UINT32_MAX);
        }
        else if (mutation_index == 8)
        {
            u64 command = 0;
            if (object_fuzz_mach_find_command(seed, 2, &command))
            {
                object_fuzz_write_u32(bytes, seed.length, command + 8, UINT32_MAX);
            }
        }
        else if (mutation_index == 9)
        {
            u64 command = 0;
            if (object_fuzz_mach_find_command(seed, 2, &command))
            {
                object_fuzz_write_u32(bytes, seed.length, command + 16, UINT32_MAX);
            }
        }
        else if (mutation_index == 10)
        {
            u64 command = 0;
            u32 string_offset = 0;
            u32 string_size = 0;
            if (object_fuzz_mach_find_command(seed, 2, &command) && object_read_u32(seed, command + 16, &string_offset) &&
                object_read_u32(seed, command + 20, &string_size) && string_size && (u64)string_offset + string_size <= seed.length)
            {
                object_fuzz_write_u8(bytes, seed.length, (u64)string_offset + string_size - 1, 'x');
            }
        }
        else if (mutation_index == 11)
        {
            u64 command = 0;
            u32 symbol_offset = 0;
            u32 symbol_count = 0;
            if (object_fuzz_mach_find_command(seed, 2, &command) && object_read_u32(seed, command + 8, &symbol_offset) &&
                object_read_u32(seed, command + 12, &symbol_count) && symbol_count)
            {
                object_fuzz_write_u32(bytes, seed.length, symbol_offset, UINT32_MAX);
            }
        }
        else if (mutation_index == 12)
        {
            u64 section = 32 + 72;
            u32 relocation_offset = 0;
            u32 relocation_count = 0;
            if (object_read_u32(seed, section + 56, &relocation_offset) && object_read_u32(seed, section + 60, &relocation_count) && relocation_count)
            {
                object_fuzz_write_u32(bytes, seed.length, (u64)relocation_offset + 4, UINT32_MAX);
            }
        }
        else if (mutation_index == 13)
        {
            object_fuzz_write_u32(bytes, seed.length, 32 + 72 + 48, UINT32_MAX);
        }
        else if (mutation_index == 14)
        {
            object_fuzz_write_u32(bytes, seed.length, 32 + 72 + 56, UINT32_MAX);
        }
        else if (mutation_index == 15)
        {
            object_fuzz_write_u32(bytes, seed.length, 32 + 72 + 60, UINT32_MAX);
        }
        else if (mutation_index == 16)
        {
            u64 command = 0;
            if (object_fuzz_mach_find_command(seed, 2, &command))
            {
                object_fuzz_write_u32(bytes, seed.length, command + 12, UINT32_MAX);
            }
        }
        else if (mutation_index == 17)
        {
            u64 command = 0;
            if (object_fuzz_mach_find_command(seed, 2, &command))
            {
                object_fuzz_write_u32(bytes, seed.length, command + 20, UINT32_MAX);
            }
        }
        else if (mutation_index == 18)
        {
            object_fuzz_write_u32(bytes, seed.length, 32 + 4, UINT32_MAX);
        }
        else if (mutation_index == 19)
        {
            object_fuzz_write_u32(bytes, seed.length, 16, UINT32_MAX);
        }
        else if (mutation_index == 20)
        {
            object_fuzz_write_u32(bytes, seed.length, 32 + 72 + 64, UINT32_MAX);
        }
        else if (mutation_index == 21)
        {
            object_fuzz_write_u8(bytes, seed.length, 32 + 72, (u8)input_value);
        }
        else if (mutation_index == 22)
        {
            u64 command = 0;
            if (object_fuzz_mach_find_command(seed, 2, &command))
            {
                object_fuzz_write_u32(bytes, seed.length, command + 4, UINT32_MAX);
            }
        }
        else
        {
            object_fuzz_write_u32(bytes, seed.length, 32 + 12, (u32)input_value);
        }
        break;
    case OBJECT_FORMAT_COUNT:
        break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void object_fuzz_read_candidate(Arena* arena, ByteSlice bytes, Target target, bool archive)
{
    if (archive)
    {
        ObjectArchive result = object_archive_read(arena, bytes, target);
        BUSTER_UNUSED(result);
    }
    else
    {
        ObjectFile result = object_read(arena, bytes, target);
        BUSTER_UNUSED(result);
    }
}

BUSTER_GLOBAL_LOCAL ObjectArtifact object_fuzz_seed_write(Arena* arena, Target target, ObjectFormat format)
{
    u8 x86_text[] = {0xe8, 0, 0, 0, 0, 0xc3};
    u8 aarch64_text[] = {0x00, 0x00, 0x00, 0x94, 0xc0, 0x03, 0x5f, 0xd6};
    u8 read_only_data[] = {'f', 'u', 'z', 'z', 0};
    u8 debug_data[] = {0};
    ByteSlice text = target.cpu_arch == CPU_ARCH_AARCH64 ? (ByteSlice)BUSTER_ARRAY_TO_SLICE(aarch64_text) : (ByteSlice)BUSTER_ARRAY_TO_SLICE(x86_text);
    ObjectSection sections[OBJECT_SECTION_COUNT] = {0};
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .kind = (ObjectSectionKind)kind,
            .alignment = object_section_default_alignment((ObjectSectionKind)kind),
        };
    }
    sections[OBJECT_SECTION_TEXT].data = text;
    sections[OBJECT_SECTION_TEXT].virtual_size = text.length;
    sections[OBJECT_SECTION_READ_ONLY_DATA].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(read_only_data);
    sections[OBJECT_SECTION_READ_ONLY_DATA].virtual_size = sizeof(read_only_data);
    sections[OBJECT_SECTION_DEBUG_INFO].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(debug_data);
    sections[OBJECT_SECTION_DEBUG_INFO].virtual_size = sizeof(debug_data);
    ObjectSymbol symbols[] = {
        {
            .name = S8("fuzz_seed"),
            .size = text.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("fuzz_data"),
            .size = sizeof(read_only_data),
            .section = OBJECT_SECTION_READ_ONLY_DATA,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
        {
            .name = S8("fuzz_external"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation relocation = {
        .addend = target.cpu_arch == CPU_ARCH_X86_64 ? -4 : 0,
        .offset = target.cpu_arch == CPU_ARCH_X86_64 ? 1 : 0,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 2,
        .kind = target.cpu_arch == CPU_ARCH_X86_64 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_CALL26,
    };
    ObjectFile object = {
        .sections = sections,
        .symbols = symbols,
        .relocations = &relocation,
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
        .symbol_count = BUSTER_ARRAY_LENGTH(symbols),
        .relocation_count = 1,
    };
    return object_write(arena, &object, format);
}

s32 object_fuzz_test_input(const u8* pointer, size_t size)
{
    if (size > BUSTER_KB(64) || (size && !pointer))
    {
        return -1;
    }
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    if (!arena)
    {
        return 0;
    }
    ByteSlice input = {.pointer = (u8*)pointer, .length = size};
    Target targets[] = {
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS},
    };
    ObjectFormat formats[] = {
        OBJECT_FORMAT_ELF64,
        OBJECT_FORMAT_COFF,
        OBJECT_FORMAT_MACH_O64,
        OBJECT_FORMAT_ELF64,
        OBJECT_FORMAT_COFF,
        OBJECT_FORMAT_MACH_O64,
    };
    bool archive_input = size >= 8 && pointer && memcmp(pointer, "!<arch>\n", 8) == 0;
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        TemporalArena raw_scope = arena_begin_temporal(arena);
        object_fuzz_read_candidate(arena, input, targets[target_index], archive_input);
        arena_set_position(arena, raw_scope.position);
    }
    for (u32 seed_index = 0; seed_index < BUSTER_ARRAY_LENGTH(targets); seed_index += 1)
    {
        TemporalArena seed_scope = arena_begin_temporal(arena);
        ObjectArtifact seed = object_fuzz_seed_write(arena, targets[seed_index], formats[seed_index]);
        if (seed.error == OBJECT_ERROR_NONE && seed.bytes.pointer && seed.bytes.length <= OBJECT_FUZZ_MAX_GENERATED_BYTES)
        {
            TemporalArena parse_scope = arena_begin_temporal(arena);
            object_fuzz_read_candidate(arena, seed.bytes, targets[seed_index], false);
            arena_set_position(arena, parse_scope.position);
            for (u32 mutation_index = 0; mutation_index < OBJECT_FUZZ_MUTATION_COUNT; mutation_index += 1)
            {
                TemporalArena mutation_scope = arena_begin_temporal(arena);
                ByteSlice mutated = object_fuzz_mutate(arena, seed.bytes, pointer, size, formats[seed_index], false, mutation_index);
                object_fuzz_read_candidate(arena, mutated, targets[seed_index], false);
                arena_set_position(arena, mutation_scope.position);
            }
            for (u32 archive_kind = 0; archive_kind < 3; archive_kind += 1)
            {
                TemporalArena archive_scope = arena_begin_temporal(arena);
                ByteSlice archive = object_fuzz_archive_make(arena, seed.bytes, archive_kind);
                if (archive.pointer)
                {
                    object_fuzz_read_candidate(arena, archive, targets[seed_index], true);
                    for (u32 mutation_index = 0; mutation_index < OBJECT_FUZZ_MUTATION_COUNT; mutation_index += 1)
                    {
                        TemporalArena mutation_scope = arena_begin_temporal(arena);
                        ByteSlice mutated = object_fuzz_mutate(arena, archive, pointer, size, formats[seed_index], true, mutation_index);
                        object_fuzz_read_candidate(arena, mutated, targets[seed_index], true);
                        arena_set_position(arena, mutation_scope.position);
                    }
                }
                arena_set_position(arena, archive_scope.position);
            }
        }
        arena_set_position(arena, seed_scope.position);
    }
    arena_destroy(arena, 1);
    return 0;
}
#endif

AnalysisEntity* object_entity_find(AnalysisResult* analysis, AnalysisEntityId entity)
{
    if (entity.module.value == analysis->module.id.value)
    {
        if (!analysis->module.entities || entity.index.value >= analysis->module.entity_count)
        {
            return 0;
        }
        return analysis->module.entities + entity.index.value;
    }
    if (!analysis->module.imports)
    {
        return 0;
    }
    for (u32 import_index = 0; import_index < analysis->module.import_count; import_index += 1)
    {
        AnalysisResult* imported = analysis->module.imports[import_index].target;
        if (imported && imported->module.id.value == entity.module.value)
        {
            if (!imported->module.entities || entity.index.value >= imported->module.entity_count)
            {
                return 0;
            }
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
           kind == OBJECT_SECTION_DEBUG_STR || kind == OBJECT_SECTION_DEBUG_LOC || kind == OBJECT_SECTION_DEBUG_RANGES ||
           kind == OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS || kind == OBJECT_SECTION_DEBUG_CODEVIEW_TYPES;
}

BUSTER_GLOBAL_LOCAL void object_metadata_sections_initialize(ObjectFile* object)
{
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        if (kind != OBJECT_SECTION_UNWIND && kind != OBJECT_SECTION_WINDOWS_PDATA && kind != OBJECT_SECTION_WINDOWS_XDATA &&
            !object_section_kind_is_debug((ObjectSectionKind)kind))
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

// Symbol references resolve by the first defined then the first undefined
// name match in symbol index order. The probe table records the lowest-index
// match of each class per name, so a lookup returns exactly what the linear
// scan over the symbol table would, without rescanning it per relocation.
typedef struct ObjectSymbolNameSlot ObjectSymbolNameSlot;
struct ObjectSymbolNameSlot
{
    String8 name;
    u32 defined;
    u32 undefined;
    bool used;
};

typedef struct ObjectSymbolNameIndex ObjectSymbolNameIndex;
struct ObjectSymbolNameIndex
{
    ObjectSymbolNameSlot* slots;
    u32 mask;
};

enum
{
    OBJECT_LOOKUP_INDEX_MIN_QUERY_COUNT = 8,
};

BUSTER_GLOBAL_LOCAL u64 object_symbol_name_hash(String8 name)
{
    u64 hash = 1469598103934665603ull;
    for (u64 index = 0; index < name.length; index += 1)
    {
        hash ^= name.pointer[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

BUSTER_GLOBAL_LOCAL ObjectSymbolNameSlot* object_symbol_name_slot(ObjectSymbolNameIndex table, String8 name)
{
    u32 mask = table.mask;
    u32 slot_index = (u32)(object_symbol_name_hash(name) & mask);
    ObjectSymbolNameSlot* slot = table.slots + slot_index;
    while (slot->used && !string_equal(slot->name, name))
    {
        slot_index = (slot_index + 1) & mask;
        slot = table.slots + slot_index;
    }
    return slot;
}

BUSTER_GLOBAL_LOCAL void object_symbol_name_index_add(ObjectSymbolNameIndex table, ObjectSymbol* symbol, u32 symbol_index)
{
    ObjectSymbolNameSlot* slot = object_symbol_name_slot(table, symbol->name);
    if (!slot->used)
    {
        slot->used = true;
        slot->name = symbol->name;
        slot->defined = UINT32_MAX;
        slot->undefined = UINT32_MAX;
    }
    if (symbol->section != OBJECT_SECTION_UNDEFINED)
    {
        if (slot->defined == UINT32_MAX)
        {
            slot->defined = symbol_index;
        }
    }
    else if (slot->undefined == UINT32_MAX)
    {
        slot->undefined = symbol_index;
    }
}

// symbol_capacity bounds every add the caller will make, including symbols it
// appends after building; the table stays under half full so probing stays
// short.
BUSTER_GLOBAL_LOCAL ObjectSymbolNameIndex object_symbol_name_index_build(Arena* arena, ObjectSymbol* symbols, u32 symbol_count, u64 symbol_capacity)
{
    u64 capacity = 16;
    while (capacity < symbol_capacity * 2 + 1)
    {
        capacity <<= 1;
    }
    ObjectSymbolNameIndex table = {
        .slots = arena_allocate(arena, ObjectSymbolNameSlot, capacity),
        .mask = (u32)(capacity - 1),
    };
    memset(table.slots, 0, sizeof(*table.slots) * capacity);
    for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
    {
        object_symbol_name_index_add(table, symbols + symbol_index, symbol_index);
    }
    return table;
}

// Legacy buster relocations identify their target with the full
// (module, entity, instantiation) tuple rather than an IrSymbolId. Keep the
// first entry for duplicate tuples, matching the former ascending scan.
typedef struct ObjectCodegenEntryIndex ObjectCodegenEntryIndex;
struct ObjectCodegenEntryIndex
{
    CodegenModuleEntry* entries;
    u32* slots;
    u64 mask;
};

BUSTER_GLOBAL_LOCAL u64 object_codegen_entry_hash(AnalysisEntityId entity, AnalysisInstantiationId instantiation)
{
    u64 entity_key = ((u64)entity.module.value << 32) | entity.index.value;
    return object_reader_hash_u64(entity_key ^ object_reader_hash_u64(instantiation.value));
}

BUSTER_GLOBAL_LOCAL u32* object_codegen_entry_slot(ObjectCodegenEntryIndex table, AnalysisEntityId entity, AnalysisInstantiationId instantiation)
{
    u64 slot_index = object_codegen_entry_hash(entity, instantiation) & table.mask;
    for (u64 probe = 0; probe <= table.mask; probe += 1)
    {
        u32* slot = table.slots + slot_index;
        if (*slot == UINT32_MAX)
        {
            return slot;
        }
        CodegenModuleEntry* candidate = table.entries + *slot;
        if (candidate->entity.module.value == entity.module.value && candidate->entity.index.value == entity.index.value &&
            candidate->instantiation.value == instantiation.value)
        {
            return slot;
        }
        slot_index = (slot_index + 1) & table.mask;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL ObjectCodegenEntryIndex object_codegen_entry_index_build(Arena* arena, CodegenModuleEntry* entries, u32 entry_count)
{
    u64 capacity = 16;
    while (capacity < (u64)entry_count * 2 + 1)
    {
        capacity <<= 1;
    }
    ObjectCodegenEntryIndex table = {
        .entries = entries,
        .slots = arena_allocate(arena, u32, capacity),
        .mask = capacity - 1,
    };
    memset(table.slots, 0xff, sizeof(*table.slots) * capacity);
    for (u32 entry_index = 0; entry_index < entry_count; entry_index += 1)
    {
        CodegenModuleEntry* entry = entries + entry_index;
        u32* slot = object_codegen_entry_slot(table, entry->entity, entry->instantiation);
        if (slot && *slot == UINT32_MAX)
        {
            *slot = entry_index;
        }
    }
    return table;
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
    TemporalArena name_temporary = {0};
    ObjectSymbolNameIndex name_index = {0};
    u32 named_relocation_count = 0;
    bool name_index_initialized = false;
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        CodeviewRelocation relocation = built.relocations[relocation_index];
        u32 symbol_index = relocation.function;
        if (relocation.symbol_name.length)
        {
            named_relocation_count += 1;
            if (!name_index_initialized && named_relocation_count == OBJECT_LOOKUP_INDEX_MIN_QUERY_COUNT)
            {
                name_temporary = scratch_begin(0, 0);
                name_index = object_symbol_name_index_build(name_temporary.arena, object->symbols, object->symbol_count, object->symbol_count);
                name_index_initialized = true;
            }
            if (name_index_initialized)
            {
                ObjectSymbolNameSlot* slot = object_symbol_name_slot(name_index, relocation.symbol_name);
                symbol_index = slot->used ? slot->defined : UINT32_MAX;
            }
            else
            {
                symbol_index = UINT32_MAX;
                for (u32 candidate_index = 0; candidate_index < object->symbol_count; candidate_index += 1)
                {
                    ObjectSymbol* candidate = object->symbols + candidate_index;
                    if (candidate->section != OBJECT_SECTION_UNDEFINED && string_equal(candidate->name, relocation.symbol_name))
                    {
                        symbol_index = candidate_index;
                        break;
                    }
                }
            }
        }
        if (symbol_index == UINT32_MAX || symbol_index >= object->symbol_count)
        {
            continue;
        }
        object->relocations[object->relocation_count++] = (ObjectRelocation){
            .offset = relocation.offset,
            .section = OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
            .symbol = symbol_index,
            .kind = relocation.kind == CODEVIEW_RELOCATION_SECREL32 ? OBJECT_RELOCATION_COFF_SECREL32 : OBJECT_RELOCATION_COFF_SECTION16,
        };
    }
    if (name_index_initialized)
    {
        scratch_end(name_temporary);
    }
}

BUSTER_GLOBAL_LOCAL void object_debug_module_set(Arena* arena, ObjectFile* object, String8 name, u64 code_size)
{
    if (!arena || !object || !object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.length)
    {
        return;
    }
    object->debug_modules = arena_allocate(arena, ObjectDebugModule, 1);
    object->debug_module_count = 1;
    object->debug_modules[0] = (ObjectDebugModule){
        .name = string_duplicate_arena(arena, name.length ? name : S8("buster.obj"), false),
        .code_size = code_size,
        .symbols_size = object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.length,
        .types_size = object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data.length,
    };
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
    case DWARF_SECTION_LOC:
        return OBJECT_SECTION_DEBUG_LOC;
    case DWARF_SECTION_RANGES:
        return OBJECT_SECTION_DEBUG_RANGES;
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
    TemporalArena name_temporary = scratch_begin(0, 0);
    ObjectSymbolNameIndex name_index = object_symbol_name_index_build(name_temporary.arena, object->symbols, object->symbol_count, object->symbol_count);
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        DwarfRelocation relocation = built.relocations[relocation_index];
        u32 relocation_symbol = relocation.address ? text_symbol : debug_symbols[relocation.target];
        if (relocation.symbol_address && relocation.symbol_name.length)
        {
            ObjectSymbolNameSlot* slot = object_symbol_name_slot(name_index, relocation.symbol_name);
            if (slot->used && slot->defined != UINT32_MAX)
            {
                relocation_symbol = slot->defined;
            }
        }
        object->relocations[object->relocation_count++] = (ObjectRelocation){
            .addend = relocation.addend,
            .offset = relocation.offset,
            .section = (u32)object_section_kind_for_dwarf(relocation.section),
            .symbol = relocation_symbol,
            .kind = relocation.address ? OBJECT_RELOCATION_ABSOLUTE64 : OBJECT_RELOCATION_ABSOLUTE32,
        };
    }
    scratch_end(name_temporary);
}

BUSTER_GLOBAL_LOCAL bool object_append_dwarf_cfi(ObjectFile* object, DwarfCfiResult built)
{
    if (!built.valid)
    {
        return false;
    }
    object->sections[OBJECT_SECTION_UNWIND].data = built.bytes;
    for (u32 relocation_index = 0; relocation_index < built.relocation_count; relocation_index += 1)
    {
        DwarfCfiRelocation relocation = built.relocations[relocation_index];
        if (relocation.function >= object->symbol_count)
        {
            return false;
        }
        object->relocations[object->relocation_count++] = (ObjectRelocation){
            .offset = relocation.offset,
            .section = OBJECT_SECTION_UNWIND,
            .symbol = relocation.function,
            .kind = object->target.cpu_arch == CPU_ARCH_X86_64 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_PREL32,
        };
    }
    return true;
}

typedef struct ObjectWindowsUnwindResult ObjectWindowsUnwindResult;
struct ObjectWindowsUnwindResult
{
    ByteSlice pdata;
    ByteSlice xdata;
    u32* xdata_offsets;
    u32 function_count;
    bool valid;
    bool aarch64;
    u8 reserved[2];
};

BUSTER_GLOBAL_LOCAL ObjectWindowsUnwindResult object_windows_x64_unwind_build(Arena* arena, CodegenFunctionDescriptor* functions, u32 function_count)
{
    ObjectWindowsUnwindResult result = {
        .function_count = function_count,
    };
    if (!arena || (function_count && !functions) || function_count > UINT32_MAX / 12)
    {
        return result;
    }
    result.xdata_offsets = arena_allocate(arena, u32, function_count);
    u64 xdata_size = 0;
    for (u32 function_index = 0; function_index < function_count; function_index += 1)
    {
        CodegenFunctionDescriptor* function = functions + function_index;
        if (function->prolog_size > UINT8_MAX)
        {
            return result;
        }
        u32 frame_action = UINT32_MAX;
        u32 frame_offset = 0;
        u32 unwind_slot_count = 0;
        for (u32 action_index = 0; action_index < function->unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = function->unwind_actions + action_index;
            if (action->code_offset > UINT8_MAX || action->register_index > 15)
            {
                return result;
            }
            if (action->kind == CODEGEN_UNWIND_ACTION_PUSH_REGISTER)
            {
                unwind_slot_count += 1;
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER)
            {
                if (frame_action != UINT32_MAX)
                {
                    return result;
                }
                frame_action = action_index;
                frame_offset = action->value;
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
            {
                if (action->value % 8)
                {
                    return result;
                }
                unwind_slot_count += action->value <= 128 ? 1 : action->value <= 524280 ? 2 : 3;
                if (frame_action != UINT32_MAX)
                {
                    if (action->value > UINT32_MAX - frame_offset)
                    {
                        return result;
                    }
                    frame_offset += action->value;
                }
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER)
            {
                if (action->value % 8)
                {
                    return result;
                }
                unwind_slot_count += action->value <= 524280 ? 2 : 3;
            }
            else
            {
                return result;
            }
            if (unwind_slot_count > UINT8_MAX)
            {
                return result;
            }
        }
        bool encode_frame = frame_action != UINT32_MAX && frame_offset <= 240 && frame_offset % 16 == 0;
        unwind_slot_count += encode_frame;
        if (unwind_slot_count > UINT8_MAX)
        {
            return result;
        }
        result.xdata_offsets[function_index] = (u32)xdata_size;
        u64 record_size = align_forward(4 + (u64)unwind_slot_count * 2, 4);
        if (xdata_size > UINT32_MAX || record_size > UINT32_MAX - xdata_size)
        {
            return result;
        }
        xdata_size += record_size;
    }
    result.pdata = (ByteSlice){
        .pointer = arena_allocate(arena, u8, (u64)function_count * 12),
        .length = (u64)function_count * 12,
    };
    result.xdata = (ByteSlice){
        .pointer = arena_allocate(arena, u8, xdata_size),
        .length = xdata_size,
    };
    if (result.pdata.length)
    {
        memset(result.pdata.pointer, 0, result.pdata.length);
    }
    if (result.xdata.length)
    {
        memset(result.xdata.pointer, 0, result.xdata.length);
    }
    for (u32 function_index = 0; function_index < function_count; function_index += 1)
    {
        CodegenFunctionDescriptor* function = functions + function_index;
        u32 frame_action = UINT32_MAX;
        u32 frame_offset = 0;
        u32 unwind_slot_count = 0;
        for (u32 action_index = 0; action_index < function->unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = function->unwind_actions + action_index;
            if (action->kind == CODEGEN_UNWIND_ACTION_PUSH_REGISTER)
            {
                unwind_slot_count += 1;
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER)
            {
                frame_action = action_index;
                frame_offset = action->value;
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
            {
                unwind_slot_count += action->value <= 128 ? 1 : action->value <= 524280 ? 2 : 3;
                if (frame_action != UINT32_MAX)
                {
                    frame_offset += action->value;
                }
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER)
            {
                unwind_slot_count += action->value <= 524280 ? 2 : 3;
            }
        }
        bool encode_frame = frame_action != UINT32_MAX && frame_offset <= 240 && frame_offset % 16 == 0;
        unwind_slot_count += encode_frame;
        u8* record = result.xdata.pointer + result.xdata_offsets[function_index];
        record[0] = 1;
        record[1] = (u8)function->prolog_size;
        record[2] = (u8)unwind_slot_count;
        if (encode_frame)
        {
            record[3] = (u8)(functions[function_index].unwind_actions[frame_action].register_index | ((frame_offset / 16) << 4));
        }
        u32 cursor = 4;
        for (u32 action_index = function->unwind_action_count; action_index > 0; action_index -= 1)
        {
            CodegenUnwindAction* action = function->unwind_actions + action_index - 1;
            if (action->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER && !encode_frame)
            {
                continue;
            }
            record[cursor] = (u8)action->code_offset;
            if (action->kind == CODEGEN_UNWIND_ACTION_PUSH_REGISTER)
            {
                record[cursor + 1] = (u8)(action->register_index << 4);
                cursor += 2;
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER)
            {
                record[cursor + 1] = 3;
                cursor += 2;
            }
            else if (action->kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER)
            {
                if (action->value <= 524280)
                {
                    u16 scaled_offset = (u16)(action->value / 8);
                    record[cursor + 1] = (u8)((action->register_index << 4) | 4);
                    memcpy(record + cursor + 2, &scaled_offset, sizeof(scaled_offset));
                    cursor += 4;
                }
                else
                {
                    if (action->value > UINT32_MAX)
                    {
                        return result;
                    }
                    record[cursor + 1] = (u8)((action->register_index << 4) | 5);
                    u32 far_offset = (u32)action->value;
                    memcpy(record + cursor + 2, &far_offset, sizeof(far_offset));
                    cursor += 6;
                }
            }
            else if (action->value <= 128)
            {
                record[cursor + 1] = (u8)(((action->value / 8 - 1) << 4) | 2);
                cursor += 2;
            }
            else if (action->value <= 524280)
            {
                u16 scaled_size = (u16)(action->value / 8);
                record[cursor + 1] = 1;
                memcpy(record + cursor + 2, &scaled_size, sizeof(scaled_size));
                cursor += 4;
            }
            else
            {
                record[cursor + 1] = 0x11;
                memcpy(record + cursor + 2, &action->value, sizeof(action->value));
                cursor += 6;
            }
        }
        if (cursor != 4 + unwind_slot_count * 2)
        {
            return (ObjectWindowsUnwindResult){0};
        }
    }
    result.valid = true;
    return result;
}

typedef struct ObjectWindowsArm64CodeBuffer ObjectWindowsArm64CodeBuffer;
struct ObjectWindowsArm64CodeBuffer
{
    u8* bytes;
    u32 count;
    u32 capacity;
    bool valid;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL void object_windows_arm64_code_byte(ObjectWindowsArm64CodeBuffer* buffer, u8 byte)
{
    if (!buffer->valid || buffer->count >= buffer->capacity)
    {
        buffer->valid = false;
        return;
    }
    buffer->bytes[buffer->count++] = byte;
}

BUSTER_GLOBAL_LOCAL bool object_windows_arm64_action_encode(ObjectWindowsArm64CodeBuffer* buffer, CodegenUnwindAction const* action)
{
    if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
    {
        if (!action->value || action->value % 16)
        {
            return false;
        }
        u32 units = action->value / 16;
        if (units <= 31)
        {
            object_windows_arm64_code_byte(buffer, (u8)units);
        }
        else if (units <= 2047)
        {
            object_windows_arm64_code_byte(buffer, (u8)(0xc0 | (units >> 8)));
            object_windows_arm64_code_byte(buffer, (u8)units);
        }
        else if (units <= 0xffffff)
        {
            object_windows_arm64_code_byte(buffer, 0xe0);
            object_windows_arm64_code_byte(buffer, (u8)(units >> 16));
            object_windows_arm64_code_byte(buffer, (u8)(units >> 8));
            object_windows_arm64_code_byte(buffer, (u8)units);
        }
        else
        {
            return false;
        }
    }
    else if (action->kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER)
    {
        if (action->register_index < 19 || action->register_index > 28 || action->value > 504 || action->value % 8)
        {
            return false;
        }
        u32 register_offset = action->register_index - 19;
        object_windows_arm64_code_byte(buffer, (u8)(0xd0 | (register_offset >> 2)));
        object_windows_arm64_code_byte(buffer, (u8)(((register_offset & 3) << 6) | (action->value / 8)));
    }
    else if (action->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER)
    {
        if (action->register_index != 29 || action->value > 2040 || action->value % 8)
        {
            return false;
        }
        if (!action->value)
        {
            object_windows_arm64_code_byte(buffer, 0xe1);
        }
        else
        {
            object_windows_arm64_code_byte(buffer, 0xe2);
            object_windows_arm64_code_byte(buffer, (u8)(action->value / 8));
        }
    }
    else if (action->kind == CODEGEN_UNWIND_ACTION_NOP)
    {
        object_windows_arm64_code_byte(buffer, 0xe3);
    }
    else
    {
        return false;
    }
    return buffer->valid;
}

BUSTER_GLOBAL_LOCAL bool object_windows_arm64_initial_frame_valid(CodegenFunctionDescriptor const* function)
{
    if (function->unwind_action_count < 4)
    {
        return false;
    }
    CodegenUnwindAction const* actions = function->unwind_actions;
    return actions[0].kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK && actions[0].value == 16 && actions[0].code_offset == 4 &&
           actions[1].kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER && actions[1].register_index == 29 && actions[1].value == 0 &&
           actions[1].code_offset == 4 && actions[2].kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER && actions[2].register_index == 30 &&
           actions[2].value == 8 && actions[2].code_offset == 4 && actions[3].kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER &&
           actions[3].register_index == 29 && actions[3].value == 0 && actions[3].code_offset == 8;
}

BUSTER_GLOBAL_LOCAL bool object_windows_arm64_codes_build(CodegenFunctionDescriptor const* function, ObjectWindowsArm64CodeBuffer* codes,
                                                          u32* epilog_code_index)
{
    if (!object_windows_arm64_initial_frame_valid(function))
    {
        return false;
    }
    u32 body_end = function->unwind_action_count;
    bool frame_base = body_end >= 2 && function->unwind_actions[body_end - 1].kind == CODEGEN_UNWIND_ACTION_NOP &&
                      function->unwind_actions[body_end - 2].kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER &&
                      function->unwind_actions[body_end - 2].register_index == 28;
    for (u32 action_index = function->unwind_action_count; action_index > 4; action_index -= 1)
    {
        if (!object_windows_arm64_action_encode(codes, function->unwind_actions + action_index - 1))
        {
            return false;
        }
    }
    if (!object_windows_arm64_action_encode(codes, function->unwind_actions + 3))
    {
        return false;
    }
    // The first instruction is STP x29,x30,[sp,#-16]!, which both allocates
    // the fixed frame-chain slot and saves FP/LR.
    object_windows_arm64_code_byte(codes, 0x81);
    object_windows_arm64_code_byte(codes, 0xe4);
    *epilog_code_index = codes->count;
    if (frame_base)
    {
        if (!object_windows_arm64_action_encode(codes, function->unwind_actions + body_end - 1) ||
            !object_windows_arm64_action_encode(codes, function->unwind_actions + body_end - 2))
        {
            return false;
        }
        body_end -= 2;
    }
    // Epilogs add allocation chunks in their original order and contain no
    // guard-page touches or redundant x29 frame setup.
    for (u32 action_index = 4; action_index < body_end; action_index += 1)
    {
        CodegenUnwindAction const* action = function->unwind_actions + action_index;
        if (action->kind == CODEGEN_UNWIND_ACTION_NOP && action_index + 13 < body_end)
        {
            bool large_allocation = function->unwind_actions[action_index + 13].kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK &&
                                    function->unwind_actions[action_index + 13].value > 4080;
            for (u32 nop_index = 0; nop_index < 13 && large_allocation; nop_index += 1)
            {
                large_allocation &= function->unwind_actions[action_index + nop_index].kind == CODEGEN_UNWIND_ACTION_NOP;
            }
            if (large_allocation)
            {
                for (u32 nop_index = 0; nop_index < 4; nop_index += 1)
                {
                    object_windows_arm64_code_byte(codes, 0xe3);
                }
                action_index += 12;
                continue;
            }
        }
        if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK && !object_windows_arm64_action_encode(codes, action))
        {
            return false;
        }
        if (action->kind != CODEGEN_UNWIND_ACTION_ALLOCATE_STACK && action->kind != CODEGEN_UNWIND_ACTION_NOP)
        {
            return false;
        }
    }
    object_windows_arm64_code_byte(codes, 0x81);
    object_windows_arm64_code_byte(codes, 0xe4);
    return codes->valid;
}

BUSTER_GLOBAL_LOCAL ObjectWindowsUnwindResult object_windows_arm64_unwind_build(Arena* arena, CodegenFunctionDescriptor* functions,
                                                                                 u32 function_count)
{
    ObjectWindowsUnwindResult result = {
        .function_count = function_count,
        .aarch64 = true,
    };
    if (!arena || (function_count && !functions) || function_count > UINT32_MAX / 8)
    {
        return result;
    }
    result.xdata_offsets = arena_allocate(arena, u32, function_count);
    u64 xdata_capacity = 0;
    for (u32 function_index = 0; function_index < function_count; function_index += 1)
    {
        CodegenFunctionDescriptor const* function = functions + function_index;
        if (!function->code_size || function->code_size % 4 || function->code_size / 4 >= (1u << 18) || function->epilog_count > UINT16_MAX)
        {
            return result;
        }
        u64 record_capacity = 8 + (u64)function->epilog_count * 4 + (u64)function->unwind_action_count * 8 + 8;
        if (xdata_capacity > UINT32_MAX || record_capacity > UINT32_MAX - xdata_capacity)
        {
            return result;
        }
        xdata_capacity += record_capacity;
    }
    result.pdata = (ByteSlice){
        .pointer = arena_allocate(arena, u8, (u64)function_count * 8),
        .length = (u64)function_count * 8,
    };
    result.xdata.pointer = arena_allocate(arena, u8, xdata_capacity);
    if (result.pdata.length)
    {
        memset(result.pdata.pointer, 0, result.pdata.length);
    }
    u32 xdata_cursor = 0;
    for (u32 function_index = 0; function_index < function_count; function_index += 1)
    {
        CodegenFunctionDescriptor const* function = functions + function_index;
        result.xdata_offsets[function_index] = xdata_cursor;
        u32 code_capacity = function->unwind_action_count * 8 + 8;
        ObjectWindowsArm64CodeBuffer codes = {
            .bytes = arena_allocate(arena, u8, code_capacity),
            .capacity = code_capacity,
            .valid = true,
        };
        u32 epilog_code_index = 0;
        if (!object_windows_arm64_codes_build(function, &codes, &epilog_code_index) || epilog_code_index > 1023)
        {
            return result;
        }
        u32 code_words = (codes.count + 3) / 4;
        bool extended = function->epilog_count > 31 || code_words > 31;
        if (code_words > UINT8_MAX)
        {
            return result;
        }
        u32 header_size = extended ? 8 : 4;
        u64 record_size = (u64)header_size + (u64)function->epilog_count * 4 + (u64)code_words * 4;
        if (record_size > xdata_capacity - xdata_cursor)
        {
            return result;
        }
        u8* record = result.xdata.pointer + xdata_cursor;
        memset(record, 0, record_size);
        u32 header = function->code_size / 4;
        if (!extended)
        {
            header |= function->epilog_count << 22;
            header |= code_words << 27;
        }
        memcpy(record, &header, sizeof(header));
        if (extended)
        {
            u32 extension = function->epilog_count | (code_words << 16);
            memcpy(record + 4, &extension, sizeof(extension));
        }
        for (u32 epilog_index = 0; epilog_index < function->epilog_count; epilog_index += 1)
        {
            u32 epilog = function->epilog_offsets[epilog_index];
            if (epilog % 4 || epilog / 4 >= (1u << 18))
            {
                return result;
            }
            u32 scope = epilog / 4 | (epilog_code_index << 22);
            memcpy(record + header_size + epilog_index * 4, &scope, sizeof(scope));
        }
        memcpy(record + header_size + function->epilog_count * 4, codes.bytes, codes.count);
        xdata_cursor += (u32)record_size;
    }
    result.xdata.length = xdata_cursor;
    result.valid = true;
    return result;
}

BUSTER_GLOBAL_LOCAL bool object_append_windows_unwind(Arena* arena, ObjectFile* object, ObjectWindowsUnwindResult built)
{
    if (!built.valid || built.function_count > object->symbol_count)
    {
        return false;
    }
    object->sections[OBJECT_SECTION_WINDOWS_PDATA].data = built.pdata;
    object->sections[OBJECT_SECTION_WINDOWS_PDATA].virtual_size = built.pdata.length;
    object->sections[OBJECT_SECTION_WINDOWS_XDATA].data = built.xdata;
    object->sections[OBJECT_SECTION_WINDOWS_XDATA].virtual_size = built.xdata.length;
    if (!built.function_count)
    {
        return true;
    }
    u32 xdata_symbol = object->symbol_count++;
    object->symbols[xdata_symbol] = (ObjectSymbol){
        .name = string_format(arena, S8(".Lxdata.{u32}"), xdata_symbol),
        .size = built.xdata.length,
        .section = OBJECT_SECTION_WINDOWS_XDATA,
        .kind = OBJECT_SYMBOL_DATA,
    };
    for (u32 function_index = 0; function_index < built.function_count; function_index += 1)
    {
        u64 offset = (u64)function_index * (built.aarch64 ? 8 : 12);
        object->relocations[object->relocation_count++] = (ObjectRelocation){
            .offset = offset,
            .section = OBJECT_SECTION_WINDOWS_PDATA,
            .symbol = function_index,
            .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
        };
        if (!built.aarch64)
        {
            object->relocations[object->relocation_count++] = (ObjectRelocation){
                .addend = (s64)object->symbols[function_index].size,
                .offset = offset + 4,
                .section = OBJECT_SECTION_WINDOWS_PDATA,
                .symbol = function_index,
                .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
            };
        }
        object->relocations[object->relocation_count++] = (ObjectRelocation){
            .addend = (s64)built.xdata_offsets[function_index],
            .offset = offset + (built.aarch64 ? 4 : 8),
            .section = OBJECT_SECTION_WINDOWS_PDATA,
            .symbol = xdata_symbol,
            .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
        };
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_codegen_functions_valid(CodegenModule* module)
{
    if (module->function_count != module->entry_count || (module->function_count && (!module->functions || !module->entries)))
    {
        return false;
    }
    u32 previous_end = 0;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        CodegenModuleEntry* entry = module->entries + function_index;
        CodegenFunctionDescriptor* function = module->functions + function_index;
        if (function->symbol.value != entry->symbol.value || function->code_offset != entry->offset || function->code_offset < previous_end ||
            function->code_offset > module->code.length ||
            function->code_size > module->code.length - function->code_offset || function->prolog_size > function->code_size ||
            (function->unwind_action_count && !function->unwind_actions) || (function->epilog_count && !function->epilog_offsets))
        {
            return false;
        }
        previous_end = function->code_offset + function->code_size;
        u32 previous_offset = 0;
        for (u32 action_index = 0; action_index < function->unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = function->unwind_actions + action_index;
            if (action->kind >= CODEGEN_UNWIND_ACTION_COUNT || action->code_offset < previous_offset || action->code_offset > function->prolog_size ||
                (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK && !action->value))
            {
                return false;
            }
            previous_offset = action->code_offset;
        }
        u32 previous_epilog = 0;
        for (u32 epilog_index = 0; epilog_index < function->epilog_count; epilog_index += 1)
        {
            u32 epilog = function->epilog_offsets[epilog_index];
            if (epilog < function->prolog_size || epilog >= function->code_size || (epilog_index && epilog <= previous_epilog))
            {
                return false;
            }
            previous_epilog = epilog;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool object_relocation_kind_from_codegen(CodegenModuleRelocationKind source, ObjectRelocationKind* destination)
{
    if (!destination)
    {
        return false;
    }
    switch (source)
    {
        case CODEGEN_MODULE_RELOCATION_X86_64_PC32: *destination = OBJECT_RELOCATION_X86_64_PC32; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_CALL26: *destination = OBJECT_RELOCATION_AARCH64_CALL26; return true;
        case CODEGEN_MODULE_RELOCATION_ABSOLUTE32: *destination = OBJECT_RELOCATION_ABSOLUTE32; return true;
        case CODEGEN_MODULE_RELOCATION_ABSOLUTE64: *destination = OBJECT_RELOCATION_ABSOLUTE64; return true;
        case CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32: *destination = OBJECT_RELOCATION_X86_64_TPOFF32; return true;
        case CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32: *destination = OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32; return true;
        case CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32: *destination = OBJECT_RELOCATION_PE_TLS_OFFSET32; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP: *destination = OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12: *destination = OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12: *destination = OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12: *destination = OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12: *destination = OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12; return true;
        case CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32: *destination = OBJECT_RELOCATION_X86_64_MACH_TLV_PC32; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21: *destination = OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12: *destination = OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGE21: *destination = OBJECT_RELOCATION_AARCH64_MACH_PAGE21; return true;
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGEOFF12: *destination = OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12; return true;
        case CODEGEN_MODULE_RELOCATION_COUNT: return false;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool object_codegen_relocation_width(ObjectRelocationKind kind, u32* width)
{
    if (!width)
    {
        return false;
    }
    switch (kind)
    {
        case OBJECT_RELOCATION_ABSOLUTE32: *width = 4; return true;
        case OBJECT_RELOCATION_ABSOLUTE64: *width = 8; return true;
        default: *width = 4; return true;
    }
}

ObjectFile object_from_codegen_module(Arena* arena, AnalysisResult* analysis, CodegenModule* module, Target target)
{
    ObjectFile result = {
        .target = target,
    };
    if (!arena || !analysis || !module || module->error != CODEGEN_ERROR_NONE || !object_codegen_functions_valid(module) ||
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
    result.sections[OBJECT_SECTION_ZERO] = (ObjectSection){
        .name = S8(".bss"),
        .kind = OBJECT_SECTION_ZERO,
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
    object_metadata_sections_initialize(&result);
    ObjectWindowsUnwindResult windows_unwind = {0};
    if (target.os == OPERATING_SYSTEM_WINDOWS && (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64))
    {
        windows_unwind = target.cpu_arch == CPU_ARCH_X86_64 ? object_windows_x64_unwind_build(arena, module->functions, module->function_count)
                                                             : object_windows_arm64_unwind_build(arena, module->functions, module->function_count);
        if (!windows_unwind.valid)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
    }
    DwarfCfiResult cfi = {0};
    if (object_format_for_target(target) != OBJECT_FORMAT_COFF && module->function_count)
    {
        cfi = dwarf_cfi_build(arena, (DwarfCfiInput){
                                         .functions = module->functions,
                                         .target = target,
                                         .function_count = module->function_count,
                                     });
        if (!cfi.valid)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
    }
    DwarfResult dwarf = {0};
    CodeviewResult codeview = {0};
    if (module->debug_info && analysis->module.source_count && (module->entry_count || module->global_count))
    {
        String8* file_paths = arena_allocate(arena, String8, analysis->module.source_count);
        for (u32 source_index = 0; source_index < analysis->module.source_count; source_index += 1)
        {
            file_paths[source_index] = analysis->module.sources[source_index].path;
        }
        DwarfFunction* functions = arena_allocate(arena, DwarfFunction, module->entry_count);
        DebugFunctionSeed* debug_functions = arena_allocate(arena, DebugFunctionSeed, module->entry_count);
        for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
        {
            CodegenModuleEntry* entry = module->entries + entry_index;
            u64 end = entry->offset + module->functions[entry_index].code_size;
            AnalysisEntity* definition = object_entity_find(analysis, entry->entity);
            functions[entry_index] = (DwarfFunction){
                .name = definition ? definition->name : object_entity_name(arena, analysis, entry->entity, entry->instantiation),
                .code_offset = entry->offset,
                .code_size = (u32)(end - entry->offset),
                .file = definition && definition->source.value < analysis->module.source_count ? definition->source.value : 0,
                .line = definition ? definition->range.line : 0,
            };
            debug_functions[entry_index] = (DebugFunctionSeed){
                .name = functions[entry_index].name,
                .symbol = entry->symbol,
                .entity = entry->entity,
                .instantiation = entry->instantiation,
                .code_offset = entry->offset,
                .code_size = (u32)(end - entry->offset),
            };
        }
        u32 line_count = module->entry_count ? module->line_entry_count : 0;
        DwarfLineEntry* lines = arena_allocate(arena, DwarfLineEntry, line_count);
        u32 function_cursor = 0;
        for (u32 line_index = 0; line_index < line_count; line_index += 1)
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
            DebugModel debug_model = debug_model_build(arena, (DebugModelInput){
                                                                   .analysis = analysis,
                                                                   .module = module->ir_module,
                                                                   .producer = S8("buster"),
                                                                   .comp_dir = S8("."),
                                                                   .functions = debug_functions,
                                                                   .locations = module->debug_locations,
                                                                   .function_count = module->entry_count,
                                                                   .location_count = module->debug_location_count,
                                                                   .canonical = false,
                                                               });
            codeview = codeview_build(arena, (CodeviewInput){
                                                 .model = &debug_model,
                                                 .producer = S8("buster"),
                                                 .file_paths = file_paths,
                                                 .functions = functions,
                                                 .lines = lines,
                                                 .file_count = analysis->module.source_count,
                                                 .function_count = module->entry_count,
                                                 .line_count = line_count,
                                                 .machine = target.cpu_arch == CPU_ARCH_AARCH64 ? CODEVIEW_MACHINE_ARM64 : CODEVIEW_MACHINE_X64,
                                             });
        }
        else
        {
            DebugModel debug_model = debug_model_build(arena, (DebugModelInput){
                                                                   .analysis = analysis,
                                                                   .module = module->ir_module,
                                                                   .producer = S8("buster"),
                                                                   .comp_dir = S8("."),
                                                                   .functions = debug_functions,
                                                                   .locations = module->debug_locations,
                                                                   .function_count = module->entry_count,
                                                                   .location_count = module->debug_location_count,
                                                                   .canonical = false,
                                                               });
            dwarf = dwarf_build(arena, (DwarfInput){
                                           .model = &debug_model,
                                           .target = target,
                                           .producer = S8("buster"),
                                           .comp_dir = S8("."),
                                           .file_paths = file_paths,
                                           .functions = functions,
                                           .lines = lines,
                                           .code_size = module->code.length,
                                           .file_count = analysis->module.source_count,
                                           .function_count = module->entry_count,
                                           .line_count = line_count,
                                           .language = 0x000c,
                                       });
        }
    }
    u32 metadata_relocation_count = (dwarf.valid ? dwarf.relocation_count : 0) + (codeview.valid ? codeview.relocation_count : 0) + cfi.relocation_count +
                                    windows_unwind.function_count * (windows_unwind.aarch64 ? 2u : 3u);
    u32 symbol_capacity =
        module->entry_count + module->relocation_count + (module->data_relocation_count ? 1 : 0) + (dwarf.valid ? OBJECT_DWARF_EXTRA_SYMBOLS : 0) +
        (windows_unwind.function_count ? 1 : 0);
    result.symbols = arena_allocate(arena, ObjectSymbol, symbol_capacity);
    for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
    {
        CodegenModuleEntry* entry = module->entries + entry_index;
        u64 end = entry->offset + module->functions[entry_index].code_size;
        result.symbols[result.symbol_count++] = (ObjectSymbol){
            .name = object_entity_name(arena, analysis, entry->entity, entry->instantiation),
            .value = entry->offset,
            .size = end - entry->offset,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        };
    }
    result.relocations = arena_allocate(arena, ObjectRelocation, module->relocation_count + metadata_relocation_count);
    Arena* lookup_conflicts[] = {
        arena,
    };
    bool entry_index_initialized = module->relocation_count >= OBJECT_LOOKUP_INDEX_MIN_QUERY_COUNT;
    bool name_index_initialized = false;
    u32 missing_tuple_query_count = 0;
    TemporalArena lookup_temporary = {0};
    ObjectCodegenEntryIndex entry_index = {0};
    ObjectSymbolNameIndex name_index = {0};
    if (entry_index_initialized)
    {
        lookup_temporary = scratch_begin(lookup_conflicts, BUSTER_ARRAY_LENGTH(lookup_conflicts));
        entry_index = object_codegen_entry_index_build(lookup_temporary.arena, module->entries, module->entry_count);
    }
    for (u32 relocation_index = 0; relocation_index < module->relocation_count; relocation_index += 1)
    {
        CodegenModuleRelocation* source = module->relocations + relocation_index;
        ObjectRelocationKind kind = OBJECT_RELOCATION_X86_64_PC32;
        if (!codegen_module_relocation_valid(source) || !object_relocation_kind_from_codegen((CodegenModuleRelocationKind)source->kind, &kind))
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            if (entry_index_initialized)
            {
                scratch_end(lookup_temporary);
            }
            return result;
        }
        u32 symbol_index = UINT32_MAX;
        if (entry_index_initialized)
        {
            u32* entry_slot = object_codegen_entry_slot(entry_index, source->entity, source->instantiation);
            symbol_index = entry_slot ? *entry_slot : UINT32_MAX;
        }
        else
        {
            for (u32 candidate_index = 0; candidate_index < module->entry_count; candidate_index += 1)
            {
                CodegenModuleEntry* candidate = module->entries + candidate_index;
                if (candidate->entity.module.value == source->entity.module.value && candidate->entity.index.value == source->entity.index.value &&
                    candidate->instantiation.value == source->instantiation.value)
                {
                    symbol_index = candidate_index;
                    break;
                }
            }
        }
        if (symbol_index == UINT32_MAX)
        {
            missing_tuple_query_count += 1;
            if (!name_index_initialized && missing_tuple_query_count == OBJECT_LOOKUP_INDEX_MIN_QUERY_COUNT)
            {
                name_index = object_symbol_name_index_build(lookup_temporary.arena, result.symbols, result.symbol_count,
                                                            (u64)result.symbol_count + module->relocation_count - relocation_index);
                name_index_initialized = true;
            }
            String8 name = object_entity_name(arena, analysis, source->entity, source->instantiation);
            if (name_index_initialized)
            {
                ObjectSymbolNameSlot* name_slot = object_symbol_name_slot(name_index, name);
                if (name_slot->used)
                {
                    symbol_index = name_slot->undefined;
                }
            }
            else
            {
                for (u32 candidate_index = 0; candidate_index < result.symbol_count; candidate_index += 1)
                {
                    ObjectSymbol* candidate = result.symbols + candidate_index;
                    if (candidate->section == OBJECT_SECTION_UNDEFINED && string_equal(candidate->name, name))
                    {
                        symbol_index = candidate_index;
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
                    .kind = OBJECT_SYMBOL_FUNCTION,
                    .global = true,
                };
                if (name_index_initialized)
                {
                    object_symbol_name_index_add(name_index, result.symbols + symbol_index, symbol_index);
                }
            }
        }
        result.relocations[result.relocation_count++] = (ObjectRelocation){
            .addend = kind == OBJECT_RELOCATION_X86_64_PC32 ? -4 : 0,
            .offset = source->offset,
            .section = OBJECT_SECTION_TEXT,
            .symbol = symbol_index,
            .kind = kind,
        };
        u32 relocation_width = 4;
        object_codegen_relocation_width(kind, &relocation_width);
        if (kind == OBJECT_RELOCATION_ABSOLUTE32 || kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            if (source->offset > module->code.length || relocation_width > module->code.length - source->offset)
            {
                result.error = OBJECT_ERROR_INVALID_INPUT;
                if (entry_index_initialized)
                {
                    scratch_end(lookup_temporary);
                }
                return result;
            }
            memset(text + source->offset, 0, relocation_width);
        }
        else if (kind == OBJECT_RELOCATION_AARCH64_CALL26)
        {
            if (source->offset + 4 > module->code.length)
            {
                result.error = OBJECT_ERROR_INVALID_INPUT;
                if (entry_index_initialized)
                {
                    scratch_end(lookup_temporary);
                }
                return result;
            }
            u32 instruction = 0x94000000;
            memcpy(text + source->offset, &instruction, sizeof(instruction));
        }
        else if (kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 || kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12)
        {
            if (source->offset + 4 > module->code.length)
            {
                result.error = OBJECT_ERROR_INVALID_INPUT;
                if (entry_index_initialized)
                {
                    scratch_end(lookup_temporary);
                }
                return result;
            }
            u32 instruction = kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 ? UINT32_C(0x90000009) : UINT32_C(0x91000129);
            memcpy(text + source->offset, &instruction, sizeof(instruction));
        }
        else
        {
            if (source->offset + 4 > module->code.length)
            {
                result.error = OBJECT_ERROR_INVALID_INPUT;
                if (entry_index_initialized)
                {
                    scratch_end(lookup_temporary);
                }
                return result;
            }
            memset(text + source->offset, 0, 4);
        }
    }
    if (entry_index_initialized)
    {
        scratch_end(lookup_temporary);
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
        ObjectRelocation* relocations =
            arena_allocate(arena, ObjectRelocation, module->relocation_count + module->data_relocation_count + metadata_relocation_count);
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
    if (cfi.valid && !object_append_dwarf_cfi(&result, cfi))
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    if (windows_unwind.valid && !object_append_windows_unwind(arena, &result, windows_unwind))
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    object_debug_module_set(arena, &result, analysis->module.source_count ? analysis->module.sources[0].path : S8("buster.obj"), module->code.length);
    return result;
}

ObjectFile object_from_canonical_codegen_module(Arena* arena, IrProgram* program, CodegenModule* module, Target target)
{
    ObjectFile result = {
        .target = target,
    };
    if (!arena || !program || !module || module->error != CODEGEN_ERROR_NONE || module->data_relocation_count || !object_codegen_functions_valid(module) ||
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
    result.sections[OBJECT_SECTION_ZERO] = (ObjectSection){
        .name = S8(".bss"),
        .virtual_size = module->zero_fill_size,
        .kind = OBJECT_SECTION_ZERO,
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
    object_metadata_sections_initialize(&result);
    ObjectWindowsUnwindResult windows_unwind = {0};
    if (target.os == OPERATING_SYSTEM_WINDOWS && (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64))
    {
        windows_unwind = target.cpu_arch == CPU_ARCH_X86_64 ? object_windows_x64_unwind_build(arena, module->functions, module->function_count)
                                                             : object_windows_arm64_unwind_build(arena, module->functions, module->function_count);
        if (!windows_unwind.valid)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
    }
    DwarfCfiResult cfi = {0};
    if (object_format_for_target(target) != OBJECT_FORMAT_COFF && module->function_count)
    {
        cfi = dwarf_cfi_build(arena, (DwarfCfiInput){
                                         .functions = module->functions,
                                         .target = target,
                                         .function_count = module->function_count,
                                     });
        if (!cfi.valid)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
    }
    DwarfResult dwarf = {0};
    CodeviewResult codeview = {0};
    if (module->debug_info && program->sources.count && (module->entry_count || module->global_count))
    {
        IrModule* debug_ir_module = module->ir_module;
        for (u32 module_index = 0; module_index < program->module_count && !debug_ir_module; module_index += 1)
        {
            IrModule* candidate = program->modules + module_index;
            for (u32 function_index = 0; function_index < candidate->function_count && module->entry_count; function_index += 1)
            {
                if (candidate->functions[function_index].symbol.value == module->entries[0].symbol.value)
                {
                    debug_ir_module = candidate;
                    break;
                }
            }
            for (u32 global_index = 0; global_index < candidate->global_count && !debug_ir_module && module->global_count; global_index += 1)
            {
                for (u32 object_global_index = 0; object_global_index < module->global_count; object_global_index += 1)
                {
                    if (candidate->globals[global_index].symbol.value == module->globals[object_global_index].symbol.value)
                    {
                        debug_ir_module = candidate;
                        break;
                    }
                }
            }
        }
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
        DebugFunctionSeed* debug_functions = arena_allocate(arena, DebugFunctionSeed, module->entry_count);
        for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
        {
            CodegenModuleEntry* entry = module->entries + entry_index;
            u64 end = entry->offset + module->functions[entry_index].code_size;
            IrSymbol* entry_symbol = ir_symbol_from_id(&program->symbols, entry->symbol);
            IrSourceRange declaration = entry->symbol.value < program->symbols.count ? declaration_sources[entry->symbol.value] : (IrSourceRange){0};
            functions[entry_index] = (DwarfFunction){
                .name = entry_symbol ? entry_symbol->name : S8("unknown"),
                .code_offset = entry->offset,
                .code_size = (u32)(end - entry->offset),
                .file = declaration.source.value < program->sources.count ? declaration.source.value : 0,
                .line = ir_source_position(program, declaration).line,
            };
            debug_functions[entry_index] = (DebugFunctionSeed){
                .name = functions[entry_index].name,
                .symbol = entry->symbol,
                .code_offset = entry->offset,
                .code_size = (u32)(end - entry->offset),
            };
        }
        u32 line_count = module->entry_count ? module->line_entry_count : 0;
        DwarfLineEntry* lines = arena_allocate(arena, DwarfLineEntry, line_count);
        for (u32 line_index = 0; line_index < line_count; line_index += 1)
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
            DebugModel debug_model = debug_model_build(arena, (DebugModelInput){
                                                                   .program = program,
                                                                   .module = debug_ir_module,
                                                                   .producer = S8("buster"),
                                                                   .comp_dir = S8("."),
                                                                   .functions = debug_functions,
                                                                   .locations = module->debug_locations,
                                                                   .function_count = module->entry_count,
                                                                   .location_count = module->debug_location_count,
                                                                   .canonical = true,
                                                               });
            codeview = codeview_build(arena, (CodeviewInput){
                                                 .model = &debug_model,
                                                 .producer = S8("buster"),
                                                 .file_paths = file_paths,
                                                 .functions = functions,
                                                 .lines = lines,
                                                 .file_count = program->sources.count,
                                                 .function_count = module->entry_count,
                                                 .line_count = line_count,
                                                 .machine = target.cpu_arch == CPU_ARCH_AARCH64 ? CODEVIEW_MACHINE_ARM64 : CODEVIEW_MACHINE_X64,
                                             });
        }
        else
        {
            DebugModel debug_model = debug_model_build(arena, (DebugModelInput){
                                                                   .program = program,
                                                                   .module = debug_ir_module,
                                                                   .producer = S8("buster"),
                                                                   .comp_dir = S8("."),
                                                                   .functions = debug_functions,
                                                                   .locations = module->debug_locations,
                                                                   .function_count = module->entry_count,
                                                                   .location_count = module->debug_location_count,
                                                                   .canonical = true,
                                                               });
            dwarf = dwarf_build(arena, (DwarfInput){
                                           .model = &debug_model,
                                           .target = target,
                                           .producer = S8("buster"),
                                           .comp_dir = S8("."),
                                           .file_paths = file_paths,
                                           .functions = functions,
                                           .lines = lines,
                                           .code_size = module->code.length,
                                           .file_count = program->sources.count,
                                           .function_count = module->entry_count,
                                           .line_count = line_count,
                                           .language = 0x000c,
                                       });
        }
    }
    u32 metadata_relocation_count = (dwarf.valid ? dwarf.relocation_count : 0) + (codeview.valid ? codeview.relocation_count : 0) + cfi.relocation_count +
                                    windows_unwind.function_count * (windows_unwind.aarch64 ? 2u : 3u);
    bool apple_thread_local = false;
    if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
    {
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            apple_thread_local |= module->globals[global_index].is_thread_local;
        }
    }
    result.symbols = arena_allocate(arena, ObjectSymbol, module->entry_count + module->global_count + module->relocation_count + (apple_thread_local ? 1 : 0) +
                                                             (dwarf.valid ? OBJECT_DWARF_EXTRA_SYMBOLS : 0) +
                                                             (windows_unwind.function_count ? 1 : 0));
    for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
    {
        CodegenModuleEntry entry = module->entries[entry_index];
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, entry.symbol);
        if (!symbol)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
        u64 end = entry.offset + module->functions[entry_index].code_size;
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
            global.zero_fill ? (ByteSlice){.length = global.is_thread_local ? module->thread_local_zero_size : module->zero_fill_size} :
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
            .section = global.zero_fill         ? (global.is_thread_local ? OBJECT_SECTION_THREAD_LOCAL_ZERO : OBJECT_SECTION_ZERO)
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
    result.relocations = arena_allocate(arena, ObjectRelocation, module->relocation_count + metadata_relocation_count);
    Arena* name_conflicts[] = {
        arena,
    };
    TemporalArena name_temporary = scratch_begin(name_conflicts, BUSTER_ARRAY_LENGTH(name_conflicts));
    u32 entry_symbol_capacity = program->symbols.count ? program->symbols.count : 1;
    u32* entry_by_symbol = arena_allocate(name_temporary.arena, u32, entry_symbol_capacity);
    memset(entry_by_symbol, 0xFF, sizeof(*entry_by_symbol) * entry_symbol_capacity);
    for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
    {
        u32 symbol_value = module->entries[entry_index].symbol.value;
        if (symbol_value < entry_symbol_capacity && entry_by_symbol[symbol_value] == UINT32_MAX)
        {
            entry_by_symbol[symbol_value] = entry_index;
        }
    }
    ObjectSymbolNameIndex name_index = object_symbol_name_index_build(name_temporary.arena, result.symbols, result.symbol_count,
                                                                      (u64)result.symbol_count + module->relocation_count);
    for (u32 relocation_index = 0; relocation_index < module->relocation_count; relocation_index += 1)
    {
        CodegenModuleRelocation source = module->relocations[relocation_index];
        ObjectRelocationKind kind = OBJECT_RELOCATION_X86_64_PC32;
        if (!codegen_module_relocation_valid(&source) || !object_relocation_kind_from_codegen((CodegenModuleRelocationKind)source.kind, &kind))
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        IrSymbol* target_symbol = ir_symbol_from_id(&program->symbols, source.symbol);
        ByteSlice source_data = source.source == CODEGEN_MODULE_RELOCATION_CODE                ? module->code
                                : source.source == CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA    ? module->read_only_data
                                : source.source == CODEGEN_MODULE_RELOCATION_DATA              ? module->writable_data
                                : source.source == CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA ? module->thread_local_data
                                                                                               : (ByteSlice){0};
        u32 relocation_width = 4;
        object_codegen_relocation_width(kind, &relocation_width);
        if (!target_symbol || source.source >= CODEGEN_MODULE_RELOCATION_SOURCE_COUNT || source.offset > source_data.length ||
            relocation_width > source_data.length - source.offset)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        u32 symbol_index = source.symbol.value < entry_symbol_capacity ? entry_by_symbol[source.symbol.value] : UINT32_MAX;
        String8 name = target_symbol->link_name.length ? target_symbol->link_name : target_symbol->name;
        if (symbol_index == UINT32_MAX)
        {
            ObjectSymbolNameSlot* slot = object_symbol_name_slot(name_index, name);
            if (slot->used)
            {
                symbol_index = slot->defined != UINT32_MAX ? slot->defined : slot->undefined;
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
            object_symbol_name_index_add(name_index, &result.symbols[symbol_index], symbol_index);
        }
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
    scratch_end(name_temporary);
    if (result.error != OBJECT_ERROR_NONE)
    {
        return result;
    }
    object_append_dwarf(&result, dwarf);
    object_append_codeview(&result, codeview);
    if (cfi.valid && !object_append_dwarf_cfi(&result, cfi))
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    if (windows_unwind.valid && !object_append_windows_unwind(arena, &result, windows_unwind))
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    object_debug_module_set(arena, &result, program->sources.count ? program->sources.sources[0].path : S8("buster.obj"), module->code.length);
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
    return kind == OBJECT_RELOCATION_AARCH64_JUMP26                 ? 282
           : kind == OBJECT_RELOCATION_AARCH64_CALL26               ? 283
           : kind == OBJECT_RELOCATION_AARCH64_PREL32               ? 261
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
        if (!object_section_kind_is_zero_fill(object->sections[section].kind))
        {
            object_buffer_write(&buffer, object->sections[section].data.pointer, object->sections[section].data.length);
        }
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
            if (object_section_kind_is_zero_fill(source->kind))
            {
                type = 8;
            }
            else if (source->kind == OBJECT_SECTION_UNWIND && object->target.cpu_arch == CPU_ARCH_X86_64)
            {
                type = 0x70000001;
            }
            flags = source->kind == OBJECT_SECTION_TEXT                                                                    ? 0x6
                    : source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA || source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? 0x403
                    : source->kind == OBJECT_SECTION_DATA || source->kind == OBJECT_SECTION_ZERO                           ? 0x3
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
               : kind == OBJECT_RELOCATION_COFF_ADDR32NB            ? 0x0003
                                                                    : 0;
    }
    return kind == OBJECT_RELOCATION_AARCH64_CALL26 || kind == OBJECT_RELOCATION_AARCH64_JUMP26 ? 0x0003
           : kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP ? 0x0004
           : kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12 ? 0x0007
           : kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12   ? 0x000f
           : kind == OBJECT_RELOCATION_ABSOLUTE64                ? 0x000e
           : kind == OBJECT_RELOCATION_ABSOLUTE32                ? 0x0001
           : kind == OBJECT_RELOCATION_COFF_SECREL32             ? 0x0008
           : kind == OBJECT_RELOCATION_COFF_SECTION16            ? 0x0007
           : kind == OBJECT_RELOCATION_COFF_ADDR32NB             ? 0x0002
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
        ObjectSection* object_section = object->sections + section;
        bool zero_fill = object_section_kind_is_zero_fill(object_section->kind);
        object_buffer_align(&buffer, 4);
        raw_offsets[section] = zero_fill ? 0 : (u32)buffer.count;
        if (!zero_fill)
        {
            object_buffer_write(&buffer, object_section->data.pointer, object_section->data.length);
        }
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
            else if (addend && source->kind != OBJECT_RELOCATION_AARCH64_CALL26 && source->kind != OBJECT_RELOCATION_AARCH64_JUMP26)
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
        object_write_u32_at(&buffer, offset + 16, (u32)(object_section_kind_is_zero_fill(source->kind) ? source->virtual_size : source->data.length));
        object_write_u32_at(&buffer, offset + 20, raw_offsets[section]);
        object_write_u32_at(&buffer, offset + 24, relocation_counts[section] ? relocation_offsets[section] : 0);
        object_write_u16_at(&buffer, offset + 32, relocation_counts[section]);
        u32 characteristics = source->kind == OBJECT_SECTION_TEXT             ? 0x60500020
                              : source->kind == OBJECT_SECTION_READ_ONLY_DATA ? 0x40500040
                              : source->kind == OBJECT_SECTION_WINDOWS_PDATA || source->kind == OBJECT_SECTION_WINDOWS_XDATA
                                  ? 0x40300040
                              : object_section_kind_is_debug(source->kind)     ? 0x42100040
                              : object_section_kind_is_zero_fill(source->kind) ? 0xc0500080
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
        return kind == OBJECT_RELOCATION_AARCH64_CALL26 || kind == OBJECT_RELOCATION_AARCH64_JUMP26 ? 2
               : kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21    ? 8
               : kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 ? 9
               : kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21         ? 3
               : kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12      ? 4
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
    u32 prel32_count = 0;
    for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
    {
        prel32_count += object->relocations[relocation].kind == OBJECT_RELOCATION_AARCH64_PREL32;
    }
    if (prel32_count > UINT32_MAX - object->symbol_count || object->symbol_count + prel32_count > 0x00ffffff)
    {
        result.error = OBJECT_ERROR_CAPACITY;
        return result;
    }
    u32 symbol_count = object->symbol_count + prel32_count;
    u32* prel32_place_symbols = arena_allocate(arena, u32, object->relocation_count);
    u32 next_place_symbol = object->symbol_count;
    for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
    {
        prel32_place_symbols[relocation] = object->relocations[relocation].kind == OBJECT_RELOCATION_AARCH64_PREL32 ? next_place_symbol++ : UINT32_MAX;
    }
    if (section_count > (UINT32_MAX - MACH_SEGMENT_COMMAND_SIZE) / MACH_SECTION_SIZE)
    {
        result.error = OBJECT_ERROR_CAPACITY;
        return result;
    }
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
        segment_virtual_size += object_section_kind_is_zero_fill(object->sections[section].kind) ? object->sections[section].virtual_size
                                                                                                  : object->sections[section].data.length;
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
            else if (source->kind == OBJECT_RELOCATION_AARCH64_PREL32)
            {
                // Mach-O arm64 PREL32 stores its signed addend in the
                // relocated word.  Always rewrite the slot, including a
                // zero addend, so stale input bytes cannot survive a write.
                object_write_u32_at(&buffer, section_offsets[section] + source->offset, (u32)(s32)addend);
            }
            else if (addend && source->kind != OBJECT_RELOCATION_AARCH64_CALL26 && source->kind != OBJECT_RELOCATION_AARCH64_JUMP26 &&
                     source->kind != OBJECT_RELOCATION_AARCH64_MACH_PAGE21 && source->kind != OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12 &&
                     source->kind != OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 && source->kind != OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12)
            {
                object_write_u32_at(&buffer, section_offsets[section] + source->offset, (u32)addend);
            }
            if (source->kind == OBJECT_RELOCATION_AARCH64_PREL32)
            {
                u64 offset = buffer.count;
                object_buffer_zero(&buffer, 2 * MACH_RELOCATION_SIZE);
                object_write_u32_at(&buffer, offset, (u32)source->offset);
                object_write_u32_at(&buffer, offset + 4, prel32_place_symbols[relocation] | (2u << 25) | (1u << 27) | (1u << 28));
                object_write_u32_at(&buffer, offset + MACH_RELOCATION_SIZE, (u32)source->offset);
                object_write_u32_at(&buffer, offset + MACH_RELOCATION_SIZE + 4, source->symbol | (2u << 25) | (1u << 27));
                relocation_counts[section] += 2;
                continue;
            }
            if ((source->kind == OBJECT_RELOCATION_AARCH64_CALL26 || source->kind == OBJECT_RELOCATION_AARCH64_JUMP26 ||
                 source->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 || source->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12 ||
                 source->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 || source->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12) &&
                addend)
            {
                u32 encoded_addend = 0;
                if (!a64_signed_scaled_immediate_encode(addend, 24, 0, &encoded_addend))
                {
                    buffer.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                    break;
                }
                u64 offset = buffer.count;
                object_buffer_zero(&buffer, MACH_RELOCATION_SIZE);
                object_write_u32_at(&buffer, offset, (u32)source->offset);
                object_write_u32_at(&buffer, offset + 4, encoded_addend | (2u << 25) | (10u << 28));
                relocation_counts[section] += 1;
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
                               source->kind != OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 &&
                               source->kind != OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12;
            if (source->section == OBJECT_SECTION_UNWIND && source->kind == OBJECT_RELOCATION_X86_64_PC32)
            {
                type = 1;
            }
            u32 word = (source->symbol & 0x00ffffff) | (pc_relative ? 1u << 24 : 0) | ((source->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 3u : 2u) << 25) |
                       (1u << 27) | (type << 28);
            object_write_u32_at(&buffer, offset + 4, word);
            relocation_counts[section] += 1;
        }
    }
    object_buffer_align(&buffer, 8);
    u32 symbol_offset = (u32)buffer.count;
    object_buffer_zero(&buffer, (u64)symbol_count * MACH_SYMBOL_SIZE);
    u32 string_offset = (u32)buffer.count;
    u8 zero = 0;
    object_buffer_write(&buffer, &zero, 1);
    u32* symbol_name_offsets = arena_allocate(arena, u32, symbol_count);
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        symbol_name_offsets[symbol] = (u32)(buffer.count - string_offset);
        object_buffer_write(&buffer, "_", 1);
        object_buffer_write(&buffer, object->symbols[symbol].name.pointer, object->symbols[symbol].name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    for (u32 symbol = object->symbol_count; symbol < symbol_count; symbol += 1)
    {
        symbol_name_offsets[symbol] = (u32)(buffer.count - string_offset);
        String8 name = string_format(arena, S8("L_buster_eh_place_{u32}"), symbol - object->symbol_count);
        object_buffer_write(&buffer, name.pointer, name.length);
        object_buffer_write(&buffer, &zero, 1);
    }
    for (u32 symbol = 0; symbol < object->symbol_count; symbol += 1)
    {
        ObjectSymbol* source = object->symbols + symbol;
        u64 offset = symbol_offset + (u64)symbol * MACH_SYMBOL_SIZE;
        object_write_u32_at(&buffer, offset, symbol_name_offsets[symbol]);
        buffer.bytes[offset + 4] = source->section == OBJECT_SECTION_UNDEFINED ? 0x01 : (u8)(0x0e | (source->global ? 1 : 0));
        buffer.bytes[offset + 5] = source->section == OBJECT_SECTION_UNDEFINED ? 0 : (u8)(source->section + 1);
        // Undefined symbols use REFERENCE_FLAG_UNDEFINED_NON_LAZY (0).
        // REFERENCE_FLAG_UNDEFINED_LAZY (1) is reserved for symbols reached
        // through a lazy-symbol-pointer section, which this object model does
        // not synthesize.  Mach-O has no separate defined-function bit; the
        // reader uses the canonical __text section to retain that kind.
        u16 reference_kind = source->section == OBJECT_SECTION_UNDEFINED ? 0 : 2;
        object_write_u16_at(&buffer, offset + 6, reference_kind);
        object_write_u64_at(&buffer, offset + 8, source->value + (source->section == OBJECT_SECTION_UNDEFINED ? 0 : section_addresses[source->section]));
    }
    for (u32 relocation = 0; relocation < object->relocation_count; relocation += 1)
    {
        u32 symbol = prel32_place_symbols[relocation];
        if (symbol == UINT32_MAX)
        {
            continue;
        }
        ObjectRelocation* source = object->relocations + relocation;
        u64 offset = symbol_offset + (u64)symbol * MACH_SYMBOL_SIZE;
        object_write_u32_at(&buffer, offset, symbol_name_offsets[symbol]);
        buffer.bytes[offset + 4] = 0x0e;
        buffer.bytes[offset + 5] = (u8)(source->section + 1);
        object_write_u64_at(&buffer, offset + 8, section_addresses[source->section] + source->offset);
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
                               : source->kind == OBJECT_SECTION_ZERO              ? S8("__bss")
                               : source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA ? S8("__thread_data")
                               : source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? S8("__thread_bss")
                               : source->kind == OBJECT_SECTION_UNWIND            ? S8("__eh_frame")
                               : source->kind == OBJECT_SECTION_WINDOWS_PDATA     ? S8("__pdata")
                               : source->kind == OBJECT_SECTION_WINDOWS_XDATA     ? S8("__xdata")
                               : source->kind == OBJECT_SECTION_DEBUG_INFO        ? S8("__debug_info")
                               : source->kind == OBJECT_SECTION_DEBUG_ABBREV     ? S8("__debug_abbrev")
                               : source->kind == OBJECT_SECTION_DEBUG_LINE       ? S8("__debug_line")
                               : source->kind == OBJECT_SECTION_DEBUG_STR        ? S8("__debug_str")
                               : source->kind == OBJECT_SECTION_DEBUG_LOC        ? S8("__debug_loc")
                               : source->kind == OBJECT_SECTION_DEBUG_RANGES     ? S8("__debug_ranges")
                                                                               : S8("__data");
        String8 segment_name =
            (source->kind == OBJECT_SECTION_DATA || source->kind == OBJECT_SECTION_ZERO || source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA ||
             source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO || source->kind == OBJECT_SECTION_WINDOWS_PDATA ||
             source->kind == OBJECT_SECTION_WINDOWS_XDATA)
                ? S8("__DATA")
            : object_section_kind_is_debug(source->kind) ? S8("__DWARF")
                                                         : S8("__TEXT");
        object_mach_name_write(buffer.bytes + offset, 16, section_name);
        object_mach_name_write(buffer.bytes + offset + 16, 16, segment_name);
        object_write_u64_at(&buffer, offset + 32, section_addresses[section]);
        object_write_u64_at(&buffer, offset + 40, object_section_kind_is_zero_fill(source->kind) ? source->virtual_size : source->data.length);
        object_write_u32_at(&buffer, offset + 48, object_section_kind_is_zero_fill(source->kind) ? 0 : section_offsets[section]);
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
                            : source->kind == OBJECT_SECTION_ZERO              ? 0x1
                            : source->kind == OBJECT_SECTION_THREAD_LOCAL_DATA ? 0x11
                            : source->kind == OBJECT_SECTION_THREAD_LOCAL_ZERO ? 0x12
                            : source->kind == OBJECT_SECTION_UNWIND            ? 0x6800000b
                            : object_section_kind_is_debug(source->kind)       ? 0x02000000
                                                                               : 0);
    }
    u64 symtab_command = segment_offset + segment_size;
    object_write_u32_at(&buffer, symtab_command, 0x2);
    object_write_u32_at(&buffer, symtab_command + 4, MACH_SYMTAB_COMMAND_SIZE);
    object_write_u32_at(&buffer, symtab_command + 8, symbol_offset);
    object_write_u32_at(&buffer, symtab_command + 12, symbol_count);
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
        if ((source->data.length && !source->data.pointer) || (source->alignment && (source->alignment & (source->alignment - 1))) ||
            (object_section_kind_is_zero_fill(source->kind) && source->data.length))
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
        u64 section_length = source->section < object->section_count ? object->sections[source->section].data.length : 0;
        if (source->section >= object->section_count || source->symbol >= object->symbol_count || source->kind >= OBJECT_RELOCATION_COUNT ||
            source->offset > section_length || relocation_size > section_length - source->offset)
        {
            return result;
        }
        if (source->kind == OBJECT_RELOCATION_AARCH64_PREL32)
        {
            if ((source->offset & 3) || object->sections[source->section].alignment < 4)
            {
                return result;
            }
            if (source->addend < INT32_MIN || source->addend > INT32_MAX)
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
        }
        if (format == OBJECT_FORMAT_MACH_O64 && object->target.cpu_arch == CPU_ARCH_AARCH64 && source->kind == OBJECT_RELOCATION_ABSOLUTE32 &&
            !object_section_kind_is_debug(object->sections[source->section].kind))
        {
            // arm64 Mach-O's 32-bit UNSIGNED record is reserved here for the
            // second half of the canonical PREL32 pair; a bare record is not
            // a lossless representation in this object model.
            result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
            return result;
        }
        if (source->kind == OBJECT_RELOCATION_AARCH64_CALL26 || source->kind == OBJECT_RELOCATION_AARCH64_JUMP26)
        {
            u32 instruction = 0;
            A64MCInst decoded = {0};
            A64Opcode opcode = source->kind == OBJECT_RELOCATION_AARCH64_CALL26 ? A64_OPCODE_BL : A64_OPCODE_B;
            memcpy(&instruction, object->sections[source->section].data.pointer + source->offset, sizeof(instruction));
            if ((source->offset & 3) || object->sections[source->section].alignment < 4 || !a64_mc_decode(instruction, &decoded) ||
                decoded.opcode != opcode || decoded.operands[0].value != 0)
            {
                return result;
            }
            if (format == OBJECT_FORMAT_COFF && source->addend != 0)
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
            if (format == OBJECT_FORMAT_MACH_O64 && (source->addend < -INT64_C(0x800000) || source->addend > INT64_C(0x7fffff)))
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
        }
        if (source->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 || source->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12 ||
            source->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 || source->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12)
        {
            u32 instruction = 0;
            u32 shift = 0;
            memcpy(&instruction, object->sections[source->section].data.pointer + source->offset, sizeof(instruction));
            if ((source->offset & 3) || object->sections[source->section].alignment < 4 ||
                ((source->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 || source->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21)
                     ? !object_mach_page21_instruction_valid(instruction)
                     : !object_mach_pageoff12_shift(instruction, &shift)))
            {
                return result;
            }
            // A TLVP pageoff denotes the 8-byte descriptor load generated by
            // Darwin's ABI.  Reject byte/half/word stores and unscaled ADDs
            // rather than accepting a relocation whose architectural meaning
            // would be ambiguous in an in-memory linker.
            if (source->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 &&
                ((instruction & UINT32_C(0xffc00000)) != UINT32_C(0xf9400000) || shift != 3))
            {
                return result;
            }
            if (format == OBJECT_FORMAT_MACH_O64 &&
                (source->addend < -INT64_C(0x800000) || source->addend > INT64_C(0x7fffff)))
            {
                result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
                return result;
            }
        }
        if (format == OBJECT_FORMAT_MACH_O64 && source->offset > INT32_MAX)
        {
            result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
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

BUSTER_GLOBAL_LOCAL bool object_address_difference(u64 target, u64 place, s64 addend, s64* result)
{
    return a64_pc_relative_displacement(target, place, addend, result);
}

ObjectExecutable object_link_executable(ObjectFile* object)
{
    ObjectExecutable result = {0};
    if (!object || object->error != OBJECT_ERROR_NONE || object->section_count <= OBJECT_SECTION_TEXT || object->section_count > OBJECT_SECTION_COUNT ||
        !object->sections || (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations))
    {
        result.error = OBJECT_ERROR_INVALID_INPUT;
        return result;
    }
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 image_size = 0;
    for (u32 section = 0; section < object->section_count; section += 1)
    {
        ObjectSection* source = object->sections + section;
        if ((u32)source->kind >= (u32)OBJECT_SECTION_COUNT || (source->data.length && !source->data.pointer) ||
            (object_section_kind_is_zero_fill(source->kind) && source->data.length) ||
            (source->alignment && (source->alignment & (source->alignment - 1))))
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            return result;
        }
        u64 alignment = object->sections[section].alignment;
        if (!alignment)
        {
            alignment = 1;
        }
        if (image_size > UINT64_MAX - (alignment - 1))
        {
            result.error = OBJECT_ERROR_CAPACITY;
            return result;
        }
        image_size = (image_size + alignment - 1) & ~(alignment - 1);
        section_offsets[section] = image_size;
        u64 section_size = BUSTER_MAX(source->data.length, source->virtual_size);
        if (section_size > UINT64_MAX - image_size)
        {
            result.error = OBJECT_ERROR_CAPACITY;
            return result;
        }
        image_size += section_size;
    }
    u64 page_size = os_get_page_size();
    if (!page_size || (page_size & (page_size - 1)) || image_size > UINT64_MAX - (page_size - 1))
    {
        result.error = OBJECT_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    u64 allocation_size = (image_size + page_size - 1) & ~(page_size - 1);
    if (!allocation_size)
    {
        allocation_size = page_size;
    }
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
        if (relocation->section >= object->section_count || relocation->symbol >= object->symbol_count)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        if ((u32)relocation->kind >= (u32)OBJECT_RELOCATION_COUNT)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        u64 relocation_size = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        ObjectSection* source_section = object->sections + relocation->section;
        if (relocation->offset > source_section->data.length || relocation_size > source_section->data.length - relocation->offset)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        if (relocation->kind == OBJECT_RELOCATION_AARCH64_PREL32 &&
            ((relocation->offset & 3) || source_section->alignment < 4))
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
        u8* patch = (u8*)address + section_offsets[relocation->section] + relocation->offset;
        if (symbol->section >= object->section_count)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        ObjectSection* target_section = object->sections + symbol->section;
        u64 target_section_size = BUSTER_MAX(target_section->data.length, target_section->virtual_size);
        if (symbol->value > target_section_size || symbol->size > target_section_size - symbol->value)
        {
            result.error = OBJECT_ERROR_INVALID_INPUT;
            break;
        }
        u8* target = (u8*)address + section_offsets[symbol->section] + symbol->value;
        if (relocation->kind == OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 displacement = 0;
            if (!object_address_difference((u64)(uintptr_t)target, (u64)(uintptr_t)patch, relocation->addend, &displacement) ||
                displacement < INT32_MIN || displacement > INT32_MAX)
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
            s32 value = (s32)displacement;
            memcpy(patch, &value, sizeof(value));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26 || relocation->kind == OBJECT_RELOCATION_AARCH64_JUMP26)
        {
            s64 displacement = 0;
            u32 instruction = 0;
            u32 patched = 0;
            memcpy(&instruction, patch, sizeof(instruction));
            A64Opcode opcode = relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26 ? A64_OPCODE_BL : A64_OPCODE_B;
            if (symbol->kind != OBJECT_SYMBOL_FUNCTION || ((u64)(uintptr_t)patch & 3) ||
                !object_address_difference((u64)(uintptr_t)target, (u64)(uintptr_t)patch, relocation->addend, &displacement) ||
                !a64_pc_relative_patch(opcode, instruction, displacement, &patched))
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
            memcpy(patch, &patched, sizeof(patched));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_PREL32)
        {
            s64 displacement = 0;
            if (((u64)(uintptr_t)patch & 3) ||
                !object_address_difference((u64)(uintptr_t)target, (u64)(uintptr_t)patch, relocation->addend, &displacement) ||
                displacement < INT32_MIN || displacement > INT32_MAX)
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
            s32 value = (s32)displacement;
            memcpy(patch, &value, sizeof(value));
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            u64 value = 0;
            if (!object_address_addend((u64)(uintptr_t)target, relocation->addend, &value))
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
            memcpy(patch, &value, sizeof(value));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 || relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12)
        {
            if (!object_apply_aarch64_mach_page_relocation(relocation->kind, patch, (u64)(uintptr_t)patch, (u64)(uintptr_t)target,
                                                           relocation->addend))
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 ||
                 relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 ||
                 relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP ||
                 relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12 ||
                 relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12 ||
                 relocation->kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 ||
                 relocation->kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12 ||
                 relocation->kind == OBJECT_RELOCATION_X86_64_TPOFF32 ||
                 relocation->kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ||
                 relocation->kind == OBJECT_RELOCATION_PE_TLS_OFFSET32 ||
                 relocation->kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32)
        {
            // A standalone executable image has no thread-pointer or Darwin
            // TLVP resolver.  Keep these relocations fail-closed rather than
            // encoding an address that would only work by accident on one
            // host's TLS layout.
            result.error = OBJECT_ERROR_UNSUPPORTED_TARGET;
            break;
        }
        else if (relocation->kind == OBJECT_RELOCATION_COFF_ADDR32NB)
        {
            u64 value = section_offsets[symbol->section] + symbol->value;
            if (relocation->addend < 0)
            {
                u64 magnitude = (u64)(-(relocation->addend + 1)) + 1;
                if (magnitude > value)
                {
                    result.error = OBJECT_ERROR_CAPACITY;
                    break;
                }
                value -= magnitude;
            }
            else
            {
                u64 addend = (u64)relocation->addend;
                if (addend > UINT32_MAX || value > UINT32_MAX - addend)
                {
                    result.error = OBJECT_ERROR_CAPACITY;
                    break;
                }
                value += addend;
            }
            if (value > UINT32_MAX)
            {
                result.error = OBJECT_ERROR_CAPACITY;
                break;
            }
            u32 image_relative = (u32)value;
            memcpy(patch, &image_relative, sizeof(image_relative));
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
