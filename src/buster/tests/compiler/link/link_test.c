#include <buster/tests/compiler/link/link_test.h>

#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/file.h>

BUSTER_GLOBAL_LOCAL String8 link_test_temporary_executable_path(Arena* arena, String8 name, String8 suffix)
{
#if BUSTER_ANDROID || BUSTER_IOS
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(name);
    BUSTER_UNUSED(suffix);
    return (String8){0};
#else
    return buster_test_temporary_path(arena, name, suffix);
#endif
}

BUSTER_GLOBAL_LOCAL ObjectFile link_test_object_make(Arena* arena, Target target, ByteSlice text, ObjectSymbol* symbols, u32 symbol_count,
                                                     ObjectRelocation* relocations, u32 relocation_count)
{
    ObjectSection* sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .kind = (ObjectSectionKind)kind,
            .alignment = object_section_default_alignment((ObjectSectionKind)kind),
        };
    }
    sections[OBJECT_SECTION_TEXT].data = text;
    return (ObjectFile){
        .sections = sections,
        .symbols = symbols,
        .relocations = relocations,
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
        .symbol_count = symbol_count,
        .relocation_count = relocation_count,
    };
}

BUSTER_GLOBAL_LOCAL bool link_test_elf_section_find(ByteSlice image, String8 name, u32* section_index, u64* section_header)
{
    if (image.length < 64 || image.pointer[0] != 0x7f || image.pointer[1] != 'E' || image.pointer[2] != 'L' || image.pointer[3] != 'F')
    {
        return false;
    }
    u64 table_offset = link_read_u64(image.pointer, 40);
    u16 header_size = 0;
    u16 section_count = 0;
    u16 string_index = 0;
    memcpy(&header_size, image.pointer + 58, sizeof(header_size));
    memcpy(&section_count, image.pointer + 60, sizeof(section_count));
    memcpy(&string_index, image.pointer + 62, sizeof(string_index));
    if (!table_offset || header_size < 64 || string_index >= section_count || table_offset > image.length ||
        (u64)section_count > (image.length - table_offset) / header_size)
    {
        return false;
    }
    u64 string_header = table_offset + (u64)string_index * header_size;
    u64 string_offset = link_read_u64(image.pointer, string_header + 24);
    u64 string_size = link_read_u64(image.pointer, string_header + 32);
    if (string_offset > image.length || string_size > image.length - string_offset)
    {
        return false;
    }
    for (u32 index = 1; index < section_count; index += 1)
    {
        u64 header = table_offset + (u64)index * header_size;
        u32 name_offset = link_read_u32(image.pointer, header);
        if (name_offset >= string_size)
        {
            continue;
        }
        u64 length = 0;
        while ((u64)name_offset + length < string_size && image.pointer[string_offset + name_offset + length])
        {
            length += 1;
        }
        if ((u64)name_offset + length < string_size && string_equal(
                                                              (String8){
                                                                  .pointer = (char8*)image.pointer + string_offset + name_offset,
                                                                  .length = length,
                                                              },
                                                              name))
        {
            if (section_index)
            {
                *section_index = index;
            }
            if (section_header)
            {
                *section_header = header;
            }
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool link_test_mach_section_find(ByteSlice image, String8 segment_name, String8 section_name, u64* section_header)
{
    if (image.length < 32 || link_read_u32(image.pointer, 0) != 0xfeedfacf)
    {
        return false;
    }
    u32 command_count = link_read_u32(image.pointer, 16);
    u64 command = 32;
    for (u32 command_index = 0; command_index < command_count; command_index += 1)
    {
        if (command > image.length || 8 > image.length - command)
        {
            return false;
        }
        u32 kind = link_read_u32(image.pointer, command);
        u32 size = link_read_u32(image.pointer, command + 4);
        if (size < 8 || size > image.length - command)
        {
            return false;
        }
        if (kind == 0x19 && size >= 72)
        {
            u32 section_count = link_read_u32(image.pointer, command + 64);
            if ((u64)section_count > (size - 72) / 80)
            {
                return false;
            }
            for (u32 index = 0; index < section_count; index += 1)
            {
                u64 section = command + 72 + (u64)index * 80;
                String8 candidate_section = {.pointer = (char8*)image.pointer + section, .length = 0};
                String8 candidate_segment = {.pointer = (char8*)image.pointer + section + 16, .length = 0};
                while (candidate_section.length < 16 && candidate_section.pointer[candidate_section.length])
                {
                    candidate_section.length += 1;
                }
                while (candidate_segment.length < 16 && candidate_segment.pointer[candidate_segment.length])
                {
                    candidate_segment.length += 1;
                }
                if (string_equal(candidate_segment, segment_name) && string_equal(candidate_section, section_name))
                {
                    if (section_header)
                    {
                        *section_header = section;
                    }
                    return true;
                }
            }
        }
        command += size;
    }
    return false;
}

#if BUSTER_LINUX || BUSTER_WINDOWS || BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL bool link_test_pe_section_find(ByteSlice image, String8 name, u32* virtual_address, u32* raw_offset)
{
    if (image.length < 0x40 || image.pointer[0] != 'M' || image.pointer[1] != 'Z')
    {
        return false;
    }
    u32 pe_offset = link_read_u32(image.pointer, 0x3c);
    if (pe_offset > image.length || image.length - pe_offset < 24 || memcmp(image.pointer + pe_offset, "PE\0\0", 4) != 0)
    {
        return false;
    }
    u16 section_count = 0;
    u16 optional_size = 0;
    memcpy(&section_count, image.pointer + pe_offset + 6, sizeof(section_count));
    memcpy(&optional_size, image.pointer + pe_offset + 20, sizeof(optional_size));
    u64 section_table = (u64)pe_offset + 24 + optional_size;
    if (section_table > image.length || (u64)section_count > (image.length - section_table) / 40)
    {
        return false;
    }
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * 40;
        String8 candidate = {.pointer = (char8*)image.pointer + section, .length = 0};
        while (candidate.length < 8 && candidate.pointer[candidate.length])
        {
            candidate.length += 1;
        }
        if (!string_equal(candidate, name))
        {
            continue;
        }
        if (virtual_address)
        {
            *virtual_address = link_read_u32(image.pointer, section + 12);
        }
        if (raw_offset)
        {
            *raw_offset = link_read_u32(image.pointer, section + 20);
        }
        return true;
    }
    return false;
}
#endif

#if BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL bool link_test_pe_import_matches(ByteSlice executable, String8 library, String8 symbol)
{
    if (executable.length < 0x40 || executable.pointer[0] != 'M' || executable.pointer[1] != 'Z')
    {
        return false;
    }
    u32 pe_offset = link_read_u32(executable.pointer, 0x3c);
    if (pe_offset > executable.length || executable.length - pe_offset < 24 || memcmp(executable.pointer + pe_offset, "PE\0\0", 4) != 0)
    {
        return false;
    }
    u16 section_count = 0;
    u16 optional_size = 0;
    memcpy(&section_count, executable.pointer + pe_offset + 6, sizeof(section_count));
    memcpy(&optional_size, executable.pointer + pe_offset + 20, sizeof(optional_size));
    u64 optional = pe_offset + 24;
    if (optional > executable.length || optional_size > executable.length - optional || optional_size < 128)
    {
        return false;
    }
    u32 import_rva = link_read_u32(executable.pointer, optional + 120);
    u64 section_table = optional + optional_size;
    u64 import_offset = 0;
    bool import_mapped = false;
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * 40;
        if (section > executable.length || 40 > executable.length - section)
        {
            return false;
        }
        u32 virtual_size = link_read_u32(executable.pointer, section + 8);
        u32 virtual_address = link_read_u32(executable.pointer, section + 12);
        u32 raw_size = link_read_u32(executable.pointer, section + 16);
        u32 raw_offset = link_read_u32(executable.pointer, section + 20);
        u64 span = BUSTER_MAX(virtual_size, raw_size);
        if (import_rva >= virtual_address && (u64)import_rva - virtual_address < span)
        {
            import_offset = raw_offset + ((u64)import_rva - virtual_address);
            import_mapped = import_offset < executable.length;
            break;
        }
    }
    if (!import_mapped)
    {
        return false;
    }
    for (u32 descriptor_index = 0; descriptor_index <= (u32)section_count + 2; descriptor_index += 1)
    {
        u64 descriptor = import_offset + (u64)descriptor_index * 20;
        if (descriptor > executable.length || 20 > executable.length - descriptor)
        {
            return false;
        }
        u32 lookup_rva = link_read_u32(executable.pointer, descriptor);
        u32 name_rva = link_read_u32(executable.pointer, descriptor + 12);
        if (!lookup_rva && !name_rva)
        {
            return false;
        }
        u64 name_offset = 0;
        u64 lookup_offset = 0;
        bool name_mapped = false;
        bool lookup_mapped = false;
        for (u16 section_index = 0; section_index < section_count; section_index += 1)
        {
            u64 section = section_table + (u64)section_index * 40;
            u32 virtual_size = link_read_u32(executable.pointer, section + 8);
            u32 virtual_address = link_read_u32(executable.pointer, section + 12);
            u32 raw_size = link_read_u32(executable.pointer, section + 16);
            u32 raw_offset = link_read_u32(executable.pointer, section + 20);
            u64 span = BUSTER_MAX(virtual_size, raw_size);
            if (name_rva >= virtual_address && (u64)name_rva - virtual_address < span)
            {
                name_offset = raw_offset + ((u64)name_rva - virtual_address);
                name_mapped = name_offset < executable.length;
            }
            if (lookup_rva >= virtual_address && (u64)lookup_rva - virtual_address < span)
            {
                lookup_offset = raw_offset + ((u64)lookup_rva - virtual_address);
                lookup_mapped = lookup_offset < executable.length;
            }
        }
        if (!name_mapped || !lookup_mapped)
        {
            return false;
        }
        u64 name_length = 0;
        while (name_offset + name_length < executable.length && executable.pointer[name_offset + name_length])
        {
            name_length += 1;
        }
        if (name_offset + name_length >= executable.length || !string_equal(
                                                                  (String8){
                                                                      .pointer = (char8*)executable.pointer + name_offset,
                                                                      .length = name_length,
                                                                  },
                                                                  library))
        {
            continue;
        }
        for (u32 thunk_index = 0; lookup_offset + (u64)thunk_index * 8 + sizeof(u64) <= executable.length; thunk_index += 1)
        {
            u64 name_entry = link_read_u64(executable.pointer, lookup_offset + (u64)thunk_index * 8);
            if (!name_entry)
            {
                return false;
            }
            if (name_entry >> 63)
            {
                continue;
            }
            u64 symbol_offset = 0;
            bool symbol_mapped = false;
            for (u16 section_index = 0; section_index < section_count; section_index += 1)
            {
                u64 section = section_table + (u64)section_index * 40;
                u32 virtual_size = link_read_u32(executable.pointer, section + 8);
                u32 virtual_address = link_read_u32(executable.pointer, section + 12);
                u32 raw_size = link_read_u32(executable.pointer, section + 16);
                u32 raw_offset = link_read_u32(executable.pointer, section + 20);
                u64 span = BUSTER_MAX(virtual_size, raw_size);
                if (name_entry >= virtual_address && name_entry - virtual_address < span)
                {
                    symbol_offset = raw_offset + (name_entry - virtual_address) + 2;
                    symbol_mapped = symbol_offset < executable.length;
                    break;
                }
            }
            if (!symbol_mapped)
            {
                return false;
            }
            u64 symbol_length = 0;
            while (symbol_offset + symbol_length < executable.length && executable.pointer[symbol_offset + symbol_length])
            {
                symbol_length += 1;
            }
            if (symbol_offset + symbol_length >= executable.length)
            {
                return false;
            }
            if (string_equal(
                    (String8){
                        .pointer = (char8*)executable.pointer + symbol_offset,
                        .length = symbol_length,
                    },
                    symbol))
            {
                return true;
            }
        }
    }
    return false;
}
#endif

BUSTER_GLOBAL_LOCAL void link_test_runtime_stack_walk_skip(UnitTestArguments* arguments, String8 reason)
{
    arguments->show(arguments, S8("SKIP runtime stack walk: {S8}\n"), reason);
}

#if !BUSTER_SANITIZE && !BUSTER_ANDROID && !BUSTER_IOS && (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
BUSTER_GLOBAL_LOCAL String8 link_test_runtime_stack_walk_source(void)
{
#if BUSTER_WINDOWS
    return S8("typedef unsigned long long RuntimeU64;"
              "typedef unsigned int RuntimeU32;"
              "typedef unsigned short RuntimeU16;"
              "extern unsigned short RtlCaptureStackBackTrace(unsigned long skip, unsigned long count, void** buffer, unsigned long* hash);"
              "typedef struct RuntimeContext RuntimeContext;"
              "struct RuntimeContext {"
              "    RuntimeU64 p1_home; RuntimeU64 p2_home; RuntimeU64 p3_home; RuntimeU64 p4_home; RuntimeU64 p5_home; RuntimeU64 p6_home;"
              "    RuntimeU32 context_flags; RuntimeU32 mxcsr;"
              "    RuntimeU16 seg_cs; RuntimeU16 seg_ds; RuntimeU16 seg_es; RuntimeU16 seg_fs; RuntimeU16 seg_gs; RuntimeU16 seg_ss;"
              "    RuntimeU32 eflags;"
              "    RuntimeU64 dr0; RuntimeU64 dr1; RuntimeU64 dr2; RuntimeU64 dr3; RuntimeU64 dr6; RuntimeU64 dr7;"
              "    RuntimeU64 rax; RuntimeU64 rcx; RuntimeU64 rdx; RuntimeU64 rbx; RuntimeU64 rsp; RuntimeU64 rbp; RuntimeU64 rsi; RuntimeU64 rdi;"
              "    RuntimeU64 r8; RuntimeU64 r9; RuntimeU64 r10; RuntimeU64 r11; RuntimeU64 r12; RuntimeU64 r13; RuntimeU64 r14; RuntimeU64 r15; RuntimeU64 rip;"
              "    RuntimeU64 extended[122];"
              "};"
              "typedef struct RuntimeFunction RuntimeFunction;"
              "struct RuntimeFunction { RuntimeU32 begin_address; RuntimeU32 end_address; RuntimeU32 unwind_data; };"
              "extern RuntimeFunction* RtlLookupFunctionEntry(RuntimeU64 control_pc, RuntimeU64* image_base, void* history);"
              "extern void RtlCaptureContext(RuntimeContext* context);"
              "extern void* RtlVirtualUnwind(unsigned long handler_type, RuntimeU64 image_base, RuntimeU64 control_pc, RuntimeFunction* function_entry,"
              "                                 RuntimeContext* context, void** handler_data, RuntimeU64* establisher_frame, void* context_pointers);"
              "extern void* GetStdHandle(int standard_handle);"
              "extern int WriteFile(void* handle, void* buffer, unsigned long byte_count, unsigned long* written, void* overlapped);"
              "int main(void);"
              "static RuntimeU64 runtime_epilog_status;"
              "static RuntimeU64 runtime_epilog_unwind_pc;"
              "static RuntimeU64 runtime_large_epilog_status;"
              "static RuntimeU64 runtime_large_epilog_unwind_pc;"
              "static RuntimeU64 runtime_body_status;"
              "static RuntimeU64 runtime_large_body_status;"
              "static RuntimeU32 runtime_unwind_read_u16(unsigned char* bytes)"
              "{ return (RuntimeU32)bytes[0] | ((RuntimeU32)bytes[1] << 8); }"
              "static RuntimeU64 runtime_unwind_read_u32(unsigned char* bytes)"
              "{ return (RuntimeU64)bytes[0] | ((RuntimeU64)bytes[1] << 8) | ((RuntimeU64)bytes[2] << 16) | ((RuntimeU64)bytes[3] << 24); }"
              "static RuntimeU64 runtime_find_epilog(void* function, int* result_length)"
              "{"
              "    unsigned char* bytes = (unsigned char*)function;"
              "    RuntimeU64 function_base = 0;"
              "    RuntimeFunction* target_entry = RtlLookupFunctionEntry((RuntimeU64)function + 1, &function_base, 0);"
              "    RuntimeU64 function_offset = (RuntimeU64)function - function_base;"
              "    RuntimeU64 function_length = target_entry && (RuntimeU64)target_entry->end_address > function_offset ? (RuntimeU64)target_entry->end_address - function_offset : 0;"
              "    RuntimeU64 result = 0; int found_length = 0;"
              "    for (RuntimeU64 offset = 0; offset + 6 <= function_length; offset += 1)"
              "    {"
              "        int length = 0;"
              "        if (offset + 6 <= function_length && bytes[offset] == 0x48 && bytes[offset + 1] == 0x83 && bytes[offset + 2] == 0xc4 && "
              "            bytes[offset + 4] == 0x5d && bytes[offset + 5] == 0xc3)"
              "        { length = 4; }"
              "        if (offset + 9 <= function_length && bytes[offset] == 0x48 && bytes[offset + 1] == 0x81 && bytes[offset + 2] == 0xc4 && "
              "            bytes[offset + 7] == 0x5d && bytes[offset + 8] == 0xc3)"
              "        { length = 7; }"
              "        if (offset + 6 <= function_length && bytes[offset] == 0x48 && bytes[offset + 1] == 0x8d && bytes[offset + 2] == 0x65 && "
              "            bytes[offset + 3] == 0x00 && bytes[offset + 4] == 0x5d && bytes[offset + 5] == 0xc3)"
              "        { length = 4; }"
              "        if (offset + 9 <= function_length && bytes[offset] == 0x48 && bytes[offset + 1] == 0x8d && bytes[offset + 2] == 0xa5 && "
              "            bytes[offset + 7] == 0x5d && bytes[offset + 8] == 0xc3)"
              "        { length = 7; }"
              "        if (length)"
              "        {"
              "            RuntimeU64 candidate_base = 0;"
              "            RuntimeFunction* candidate_entry = RtlLookupFunctionEntry((RuntimeU64)(bytes + offset) + 1, &candidate_base, 0);"
              "            if (candidate_entry == target_entry && candidate_base == function_base)"
              "            { result = (RuntimeU64)(bytes + offset); found_length = length; }"
              "        }"
              "    }"
              "    *result_length = found_length; return result;"
              "}"
              "static int runtime_unwind_body(void* function, void* expected_main, RuntimeContext* context)"
              "{"
              "    RuntimeU64 image_base = 0;"
              "    RuntimeFunction* function_entry = RtlLookupFunctionEntry((RuntimeU64)function + 1, &image_base, 0);"
              "    if (!function_entry || !image_base) { return 0; }"
              "    unsigned char* unwind = (unsigned char*)(image_base + function_entry->unwind_data);"
              "    RuntimeU32 unwind_header = unwind[0]; RuntimeU32 prolog_size = unwind[1]; RuntimeU32 code_count = unwind[2];"
              "    RuntimeU32 frame_header = unwind[3]; RuntimeU32 fixed_size = 0;"
              "    RuntimeU32 push_count = 0; RuntimeU32 allocate_count = 0; RuntimeU32 frame_count = 0;"
              "    if ((unwind_header & 7) != 1 || (unwind_header >> 3) != 0 || (frame_header & 0xf) != 5 || (frame_header >> 4) != 0 || code_count > 64) { return 0; }"
              "    for (RuntimeU32 slot = 0; slot < code_count; slot += 1)"
              "    {"
              "        unsigned char* code = unwind + 4 + slot * 2;"
              "        RuntimeU32 operation = code[1] & 0xf; RuntimeU32 info = code[1] >> 4;"
              "        if (operation == 0)"
              "        {"
              "            if (info != 5) { return 0; }"
              "            push_count += 1;"
              "        }"
              "        else if (operation == 1)"
              "        {"
              "            if (info == 0)"
              "            {"
              "                if (slot + 1 >= code_count) { return 0; }"
              "                RuntimeU32 size = 8 * runtime_unwind_read_u16(unwind + 4 + (slot + 1) * 2);"
              "                if (!size || fixed_size > 0xffffffffU - size) { return 0; }"
              "                fixed_size += size; slot += 1;"
              "            }"
              "            else if (info == 1)"
              "            {"
              "                if (slot + 2 >= code_count) { return 0; }"
              "                RuntimeU64 size = runtime_unwind_read_u32(unwind + 4 + (slot + 1) * 2);"
              "                if (!size || size > 0xffffffffU || fixed_size > 0xffffffffU - (RuntimeU32)size) { return 0; }"
              "                fixed_size += (RuntimeU32)size; slot += 2;"
              "            }"
              "            else { return 0; }"
              "            allocate_count += 1;"
              "        }"
              "        else if (operation == 2)"
              "        {"
              "            RuntimeU32 size = (info + 1) * 8;"
              "            if (fixed_size > 0xffffffffU - size) { return 0; }"
              "            fixed_size += size; allocate_count += 1;"
              "        }"
              "        else if (operation == 3)"
              "        {"
              "            if (info != 0) { return 0; }"
              "            frame_count += 1;"
              "        }"
              "        else { return 0; }"
              "    }"
              "    RuntimeU64 function_offset = (RuntimeU64)function - image_base;"
              "    RuntimeU64 function_begin = image_base + function_entry->begin_address;"
              "    RuntimeU64 function_end = image_base + function_entry->end_address;"
              "    if (!context || function_begin > 0xffffffffffffffffULL - prolog_size) { return 0; }"
              "    RuntimeU64 prolog_end = function_begin + prolog_size;"
              "    RuntimeU64 captured_pc = context->rip; RuntimeU64 captured_base = 0;"
              "    RuntimeFunction* captured_entry = RtlLookupFunctionEntry(captured_pc, &captured_base, 0);"
              "    if (push_count != 1 || allocate_count != 1 || frame_count != 1 || !fixed_size || fixed_size % 8 ||"
              "        function_offset != function_entry->begin_address || function_end <= function_begin || captured_entry != function_entry ||"
              "        captured_base != image_base || captured_pc < prolog_end || captured_pc >= function_end)"
              "    { return 0; }"
              "    RuntimeU64 frame = context->rbp;"
              "    if (frame > 0xffffffffffffffffULL - fixed_size - 16) { return 0; }"
              "    RuntimeU64 saved_address = frame + fixed_size;"
              "    RuntimeU64 saved_rbp = *(RuntimeU64*)(saved_address); RuntimeU64 saved_rip = *(RuntimeU64*)(saved_address + 8);"
              "    RuntimeU64 expected_rsp = saved_address + 16;"
              "    context->rip = captured_pc;"
              "    void* handler_data = 0; RuntimeU64 establisher_frame = 0;"
              "    RtlVirtualUnwind(0, image_base, context->rip, function_entry, context, &handler_data, &establisher_frame, 0);"
              "    return context->rip == saved_rip && context->rip >= (RuntimeU64)expected_main && context->rip < (RuntimeU64)expected_main + 4096 &&"
              "           context->rsp == expected_rsp && context->rbp == saved_rbp && establisher_frame == frame;"
              "}"
              "static int runtime_unwind_epilog(void* function, void* expected_main)"
              "{"
              "    int add_length = 0; RuntimeU64 control_pc = runtime_find_epilog(function, &add_length);"
              "    RuntimeU64 image_base = 0;"
              "    RuntimeFunction* function_entry = control_pc ? RtlLookupFunctionEntry(control_pc + (RuntimeU64)add_length, &image_base, 0) : 0;"
              "    if (!function_entry) { return 0; }"
              "    RuntimeU64 fake_stack[8]; RuntimeU64 context_storage[156];"
              "    for (RuntimeU32 index = 0; index < 156; index += 1) { context_storage[index] = 0; }"
              "    RuntimeContext* context = (RuntimeContext*)(((RuntimeU64)(void*)&context_storage[0] + 15) & ~(RuntimeU64)15);"
              "    fake_stack[0] = 0x1122334455667788ULL; fake_stack[1] = (RuntimeU64)expected_main + 16;"
              "    context->context_flags = 0x0010001f; context->rsp = (RuntimeU64)&fake_stack[0]; context->rip = control_pc + (RuntimeU64)add_length;"
              "    void* handler_data = 0;"
              "    RuntimeU64 establisher_frame = 0;"
              "    RtlVirtualUnwind(0, image_base, context->rip, function_entry, context, &handler_data, &establisher_frame, 0);"
              "    runtime_epilog_unwind_pc = context->rip;"
              "    return context->rip >= (RuntimeU64)expected_main && context->rip < (RuntimeU64)expected_main + 4096;"
              "}"
              "static void runtime_touch_bytes(unsigned char* bytes, int size)"
              "{ bytes[0] ^= 0x5a; bytes[size - 1] ^= 0xa5; }"
              "typedef void* va_list;"
              "int stack_walk_normal(void** buffer, int size, int marker, ...)"
              "{"
              "    int dynamic_size = marker + 157; unsigned char dynamic_padding[dynamic_size];"
              "    va_list arguments; int extra = 0;"
              "    RuntimeU64 body_context_storage[156];"
              "    RuntimeContext* body_context = (RuntimeContext*)(((RuntimeU64)(void*)&body_context_storage[0] + 15) & ~(RuntimeU64)15);"
              "    RuntimeU64 a0 = 1; RuntimeU64 a1 = 2; RuntimeU64 a2 = 3; RuntimeU64 a3 = 4;"
              "    RuntimeU64 a4 = 5; RuntimeU64 a5 = 6; RuntimeU64 a6 = 7; RuntimeU64 a7 = 8;"
              "    RuntimeU64 a8 = 9; RuntimeU64 a9 = 10; RuntimeU64 a10 = 11; RuntimeU64 a11 = 12;"
              "    RuntimeU64 a12 = 13; RuntimeU64 a13 = 14; RuntimeU64 a14 = 15; RuntimeU64 a15 = 16;"
              "    RuntimeU64 pressure = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15;"
              "    __builtin_va_start(arguments, marker); extra = __builtin_va_arg(arguments, int); __builtin_va_end(arguments);"
              "    dynamic_padding[0] = (unsigned char)pressure; dynamic_padding[dynamic_size - 1] = (unsigned char)(pressure >> 8);"
              "    runtime_touch_bytes(dynamic_padding, dynamic_size);"
              "    if (marker != 100 || extra != 23 || pressure == 0) { a0 = pressure; return -1; }"
              "    RtlCaptureContext(body_context);"
              "    runtime_body_status = (RuntimeU64)runtime_unwind_body((void*)stack_walk_normal, (void*)main, body_context);"
              "    runtime_epilog_status = (RuntimeU64)runtime_unwind_epilog((void*)stack_walk_normal, (void*)main);"
              "    return (int)RtlCaptureStackBackTrace(0, (unsigned long)size, buffer, 0);"
              "}"
              "typedef struct RuntimeBig RuntimeBig;"
              "struct RuntimeBig { RuntimeU64 first; RuntimeU64 second; RuntimeU64 third; };"
              "static int runtime_add_one(int value) { return value + 1; }"
              "static int runtime_add_many(int a, int b, int c, int d, int e) { return a + b + c + d + e; }"
              "static void runtime_mutate_register(RuntimeBig value) { value.first += 9; }"
              "static void runtime_mutate_stack(int a, int b, int c, int d, RuntimeBig value)"
              "{ value.first += (RuntimeU64)(a + b + c + d); }"
              "static RuntimeBig runtime_make_big(RuntimeU64 first, RuntimeU64 second, RuntimeU64 third)"
              "{ return (RuntimeBig){first, second, third}; }"
              "static RuntimeBig runtime_transform_big(RuntimeBig value)"
              "{ value.first += 1; return value; }"
              "static RuntimeBig runtime_dynamic_make_big(RuntimeU64 first, RuntimeU64 second, RuntimeU64 third)"
              "{"
              "    int dynamic_size = (int)first + 256; unsigned char dynamic_padding[dynamic_size];"
              "    dynamic_padding[0] = (unsigned char)first; dynamic_padding[dynamic_size - 1] = (unsigned char)third;"
              "    runtime_touch_bytes(dynamic_padding, dynamic_size);"
              "    return (RuntimeBig){first, second, third};"
              "}"
              "static int runtime_copy_semantics(void)"
              "{"
              "    RuntimeBig original = (RuntimeBig){1, 2, 3};"
              "    RuntimeBig made = runtime_make_big(4, 5, 6);"
              "    RuntimeBig transformed = runtime_transform_big(original);"
              "    runtime_mutate_register(original);"
              "    runtime_mutate_stack(1, 2, 3, 4, original);"
              "    return runtime_add_one(0) == 1 && runtime_add_many(1, 2, 3, 4, 5) == 15 &&"
              "           original.first == 1 && original.second == 2 && original.third == 3 &&"
              "           made.first == 4 && made.second == 5 && made.third == 6 && transformed.first == 2;"
              "}"
              "int stack_walk_large(void** buffer, int size, int first, int second, int third, RuntimeBig incoming)"
              "{"
              "    unsigned char padding[40000];"
              "    int dynamic_size = first + 256; unsigned char dynamic_padding[dynamic_size];"
              "    RuntimeU64 body_context_storage[156];"
              "    RuntimeContext* body_context = (RuntimeContext*)(((RuntimeU64)(void*)&body_context_storage[0] + 15) & ~(RuntimeU64)15);"
              "    RuntimeU64 a0 = 17; RuntimeU64 a1 = 18; RuntimeU64 a2 = 19; RuntimeU64 a3 = 20;"
              "    RuntimeU64 a4 = 21; RuntimeU64 a5 = 22; RuntimeU64 a6 = 23; RuntimeU64 a7 = 24;"
              "    RuntimeU64 pressure = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;"
              "    padding[0] = (unsigned char)pressure; padding[39999] = (unsigned char)(pressure >> 8);"
              "    dynamic_padding[0] = (unsigned char)pressure; dynamic_padding[dynamic_size - 1] = (unsigned char)(pressure >> 8);"
              "    runtime_touch_bytes(dynamic_padding, dynamic_size);"
              "    if (first != 1 || second != 2 || third != 3 || incoming.first != 4 || incoming.second != 5 || incoming.third != 6) { return -1; }"
              "    if (runtime_add_many(1, 2, 3, 4, 5) != 15) { return -1; }"
              "    RuntimeBig dynamic_original = (RuntimeBig){1, 2, 3};"
              "    runtime_mutate_stack(1, 2, 3, 4, dynamic_original);"
              "    if (dynamic_original.first != 1 || dynamic_original.second != 2 || dynamic_original.third != 3) { return -1; }"
              "    RuntimeBig dynamic_made = runtime_dynamic_make_big(7, 11, 13);"
              "    if (dynamic_made.first != 7 || dynamic_made.second != 11 || dynamic_made.third != 13) { return -1; }"
              "    if (!runtime_copy_semantics()) { return -1; }"
              "    RtlCaptureContext(body_context);"
              "    runtime_large_body_status = (RuntimeU64)runtime_unwind_body((void*)stack_walk_large, (void*)main, body_context);"
              "    runtime_large_epilog_status = (RuntimeU64)runtime_unwind_epilog((void*)stack_walk_large, (void*)main);"
              "    runtime_large_epilog_unwind_pc = runtime_epilog_unwind_pc;"
              "    return (int)RtlCaptureStackBackTrace(0, (unsigned long)size, buffer, 0);"
              "}"
              "typedef struct RuntimeReport RuntimeReport;"
              "struct RuntimeReport"
              "{"
              "    void* normal_entry; void* large_entry; void* main_entry;"
              "    RuntimeU64 normal_count; RuntimeU64 large_count;"
              "    RuntimeU64 semantic_status;"
              "    RuntimeU64 body_status; RuntimeU64 large_body_status;"
              "    RuntimeU64 epilog_status; RuntimeU64 epilog_unwind_pc;"
              "    RuntimeU64 large_epilog_status; RuntimeU64 large_epilog_unwind_pc;"
              "    void* normal_frames[64]; void* large_frames[64];"
              "};"
              "int main(void)"
              "{"
              "    RuntimeReport report;"
              "    report.normal_entry = (void*)stack_walk_normal; report.large_entry = (void*)stack_walk_large; report.main_entry = (void*)main;"
              "    int normal_count = stack_walk_normal(report.normal_frames, 64, 100, 23);"
              "    int large_count = stack_walk_large(report.large_frames, 64, 1, 2, 3, (RuntimeBig){4, 5, 6});"
              "    report.normal_count = normal_count > 0 ? (RuntimeU64)normal_count : 0;"
              "    report.large_count = large_count > 0 ? (RuntimeU64)large_count : 0;"
              "    report.semantic_status = large_count > 0 ? 0x535441434b434f50ULL : 0;"
              "    report.body_status = runtime_body_status; report.large_body_status = runtime_large_body_status;"
              "    report.epilog_status = runtime_epilog_status; report.epilog_unwind_pc = runtime_epilog_unwind_pc;"
              "    report.large_epilog_status = runtime_large_epilog_status; report.large_epilog_unwind_pc = runtime_large_epilog_unwind_pc;"
              "    unsigned long written = 0;"
              "    WriteFile(GetStdHandle(-11), &report, sizeof(report), &written, 0);"
              "    return normal_count > 0 && large_count > 0 && report.semantic_status != 0 && report.body_status != 0 && report.large_body_status != 0 &&"
              "           report.epilog_status != 0 && report.large_epilog_status != 0 ? 0 : 1;"
              "}");
#elif BUSTER_LINUX || BUSTER_MACOS
    return S8("typedef unsigned long long RuntimeU64;"
              "extern int backtrace(void** buffer, int size);"
              "extern long write(int file_descriptor, void* buffer, unsigned long byte_count);"
              "int stack_walk_normal(void** buffer, int size)"
              "{"
              "    RuntimeU64 a0 = 1; RuntimeU64 a1 = 2; RuntimeU64 a2 = 3; RuntimeU64 a3 = 4;"
              "    RuntimeU64 a4 = 5; RuntimeU64 a5 = 6; RuntimeU64 a6 = 7; RuntimeU64 a7 = 8;"
              "    RuntimeU64 a8 = 9; RuntimeU64 a9 = 10; RuntimeU64 a10 = 11; RuntimeU64 a11 = 12;"
              "    RuntimeU64 a12 = 13; RuntimeU64 a13 = 14; RuntimeU64 a14 = 15; RuntimeU64 a15 = 16;"
              "    RuntimeU64 pressure = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15;"
              "    if (pressure == 0) { a0 = pressure; }"
              "    return backtrace(buffer, size);"
              "}"
              "int stack_walk_large(void** buffer, int size)"
              "{"
              "    unsigned char padding[40000];"
              "    RuntimeU64 a0 = 17; RuntimeU64 a1 = 18; RuntimeU64 a2 = 19; RuntimeU64 a3 = 20;"
              "    RuntimeU64 a4 = 21; RuntimeU64 a5 = 22; RuntimeU64 a6 = 23; RuntimeU64 a7 = 24;"
              "    RuntimeU64 pressure = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;"
              "    padding[0] = (unsigned char)pressure; padding[39999] = (unsigned char)(pressure >> 8);"
              "    return backtrace(buffer, size);"
              "}"
              "typedef struct RuntimeReport RuntimeReport;"
              "struct RuntimeReport"
              "{"
              "    void* normal_entry; void* large_entry; void* main_entry;"
              "    RuntimeU64 normal_count; RuntimeU64 large_count;"
              "    void* normal_frames[64]; void* large_frames[64];"
              "};"
              "int main(void)"
              "{"
              "    RuntimeReport report;"
              "    report.normal_entry = (void*)stack_walk_normal; report.large_entry = (void*)stack_walk_large; report.main_entry = (void*)main;"
              "    int normal_count = stack_walk_normal(report.normal_frames, 64);"
              "    int large_count = stack_walk_large(report.large_frames, 64);"
              "    report.normal_count = normal_count > 0 ? (RuntimeU64)normal_count : 0;"
              "    report.large_count = large_count > 0 ? (RuntimeU64)large_count : 0;"
              "    write(1, &report, sizeof(report));"
              "    return normal_count > 0 && large_count > 0 ? 0 : 1;"
              "}");
#else
    return (String8){0};
#endif
}

typedef struct LinkTestRuntimeReport LinkTestRuntimeReport;
struct LinkTestRuntimeReport
{
    u64 normal_entry;
    u64 large_entry;
    u64 main_entry;
    u64 normal_count;
    u64 large_count;
#if BUSTER_WINDOWS
    u64 semantic_status;
    u64 body_status;
    u64 large_body_status;
    u64 epilog_status;
    u64 epilog_unwind_pc;
    u64 large_epilog_status;
    u64 large_epilog_unwind_pc;
#endif
    u64 normal_frames[64];
    u64 large_frames[64];
};

BUSTER_GLOBAL_LOCAL bool link_test_runtime_symbol_find(ObjectFile* object, String8 name, u64* value, u64* size)
{
    if (!object || !object->symbols)
    {
        return false;
    }
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        ObjectSymbol* symbol = &object->symbols[symbol_index];
        if (symbol->kind == OBJECT_SYMBOL_FUNCTION && symbol->section == OBJECT_SECTION_TEXT && string_equal(symbol->name, name))
        {
            if (value)
            {
                *value = symbol->value;
            }
            if (size)
            {
                *size = symbol->size;
            }
            return true;
        }
    }
    return false;
}

#if BUSTER_WINDOWS
BUSTER_GLOBAL_LOCAL bool link_test_runtime_windows_xdata(ObjectFile* object, bool* has_frame_register, bool* has_large_allocation)
{
    if (!object || !object->sections || !has_frame_register || !has_large_allocation)
    {
        return false;
    }
    *has_frame_register = false;
    *has_large_allocation = false;
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
        if ((record[3] & 15) == 5 && (record[3] >> 4) == 0)
        {
            *has_frame_register = true;
        }
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
                u16 scaled = 0;
                memcpy(&scaled, record + 6 + code_index * 2, sizeof(scaled));
                *has_large_allocation |= (u32)scaled * 8 > 4096;
                code_index += 2;
            }
            else if (operation == 1 && information == 1)
            {
                if (code_index + 2 >= record[2])
                {
                    return false;
                }
                u32 size = 0;
                memcpy(&size, record + 6 + code_index * 2, sizeof(size));
                *has_large_allocation |= size > 4096;
                code_index += 3;
            }
            else
            {
                return false;
            }
        }
    }
    return record_count != 0 && *has_frame_register && *has_large_allocation;
}
#endif

#if BUSTER_LINUX
BUSTER_GLOBAL_LOCAL bool link_test_runtime_has_eh_frame_header(ByteSlice image)
{
    u32 section_index = 0;
    u64 section_header = 0;
    if (!link_test_elf_section_find(image, S8(".eh_frame_hdr"), &section_index, &section_header))
    {
        return false;
    }
    u64 program_header_offset = link_read_u64(image.pointer, 32);
    u16 program_header_size = 0;
    u16 program_header_count = 0;
    memcpy(&program_header_size, image.pointer + 54, sizeof(program_header_size));
    memcpy(&program_header_count, image.pointer + 56, sizeof(program_header_count));
    if (program_header_size < 56 || program_header_offset > image.length ||
        (u64)program_header_count > (image.length - program_header_offset) / program_header_size)
    {
        return false;
    }
    bool found_program_header = false;
    for (u32 index = 0; index < program_header_count; index += 1)
    {
        u64 program_header = program_header_offset + (u64)index * program_header_size;
        if (link_read_u32(image.pointer, program_header) == 0x6474e550 && link_read_u64(image.pointer, program_header + 32) != 0)
        {
            found_program_header = true;
        }
    }
    return found_program_header;
}
#endif

BUSTER_GLOBAL_LOCAL bool link_test_runtime_frame_contains(u64 const* frames, u64 count, u64 begin, u64 size)
{
    if (!frames || !size)
    {
        return false;
    }
    for (u64 index = 0; index < count; index += 1)
    {
        u64 address = frames[index];
        if (address >= begin && address - begin < size)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL UnitTestResult link_test_runtime_stack_walk_variant(UnitTestArguments* arguments, String8 source_path, String8 output_path,
                                                                          bool debug_info)
{
    UnitTestResult result = {0};
    String8 command_line[8] = {0};
    u32 command_count = 0;
    command_line[command_count++] = debug_info ? S8("-g") : S8("-g0");
#if BUSTER_WINDOWS
    command_line[command_count++] = S8("-l");
    command_line[command_count++] = S8("ntdll");
#endif
    command_line[command_count++] = S8("-o");
    command_line[command_count++] = output_path;
    command_line[command_count++] = source_path;
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8){command_line, command_count});
    CompilerDriverResult compiled = compiler_driver_execute_invocation(arguments->arena, invocation);
    if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
    {
        arguments->show(arguments, S8("runtime stack-walk child compilation failed: {S8}\n"), compiled.diagnostic);
    }
    BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
    if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
    {
        return result;
    }
    BUSTER_TEST(arguments, compiled.has_object);
    BUSTER_TEST(arguments, compiled.native_link.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, compiled.native_link.executable.length != 0);
    BUSTER_TEST(arguments, compiled.codegen_statistics.function_count >= 3);
    BUSTER_TEST(arguments, compiled.codegen_statistics.stack_value_bytes != 0);
    BUSTER_TEST(arguments, compiled.codegen_statistics.maximum_stack_frame_bytes > 4096);
    if (!compiled.has_object || compiled.native_link.error != LINK_ERROR_NONE || !compiled.native_link.executable.length)
    {
        return result;
    }

    ObjectSectionKind debug_kinds[] = {
        OBJECT_SECTION_DEBUG_INFO,
        OBJECT_SECTION_DEBUG_ABBREV,
        OBJECT_SECTION_DEBUG_LINE,
        OBJECT_SECTION_DEBUG_STR,
        OBJECT_SECTION_DEBUG_LOC,
        OBJECT_SECTION_DEBUG_RANGES,
        OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
        OBJECT_SECTION_DEBUG_CODEVIEW_TYPES,
    };
    bool has_debug = false;
    for (u32 kind_index = 0; kind_index < BUSTER_ARRAY_LENGTH(debug_kinds); kind_index += 1)
    {
        has_debug |= compiled.object.sections[debug_kinds[kind_index]].data.length != 0;
    }
    BUSTER_TEST(arguments, has_debug == debug_info);

    ByteSlice image = compiled.native_link.executable;
    bool has_unwind = false;
#if BUSTER_LINUX
    u32 unwind_section_index = 0;
    u64 unwind_section_header = 0;
    bool unwind_found = link_test_elf_section_find(image, S8(".eh_frame"), &unwind_section_index, &unwind_section_header);
    bool unwind_header_found = link_test_elf_section_find(image, S8(".eh_frame_hdr"), 0, 0);
    has_unwind = unwind_found && unwind_header_found && link_test_runtime_has_eh_frame_header(image);
#elif BUSTER_MACOS
    has_unwind = link_test_mach_section_find(image, S8("__TEXT"), S8("__eh_frame"), 0);
#elif BUSTER_WINDOWS
    bool pdata_found = link_test_pe_section_find(image, S8(".pdata"), 0, 0);
    bool xdata_found = link_test_pe_section_find(image, S8(".xdata"), 0, 0);
    has_unwind = pdata_found && xdata_found;
    bool has_frame_register = false;
    bool has_large_allocation = false;
    bool xdata_metadata_valid = link_test_runtime_windows_xdata(&compiled.object, &has_frame_register, &has_large_allocation);
    BUSTER_TEST(arguments, xdata_metadata_valid && has_frame_register && has_large_allocation);
#endif
    BUSTER_TEST(arguments, has_unwind);

    u64 normal_value = 0;
    u64 normal_size = 0;
    u64 large_value = 0;
    u64 large_size = 0;
    u64 main_value = 0;
    u64 main_size = 0;
    bool normal_symbol_found = link_test_runtime_symbol_find(&compiled.object, S8("stack_walk_normal"), &normal_value, &normal_size);
    bool large_symbol_found = link_test_runtime_symbol_find(&compiled.object, S8("stack_walk_large"), &large_value, &large_size);
    bool main_symbol_found = link_test_runtime_symbol_find(&compiled.object, S8("main"), &main_value, &main_size);
    BUSTER_TEST(arguments, normal_symbol_found && normal_size != 0);
    BUSTER_TEST(arguments, large_symbol_found && large_size != 0);
    BUSTER_TEST(arguments, main_symbol_found && main_size != 0);
    if (!normal_symbol_found || !normal_size || !large_symbol_found || !large_size || !main_symbol_found || !main_size)
    {
        return result;
    }

    String8 run_arguments[] = {output_path};
    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                (ProcessSpawnOptions){
                                                    .capture = (u64)1 << STANDARD_STREAM_OUTPUT,
                                                    .use_process_environment = true,
                                                });
    BUSTER_TEST(arguments, spawn.handle != 0);
    if (!spawn.handle)
    {
        return result;
    }
    ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
    BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
    ByteSlice output = wait.streams[STANDARD_STREAM_OUTPUT];
    BUSTER_TEST(arguments, output.length == sizeof(LinkTestRuntimeReport));
    if (wait.result != PROCESS_RESULT_SUCCESS || output.length != sizeof(LinkTestRuntimeReport))
    {
        return result;
    }
    LinkTestRuntimeReport report = {0};
    memcpy(&report, output.pointer, sizeof(report));
    BUSTER_TEST(arguments, report.normal_entry != 0 && report.large_entry != 0 && report.main_entry != 0);
    BUSTER_TEST(arguments, report.normal_count != 0 && report.normal_count <= BUSTER_ARRAY_LENGTH(report.normal_frames));
    BUSTER_TEST(arguments, report.large_count != 0 && report.large_count <= BUSTER_ARRAY_LENGTH(report.large_frames));
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, report.semantic_status == UINT64_C(0x535441434b434f50));
    BUSTER_TEST(arguments, report.body_status == 1 && report.large_body_status == 1);
    BUSTER_TEST(arguments, report.epilog_status == 1 && report.epilog_unwind_pc != 0);
    BUSTER_TEST(arguments, report.large_epilog_status == 1 && report.large_epilog_unwind_pc != 0);
#endif
    if (!report.normal_entry || !report.large_entry || !report.main_entry || !report.normal_count || !report.large_count ||
        report.normal_count > BUSTER_ARRAY_LENGTH(report.normal_frames) || report.large_count > BUSTER_ARRAY_LENGTH(report.large_frames))
    {
        return result;
    }

    bool base_ranges_valid = report.normal_entry >= normal_value && report.large_entry >= large_value && report.main_entry >= main_value;
    u64 normal_base = report.normal_entry - normal_value;
    u64 large_base = report.large_entry - large_value;
    u64 main_base = report.main_entry - main_value;
    base_ranges_valid &= normal_base == large_base && normal_base == main_base;
    BUSTER_TEST(arguments, base_ranges_valid);
    if (!base_ranges_valid)
    {
        return result;
    }
    u64 normal_begin = normal_base + normal_value;
    u64 large_begin = normal_base + large_value;
    u64 main_begin = normal_base + main_value;
#if BUSTER_WINDOWS
    bool epilog_unwind_reached_main = report.epilog_unwind_pc >= main_begin && report.epilog_unwind_pc < main_begin + main_size;
    BUSTER_TEST(arguments, epilog_unwind_reached_main);
    bool large_epilog_unwind_reached_main = report.large_epilog_unwind_pc >= main_begin && report.large_epilog_unwind_pc < main_begin + main_size;
    BUSTER_TEST(arguments, large_epilog_unwind_reached_main);
#endif
    bool normal_contains_normal = link_test_runtime_frame_contains(report.normal_frames, report.normal_count, normal_begin, normal_size);
    bool normal_contains_main = link_test_runtime_frame_contains(report.normal_frames, report.normal_count, main_begin, main_size);
    bool large_contains_large = link_test_runtime_frame_contains(report.large_frames, report.large_count, large_begin, large_size);
    bool large_contains_main = link_test_runtime_frame_contains(report.large_frames, report.large_count, main_begin, main_size);
    BUSTER_TEST(arguments, normal_contains_normal && normal_contains_main);
    BUSTER_TEST(arguments, large_contains_large && large_contains_main);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult link_test_runtime_stack_walk(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_SANITIZE
    link_test_runtime_stack_walk_skip(arguments, S8("sanitizer builds do not execute generated native child code"));
#elif BUSTER_ANDROID
    link_test_runtime_stack_walk_skip(arguments, S8("Android runtime execution is device-managed and has no host child-executable gate"));
#elif BUSTER_IOS
    link_test_runtime_stack_walk_skip(arguments, S8("iOS runtime execution is simulator-managed and has no host child-executable gate"));
#elif BUSTER_LINUX && !(BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    link_test_runtime_stack_walk_skip(arguments, S8("Linux runtime stack walking is only enabled for native x86-64 and AArch64"));
#elif BUSTER_MACOS && !(BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    link_test_runtime_stack_walk_skip(arguments, S8("macOS runtime stack walking is only enabled for native x86-64 and AArch64"));
#elif BUSTER_WINDOWS && !(BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    link_test_runtime_stack_walk_skip(arguments, S8("Windows runtime stack walking is only enabled for native x64 and AArch64"));
#elif !BUSTER_SANITIZE && !BUSTER_ANDROID && !BUSTER_IOS && (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    String8 source = link_test_runtime_stack_walk_source();
    String8 source_path = link_test_temporary_executable_path(arguments->arena, S8("buster-runtime-stack-walk"), S8(".c"));
#if BUSTER_WINDOWS
    String8 output_suffix = S8(".exe");
#else
    String8 output_suffix = S8("");
#endif
    BUSTER_TEST(arguments, source.length != 0 && source_path.length != 0);
    if (source.length && source_path.length && file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source)))
    {
        String8 debug_output = link_test_temporary_executable_path(arguments->arena, S8("buster-runtime-stack-walk-debug"), output_suffix);
        String8 no_debug_output = link_test_temporary_executable_path(arguments->arena, S8("buster-runtime-stack-walk-g0"), output_suffix);
        UnitTestResult debug_result = link_test_runtime_stack_walk_variant(arguments, source_path, debug_output, true);
        UnitTestResult no_debug_result = link_test_runtime_stack_walk_variant(arguments, source_path, no_debug_output, false);
        result.succeeded_test_count = debug_result.succeeded_test_count + no_debug_result.succeeded_test_count;
        result.test_count = debug_result.test_count + no_debug_result.test_count;
    }
    else
    {
        link_test_runtime_stack_walk_skip(arguments, S8("could not create the native runtime source artifact"));
        BUSTER_TEST(arguments, false);
    }
#else
    link_test_runtime_stack_walk_skip(arguments, S8("native runtime stack walking is not implemented for this target operating system"));
#endif
    return result;
}

BUSTER_TEST_F_DECL UnitTestResult link_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    static u8 const sha256_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    u8 sha256_result[32] = {0};
    link_sha256(arguments->arena, (u8 const*)"abc", 3, sha256_result);
    BUSTER_TEST(arguments, memcmp(sha256_result, sha256_abc, sizeof(sha256_abc)) == 0);
    {
        ObjectSectionKind symbol_sections[] = {
            OBJECT_SECTION_TEXT,
            OBJECT_SECTION_READ_ONLY_DATA,
            OBJECT_SECTION_DATA,
            OBJECT_SECTION_ZERO,
            OBJECT_SECTION_THREAD_LOCAL_DATA,
            OBJECT_SECTION_THREAD_LOCAL_ZERO,
        };
        u32 expected_output_sections[] = {0, 1, 2, 3, 4, 4};
        u64 expected_section_offsets[] = {0x40, 0x10, 0x20, 0x30, 0x50, 0x90};
        u8 codeview_bytes[BUSTER_ARRAY_LENGTH(symbol_sections) * 8] = {0};
        ObjectSection sections[OBJECT_SECTION_COUNT] = {0};
        sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(codeview_bytes);
        ObjectSymbol symbols[BUSTER_ARRAY_LENGTH(symbol_sections)] = {0};
        ObjectRelocation relocations[BUSTER_ARRAY_LENGTH(symbol_sections) * 2] = {0};
        u32 object_output_sections[OBJECT_SECTION_COUNT];
        u64 object_section_offsets[OBJECT_SECTION_COUNT] = {0};
        memset(object_output_sections, 0xff, sizeof(object_output_sections));
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(symbol_sections); index += 1)
        {
            ObjectSectionKind section = symbol_sections[index];
            symbols[index] = (ObjectSymbol){
                .value = 0x100 + (u64)index * 0x10,
                .section = (u32)section,
                .kind = OBJECT_SYMBOL_DATA,
            };
            relocations[index * 2] = (ObjectRelocation){
                .addend = (s64)index - 2,
                .offset = (u64)index * 8,
                .section = OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
                .symbol = index,
                .kind = OBJECT_RELOCATION_COFF_SECREL32,
            };
            relocations[index * 2 + 1] = (ObjectRelocation){
                .offset = (u64)index * 8 + 4,
                .section = OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
                .symbol = index,
                .kind = OBJECT_RELOCATION_COFF_SECTION16,
            };
            object_output_sections[section] = expected_output_sections[index];
            object_section_offsets[section] = expected_section_offsets[index];
        }
        ObjectDebugModule debug_module = {.symbols_size = sizeof(codeview_bytes)};
        ObjectFile object = {
            .sections = sections,
            .symbols = symbols,
            .relocations = relocations,
            .section_count = OBJECT_SECTION_COUNT,
            .symbol_count = BUSTER_ARRAY_LENGTH(symbols),
            .relocation_count = BUSTER_ARRAY_LENGTH(relocations),
        };
        ByteSlice resolved = link_pe_resolved_codeview(arguments->arena, &object, &debug_module, object_output_sections,
                                                       object_section_offsets, 5);
        BUSTER_TEST(arguments, resolved.length == sizeof(codeview_bytes));
        if (resolved.length == sizeof(codeview_bytes))
        {
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(symbol_sections); index += 1)
            {
                u32 section_offset = 0;
                u16 section_number = 0;
                memcpy(&section_offset, resolved.pointer + (u64)index * 8, sizeof(section_offset));
                memcpy(&section_number, resolved.pointer + (u64)index * 8 + 4, sizeof(section_number));
                s64 addend = (s64)index - 2;
                u32 expected_section_offset = (u32)((s64)symbols[index].value + (s64)expected_section_offsets[index] + addend);
                BUSTER_TEST(arguments, section_offset == expected_section_offset);
                BUSTER_TEST(arguments, section_number == expected_output_sections[index] + 1);
            }
        }
        object_output_sections[OBJECT_SECTION_DATA] = UINT32_MAX;
        BUSTER_TEST(arguments,
                    link_pe_resolved_codeview(arguments->arena, &object, &debug_module, object_output_sections, object_section_offsets, 5).length == 0);
    }
    Target target = target_native;
#if BUSTER_CPU_ARCH_X86_64
    u8 answer_text[] = {
        0xb8, 42, 0, 0, 0, 0xc3,
    };
    u8 main_text[] = {
        0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0xc3,
    };
    ByteSlice answer_bytes = (ByteSlice)BUSTER_ARRAY_TO_SLICE(answer_text);
    ByteSlice main_bytes = (ByteSlice)BUSTER_ARRAY_TO_SLICE(main_text);
    ObjectRelocation main_relocation = {
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
#elif BUSTER_CPU_ARCH_AARCH64
    u32 answer_instructions[] = {
        0x52800540,
        0xd65f03c0,
    };
    u32 main_instructions[] = {
        0xa9bf7bfd, 0x910003fd, 0x94000000, 0x5100a800, 0xa8c17bfd, 0xd65f03c0,
    };
    ByteSlice answer_bytes = {
        .pointer = (u8*)answer_instructions,
        .length = sizeof(answer_instructions),
    };
    ByteSlice main_bytes = {
        .pointer = (u8*)main_instructions,
        .length = sizeof(main_instructions),
    };
    ObjectRelocation main_relocation = {
        .offset = 8,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
#endif
#if BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64
    ObjectSymbol answer_symbols[] = {
        {
            .name = S8("answer"),
            .size = answer_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectSymbol main_symbols[] = {
        {
            .name = S8("main"),
            .size = main_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("answer"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectFile objects[] = {
        link_test_object_make(arguments->arena, target, answer_bytes, answer_symbols, BUSTER_ARRAY_LENGTH(answer_symbols), 0, 0),
        link_test_object_make(arguments->arena, target, main_bytes, main_symbols, BUSTER_ARRAY_LENGTH(main_symbols), &main_relocation, 1),
    };
#if BUSTER_LINUX
    CodegenUnwindAction main_unwind_actions[4] = {0};
    u32 main_unwind_action_count = 0;
#if BUSTER_CPU_ARCH_AARCH64
    main_unwind_actions[0] = (CodegenUnwindAction){.code_offset = 4, .value = 16, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK};
    main_unwind_actions[1] = (CodegenUnwindAction){.code_offset = 4, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 29};
    main_unwind_actions[2] = (CodegenUnwindAction){.code_offset = 4, .value = 8, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 30};
    main_unwind_actions[3] = (CodegenUnwindAction){.code_offset = 8, .kind = CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, .register_index = 29};
    main_unwind_action_count = BUSTER_ARRAY_LENGTH(main_unwind_actions);
#endif
    CodegenFunctionDescriptor answer_descriptor = {
        .code_size = (u32)answer_bytes.length,
    };
    CodegenFunctionDescriptor main_descriptor = {
        .unwind_actions = main_unwind_actions,
        .code_size = (u32)main_bytes.length,
        .prolog_size = main_unwind_action_count ? 8 : 0,
        .unwind_action_count = main_unwind_action_count,
    };
    DwarfCfiResult answer_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                      .functions = &answer_descriptor,
                                                                      .target = target,
                                                                      .function_count = 1,
                                                                  });
    DwarfCfiResult main_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                    .functions = &main_descriptor,
                                                                    .target = target,
                                                                    .function_count = 1,
                                                                });
    BUSTER_TEST(arguments, answer_cfi.valid && answer_cfi.relocation_count == 1);
    BUSTER_TEST(arguments, main_cfi.valid && main_cfi.relocation_count == 1);
    ObjectRelocation answer_relocation = {
        .offset = answer_cfi.relocations[0].offset,
        .section = OBJECT_SECTION_UNWIND,
        .symbol = 0,
        .kind = target.cpu_arch == CPU_ARCH_X86_64 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_PREL32,
    };
    ObjectRelocation main_relocations[] = {
        main_relocation,
        {
            .offset = main_cfi.relocations[0].offset,
            .section = OBJECT_SECTION_UNWIND,
            .symbol = 0,
            .kind = target.cpu_arch == CPU_ARCH_X86_64 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_PREL32,
        },
    };
    ObjectFile cfi_objects[] = {
        link_test_object_make(arguments->arena, target, answer_bytes, answer_symbols, BUSTER_ARRAY_LENGTH(answer_symbols), &answer_relocation, 1),
        link_test_object_make(arguments->arena, target, main_bytes, main_symbols, BUSTER_ARRAY_LENGTH(main_symbols), main_relocations,
                              BUSTER_ARRAY_LENGTH(main_relocations)),
    };
    cfi_objects[0].sections[OBJECT_SECTION_UNWIND].data = answer_cfi.bytes;
    cfi_objects[1].sections[OBJECT_SECTION_UNWIND].data = main_cfi.bytes;
    LinkObjectResult cfi_linked = link_objects(arguments->arena, cfi_objects, BUSTER_ARRAY_LENGTH(cfi_objects), (LinkOptions){0});
    BUSTER_TEST(arguments, cfi_linked.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, cfi_linked.object.relocation_count == 3);
    BUSTER_TEST(arguments, cfi_linked.object.sections[OBJECT_SECTION_UNWIND].data.length == answer_cfi.bytes.length + main_cfi.bytes.length);

    Target a64_elf_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    u32 a64_return = 0xd65f03c0;
    ObjectSymbol a64_main_symbol = {
        .name = S8("main"),
        .size = sizeof(a64_return),
        .section = OBJECT_SECTION_TEXT,
        .kind = OBJECT_SYMBOL_FUNCTION,
        .global = true,
    };
    CodegenFunctionDescriptor a64_descriptor = {
        .code_size = sizeof(a64_return),
    };
    DwarfCfiResult a64_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                    .functions = &a64_descriptor,
                                                                    .target = a64_elf_target,
                                                                    .function_count = 1,
                                                                });
    BUSTER_TEST(arguments, a64_cfi.valid && a64_cfi.relocation_count == 1);
    ObjectRelocation a64_cfi_relocation = {
        .offset = a64_cfi.relocations[0].offset,
        .section = OBJECT_SECTION_UNWIND,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_AARCH64_PREL32,
    };
    ObjectFile a64_elf_object = link_test_object_make(arguments->arena, a64_elf_target,
                                                      (ByteSlice){
                                                          .pointer = (u8*)&a64_return,
                                                          .length = sizeof(a64_return),
                                                      },
                                                      &a64_main_symbol, 1, &a64_cfi_relocation, 1);
    a64_elf_object.sections[OBJECT_SECTION_UNWIND].data = a64_cfi.bytes;
    NativeExecutableLinkResult a64_elf_executable = link_native_executable(arguments->arena, &a64_elf_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, a64_elf_executable.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, a64_elf_executable.executable.length >= 64);
    if (a64_elf_executable.error == LINK_ERROR_NONE)
    {
        u32 a64_header_index = 0;
        u64 a64_header = 0;
        BUSTER_TEST(arguments, a64_elf_executable.executable.pointer[18] == 183 && a64_elf_executable.executable.pointer[19] == 0);
        BUSTER_TEST(arguments, link_test_elf_section_find(a64_elf_executable.executable, S8(".eh_frame_hdr"), &a64_header_index, &a64_header));
    }

    Target a64_pe_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_WINDOWS,
    };
    u32 a64_pe_code[] = {
        0xa9bf7bfd,
        0x910003fd,
        0xa8c17bfd,
        0xd65f03c0,
    };
    u8 a64_pe_pdata[8] = {0};
    u8 a64_pe_xdata[] = {
        0x04, 0x00, 0x40, 0x08, 0x02, 0x00, 0x40, 0x00, 0xe1, 0x81, 0xe4, 0x00,
    };
    ObjectSymbol a64_pe_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(a64_pe_code),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8(".La64_xdata"),
            .size = sizeof(a64_pe_xdata),
            .section = OBJECT_SECTION_WINDOWS_XDATA,
            .kind = OBJECT_SYMBOL_DATA,
        },
    };
    ObjectRelocation a64_pe_relocations[] = {
        {
            .section = OBJECT_SECTION_WINDOWS_PDATA,
            .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
        },
        {
            .offset = 4,
            .section = OBJECT_SECTION_WINDOWS_PDATA,
            .symbol = 1,
            .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
        },
    };
    ObjectFile a64_pe_object = link_test_object_make(arguments->arena, a64_pe_target,
                                                     (ByteSlice){
                                                         .pointer = (u8*)a64_pe_code,
                                                         .length = sizeof(a64_pe_code),
                                                     },
                                                     a64_pe_symbols, BUSTER_ARRAY_LENGTH(a64_pe_symbols), a64_pe_relocations,
                                                     BUSTER_ARRAY_LENGTH(a64_pe_relocations));
    a64_pe_object.sections[OBJECT_SECTION_WINDOWS_PDATA].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(a64_pe_pdata);
    a64_pe_object.sections[OBJECT_SECTION_WINDOWS_PDATA].virtual_size = sizeof(a64_pe_pdata);
    a64_pe_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(a64_pe_xdata);
    a64_pe_object.sections[OBJECT_SECTION_WINDOWS_XDATA].virtual_size = sizeof(a64_pe_xdata);
    NativeExecutableLinkResult a64_pe_executable = link_native_executable(arguments->arena, &a64_pe_object,
                                                                          (NativeExecutableLinkOptions){
                                                                              .entry_symbol = S8("main"),
                                                                          });
    BUSTER_TEST(arguments, a64_pe_executable.error == LINK_ERROR_NONE);
    u32 a64_pdata_rva = 0;
    u32 a64_pdata_raw = 0;
    u32 a64_xdata_rva = 0;
    BUSTER_TEST(arguments, link_test_pe_section_find(a64_pe_executable.executable, S8(".pdata"), &a64_pdata_rva, &a64_pdata_raw));
    BUSTER_TEST(arguments, link_test_pe_section_find(a64_pe_executable.executable, S8(".xdata"), &a64_xdata_rva, 0));
    BUSTER_TEST(arguments, a64_pe_executable.executable.length > 0x128 && link_read_u32(a64_pe_executable.executable.pointer, 0x120) == a64_pdata_rva &&
                               link_read_u32(a64_pe_executable.executable.pointer, 0x124) == 16);
    if (a64_pdata_raw <= a64_pe_executable.executable.length && 16 <= a64_pe_executable.executable.length - a64_pdata_raw)
    {
        BUSTER_TEST(arguments, link_read_u32(a64_pe_executable.executable.pointer, a64_pdata_raw + 4) == a64_xdata_rva);
        BUSTER_TEST(arguments, link_read_u32(a64_pe_executable.executable.pointer, a64_pdata_raw + 12) == a64_xdata_rva + 8);
    }
#endif
    LinkObjectResult linked = link_objects(arguments->arena, objects, BUSTER_ARRAY_LENGTH(objects), (LinkOptions){0});
    BUSTER_TEST(arguments, linked.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, linked.object.symbol_count == 2);
    BUSTER_TEST(arguments, linked.object.relocation_count == 1);
    bool linked_text_size_matches = false;
    if (linked.object.sections && linked.object.section_count > OBJECT_SECTION_TEXT)
    {
        linked_text_size_matches = linked.object.sections[OBJECT_SECTION_TEXT].data.length == align_forward(answer_bytes.length, 16) + main_bytes.length;
    }
    BUSTER_TEST(arguments, linked_text_size_matches);
    ObjectFile duplicate_objects[] = {
        objects[0],
        objects[0],
    };
    LinkObjectResult duplicate = link_objects(arguments->arena, duplicate_objects, BUSTER_ARRAY_LENGTH(duplicate_objects), (LinkOptions){0});
    BUSTER_TEST(arguments, duplicate.error == LINK_ERROR_DUPLICATE_SYMBOL);
    BUSTER_STRING_TEST(arguments, duplicate.symbol, S8("answer"));
    ObjectFile unresolved_object = objects[1];
    LinkObjectResult unresolved = link_objects(arguments->arena, &unresolved_object, 1, (LinkOptions){0});
    BUSTER_TEST(arguments, unresolved.error == LINK_ERROR_UNRESOLVED_SYMBOL);
    BUSTER_STRING_TEST(arguments, unresolved.symbol, S8("answer"));
    LinkObjectResult permitted = link_objects(arguments->arena, &unresolved_object, 1,
                                              (LinkOptions){
                                                  .allow_undefined_symbols = true,
                                              });
    BUSTER_TEST(arguments, permitted.error == LINK_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
    ObjectFile windows_object = linked.object;
    windows_object.target.os = OPERATING_SYSTEM_WINDOWS;
    windows_object.sections = arena_allocate(arguments->arena, ObjectSection, linked.object.section_count);
    memcpy(windows_object.sections, linked.object.sections, (u64)linked.object.section_count * sizeof(*windows_object.sections));
    u8 pe_unwind_pdata[12] = {0};
    u8 pe_unwind_xdata[] = {1, 0, 0, 0};
    windows_object.sections[OBJECT_SECTION_WINDOWS_PDATA].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(pe_unwind_pdata);
    windows_object.sections[OBJECT_SECTION_WINDOWS_PDATA].virtual_size = sizeof(pe_unwind_pdata);
    windows_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(pe_unwind_xdata);
    windows_object.sections[OBJECT_SECTION_WINDOWS_XDATA].virtual_size = sizeof(pe_unwind_xdata);
    windows_object.symbols = arena_allocate(arguments->arena, ObjectSymbol, linked.object.symbol_count + 1);
    memcpy(windows_object.symbols, linked.object.symbols, (u64)linked.object.symbol_count * sizeof(*windows_object.symbols));
    u32 pe_main_symbol = UINT32_MAX;
    for (u32 symbol_index = 0; symbol_index < linked.object.symbol_count; symbol_index += 1)
    {
        if (string_equal(windows_object.symbols[symbol_index].name, S8("main")))
        {
            pe_main_symbol = symbol_index;
            break;
        }
    }
    u32 pe_xdata_symbol = windows_object.symbol_count++;
    windows_object.symbols[pe_xdata_symbol] = (ObjectSymbol){
        .name = S8(".Lpe_xdata"),
        .size = sizeof(pe_unwind_xdata),
        .section = OBJECT_SECTION_WINDOWS_XDATA,
        .kind = OBJECT_SYMBOL_DATA,
    };
    windows_object.relocations = arena_allocate(arguments->arena, ObjectRelocation, linked.object.relocation_count + 3);
    memcpy(windows_object.relocations, linked.object.relocations, (u64)linked.object.relocation_count * sizeof(*windows_object.relocations));
    windows_object.relocation_count = linked.object.relocation_count;
    if (pe_main_symbol != UINT32_MAX)
    {
        windows_object.relocations[windows_object.relocation_count++] = (ObjectRelocation){
            .section = OBJECT_SECTION_WINDOWS_PDATA,
            .symbol = pe_main_symbol,
            .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
        };
        windows_object.relocations[windows_object.relocation_count++] = (ObjectRelocation){
            .addend = (s64)windows_object.symbols[pe_main_symbol].size,
            .offset = 4,
            .section = OBJECT_SECTION_WINDOWS_PDATA,
            .symbol = pe_main_symbol,
            .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
        };
        windows_object.relocations[windows_object.relocation_count++] = (ObjectRelocation){
            .offset = 8,
            .section = OBJECT_SECTION_WINDOWS_PDATA,
            .symbol = pe_xdata_symbol,
            .kind = OBJECT_RELOCATION_COFF_ADDR32NB,
        };
    }
    BUSTER_TEST(arguments, pe_main_symbol != UINT32_MAX);
    String8 pe_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-link-test"), S8(".exe"));
    NativeExecutableLinkResult pe_executable = link_native_executable(arguments->arena, &windows_object,
                                                                      (NativeExecutableLinkOptions){
                                                                          .output_path = pe_output_path,
                                                                          .entry_symbol = S8("main"),
                                                                      });
    BUSTER_TEST(arguments, pe_executable.error == LINK_ERROR_NONE);
    bool pe_header_valid = pe_executable.executable.length > 0x84 && pe_executable.executable.pointer[0] == 'M' && pe_executable.executable.pointer[1] == 'Z' &&
                           memcmp(pe_executable.executable.pointer + 0x80, "PE\0\0", 4) == 0 && (pe_executable.executable.pointer[0x96] & 0x01) != 0;
    BUSTER_TEST(arguments, pe_header_valid);
    BUSTER_TEST(arguments, pe_executable.executable.length > 0xe8 && link_read_u64(pe_executable.executable.pointer, 0xe0) == LINK_TEST_PE_STACK_RESERVE);
    u32 pe_pdata_rva = 0;
    u32 pe_pdata_raw = 0;
    u32 pe_xdata_rva = 0;
    BUSTER_TEST(arguments, link_test_pe_section_find(pe_executable.executable, S8(".pdata"), &pe_pdata_rva, &pe_pdata_raw));
    BUSTER_TEST(arguments, link_test_pe_section_find(pe_executable.executable, S8(".xdata"), &pe_xdata_rva, 0));
    BUSTER_TEST(arguments, pe_executable.executable.length > 0x128 && link_read_u32(pe_executable.executable.pointer, 0x120) == pe_pdata_rva &&
                               link_read_u32(pe_executable.executable.pointer, 0x124) == sizeof(pe_unwind_pdata) + 12);
    BUSTER_TEST(arguments, pe_pdata_raw <= pe_executable.executable.length && sizeof(pe_unwind_pdata) + 12 <= pe_executable.executable.length - pe_pdata_raw);
    if (pe_pdata_raw <= pe_executable.executable.length && sizeof(pe_unwind_pdata) + 12 <= pe_executable.executable.length - pe_pdata_raw &&
        pe_main_symbol != UINT32_MAX)
    {
        u32 startup_rva = link_read_u32(pe_executable.executable.pointer, pe_pdata_raw);
        u32 startup_end_rva = link_read_u32(pe_executable.executable.pointer, pe_pdata_raw + 4);
        u32 startup_unwind_rva = link_read_u32(pe_executable.executable.pointer, pe_pdata_raw + 8);
        u32 function_rva = link_read_u32(pe_executable.executable.pointer, pe_pdata_raw + 12);
        u32 function_end_rva = link_read_u32(pe_executable.executable.pointer, pe_pdata_raw + 16);
        u32 unwind_rva = link_read_u32(pe_executable.executable.pointer, pe_pdata_raw + 20);
        BUSTER_TEST(arguments, startup_end_rva > startup_rva && startup_unwind_rva == pe_xdata_rva);
        BUSTER_TEST(arguments, function_end_rva - function_rva == windows_object.symbols[pe_main_symbol].size);
        BUSTER_TEST(arguments, unwind_rva == pe_xdata_rva + 8);
    }
    u8 const pe_argv_mode_prefix[] = {
        0x48, 0x83, 0xec, 0x38, 0x31, 0xc9, 0xff, 0xc1,
    };
    bool pe_argv_mode_valid = pe_executable.executable.length >= 0x400 + sizeof(pe_argv_mode_prefix) &&
                              memcmp(pe_executable.executable.pointer + 0x400, pe_argv_mode_prefix, sizeof(pe_argv_mode_prefix)) == 0;
    BUSTER_TEST(arguments, pe_argv_mode_valid);
    u8 pe_libc_main_text[] = {
        0x48, 0x83, 0xec, 0x28, 0xb9, 0xd6, 0xff, 0xff, 0xff, 0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0x48, 0x83, 0xc4, 0x28, 0xc3,
    };
    ObjectSymbol pe_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(pe_libc_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation pe_libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile pe_libc_object = link_test_object_make(arguments->arena, windows_object.target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(pe_libc_main_text),
                                                      pe_libc_symbols, BUSTER_ARRAY_LENGTH(pe_libc_symbols), &pe_libc_relocation, 1);
    String8 pe_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-libc-link-test"), S8(".exe"));
    NativeExecutableLinkResult pe_libc_executable = link_native_executable(arguments->arena, &pe_libc_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .output_path = pe_libc_output_path,
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, pe_libc_executable.error == LINK_ERROR_NONE);
    String8 external_exports[] = {
        S8("external_value"),
    };
    NativeDynamicLibrary external_library = {
        .name = S8("external.dll"),
        .exported_symbols = external_exports,
        .exported_symbol_count = BUSTER_ARRAY_LENGTH(external_exports),
    };
    pe_libc_symbols[1].name = S8("external_value");
    NativeExecutableLinkResult pe_external_executable = link_native_executable(arguments->arena, &pe_libc_object,
                                                                               (NativeExecutableLinkOptions){
                                                                                   .entry_symbol = S8("main"),
                                                                                   .dynamic_libraries = &external_library,
                                                                                   .dynamic_library_count = 1,
                                                                               });
    BUSTER_TEST(arguments, pe_external_executable.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, link_test_pe_import_matches(pe_external_executable.executable, S8("external.dll"), S8("external_value")));
    BUSTER_TEST(arguments, !link_test_pe_import_matches(pe_external_executable.executable, S8("ucrtbase.dll"), S8("external_value")));
    u8 pe_data_main_text[] = {
        0x48, 0x8d, 0x05, 0, 0, 0, 0, 0xc3,
    };
    ObjectSymbol pe_data_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(pe_data_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("external_value"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    ObjectRelocation pe_data_relocation = {
        .addend = -4,
        .offset = 3,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile pe_data_object = link_test_object_make(arguments->arena, windows_object.target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(pe_data_main_text),
                                                       pe_data_symbols, BUSTER_ARRAY_LENGTH(pe_data_symbols), &pe_data_relocation, 1);
    NativeExecutableLinkResult pe_data_executable = link_native_executable(arguments->arena, &pe_data_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .entry_symbol = S8("main"),
                                                                                .dynamic_libraries = &external_library,
                                                                                .dynamic_library_count = 1,
                                                                            });
    BUSTER_TEST(arguments, pe_data_executable.error == LINK_ERROR_NONE);
    u64 pe_data_text_offset = 0x400 + align_forward(54, 16);
    BUSTER_TEST(arguments, pe_data_executable.executable.length > pe_data_text_offset + 2 && pe_data_executable.executable.pointer[pe_data_text_offset] == 0x48 &&
                           pe_data_executable.executable.pointer[pe_data_text_offset + 1] == 0x8b);
    BUSTER_TEST(arguments, link_test_pe_import_matches(pe_data_executable.executable, S8("external.dll"), S8("external_value")));
    pe_libc_symbols[1].name = S8("missing_value");
    NativeExecutableLinkResult pe_missing_external = link_native_executable(arguments->arena, &pe_libc_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .entry_symbol = S8("main"),
                                                                                .dynamic_libraries = &external_library,
                                                                                .dynamic_library_count = 1,
                                                                            });
    BUSTER_TEST(arguments, pe_missing_external.error == LINK_ERROR_UNRESOLVED_SYMBOL);
    BUSTER_STRING_TEST(arguments, pe_missing_external.symbol, S8("missing_value"));
#endif
    u32 aarch64_main_instructions[] = {
        0x52800000,
        0xd65f03c0,
    };
    ObjectSymbol aarch64_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_main_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    Target aarch64_target = target;
    aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
    aarch64_target.os = OPERATING_SYSTEM_LINUX;
    ObjectFile aarch64_object = link_test_object_make(arguments->arena, aarch64_target,
                                                      (ByteSlice){
                                                          .pointer = (u8*)aarch64_main_instructions,
                                                          .length = sizeof(aarch64_main_instructions),
                                                      },
                                                      aarch64_symbols, BUSTER_ARRAY_LENGTH(aarch64_symbols), 0, 0);
    String8 aarch64_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-link-test"), S8(""));
    NativeExecutableLinkResult aarch64_executable = link_native_executable(arguments->arena, &aarch64_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .output_path = aarch64_output_path,
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, aarch64_executable.error == LINK_ERROR_NONE);
    bool aarch64_header_valid = aarch64_executable.executable.length > 20 && aarch64_executable.executable.pointer[0] == 0x7f &&
                                aarch64_executable.executable.pointer[1] == 'E' && aarch64_executable.executable.pointer[18] == 183;
    BUSTER_TEST(arguments, aarch64_header_valid);
    u64 aarch64_text_header = 0;
    u64 aarch64_bss_header = 0;
    u64 aarch64_debug_info_header = 0;
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_executable.executable, S8(".text"), 0, &aarch64_text_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_executable.executable, S8(".bss"), 0, &aarch64_bss_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_executable.executable, S8(".debug_info"), 0, &aarch64_debug_info_header));
    BUSTER_TEST(arguments, aarch64_text_header && link_read_u32(aarch64_executable.executable.pointer, aarch64_text_header + 4) == 1);
    BUSTER_TEST(arguments, aarch64_bss_header && link_read_u32(aarch64_executable.executable.pointer, aarch64_bss_header + 4) == 8);
    BUSTER_TEST(arguments, aarch64_debug_info_header && link_read_u64(aarch64_executable.executable.pointer, aarch64_debug_info_header + 32) == 0);
    u32 aarch64_libc_instructions[] = {
        0xa9bf7bfd, 0x910003fd, 0x52800540, 0x94000000, 0x5100a800, 0xa8c17bfd, 0xd65f03c0,
    };
    ObjectSymbol aarch64_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_libc_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation aarch64_libc_relocation = {
        .offset = 3 * sizeof(u32),
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
    ObjectFile aarch64_libc_object = link_test_object_make(arguments->arena, aarch64_target,
                                                           (ByteSlice){
                                                               .pointer = (u8*)aarch64_libc_instructions,
                                                               .length = sizeof(aarch64_libc_instructions),
                                                           },
                                                           aarch64_libc_symbols, BUSTER_ARRAY_LENGTH(aarch64_libc_symbols), &aarch64_libc_relocation, 1);
    String8 aarch64_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-libc-link-test"), S8(""));
    NativeExecutableLinkResult aarch64_libc_executable = link_native_executable(arguments->arena, &aarch64_libc_object,
                                                                                (NativeExecutableLinkOptions){
                                                                                    .output_path = aarch64_libc_output_path,
                                                                                    .entry_symbol = S8("main"),
                                                                                });
    BUSTER_TEST(arguments, aarch64_libc_executable.error == LINK_ERROR_NONE);
    String8 dynamic_section_names[] = {
        S8(".interp"), S8(".plt"), S8(".dynstr"), S8(".dynsym"), S8(".hash"), S8(".rela.plt"), S8(".got.plt"), S8(".dynamic"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(dynamic_section_names); index += 1)
    {
        BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, dynamic_section_names[index], 0, 0));
    }
    u32 dynamic_string_index = 0;
    u32 dynamic_symbol_index = 0;
    u32 got_index = 0;
    u64 dynamic_symbol_header = 0;
    u64 hash_header = 0;
    u64 relocation_header = 0;
    u64 dynamic_header = 0;
    BUSTER_TEST(arguments,
                link_test_elf_section_find(aarch64_libc_executable.executable, S8(".dynstr"), &dynamic_string_index, 0));
    BUSTER_TEST(arguments,
                link_test_elf_section_find(aarch64_libc_executable.executable, S8(".dynsym"), &dynamic_symbol_index, &dynamic_symbol_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".got.plt"), &got_index, 0));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".hash"), 0, &hash_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".rela.plt"), 0, &relocation_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".dynamic"), 0, &dynamic_header));
    BUSTER_TEST(arguments, dynamic_symbol_header && link_read_u32(aarch64_libc_executable.executable.pointer, dynamic_symbol_header + 40) == dynamic_string_index);
    BUSTER_TEST(arguments, hash_header && link_read_u32(aarch64_libc_executable.executable.pointer, hash_header + 40) == dynamic_symbol_index);
    BUSTER_TEST(arguments, relocation_header && link_read_u32(aarch64_libc_executable.executable.pointer, relocation_header + 40) == dynamic_symbol_index &&
                               link_read_u32(aarch64_libc_executable.executable.pointer, relocation_header + 44) == got_index);
    BUSTER_TEST(arguments, dynamic_header && link_read_u32(aarch64_libc_executable.executable.pointer, dynamic_header + 40) == dynamic_string_index);
    ObjectFile aarch64_tls_object = aarch64_libc_object;
    ObjectSection* aarch64_tls_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(aarch64_tls_sections, aarch64_libc_object.sections, sizeof(*aarch64_tls_sections) * OBJECT_SECTION_COUNT);
    aarch64_tls_object.sections = aarch64_tls_sections;
    u32 initialized_thread_local = 42;
    aarch64_tls_sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data = (ByteSlice){
        .pointer = (u8*)&initialized_thread_local,
        .length = sizeof(initialized_thread_local),
    };
    aarch64_tls_sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size = 32;
    NativeExecutableLinkResult aarch64_tls_executable = link_native_executable(arguments->arena, &aarch64_tls_object,
                                                                                (NativeExecutableLinkOptions){
                                                                                    .entry_symbol = S8("main"),
                                                                                });
    BUSTER_TEST(arguments, aarch64_tls_executable.error == LINK_ERROR_NONE);
    u64 thread_local_data_header = 0;
    u64 thread_local_zero_header = 0;
    u64 tls_got_header = 0;
    u64 tls_dynamic_header = 0;
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".tdata"), 0, &thread_local_data_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".tbss"), 0, &thread_local_zero_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".got.plt"), 0, &tls_got_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".dynamic"), 0, &tls_dynamic_header));
    u64 thread_local_data_address = thread_local_data_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_data_header + 16) : 0;
    u64 thread_local_data_size = thread_local_data_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_data_header + 32) : 0;
    u64 thread_local_zero_address = thread_local_zero_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 16) : 0;
    u64 thread_local_zero_size = thread_local_zero_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 32) : 0;
    u64 tls_got_address = tls_got_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_got_header + 16) : 0;
    u64 tls_got_size = tls_got_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_got_header + 32) : 0;
    u64 tls_dynamic_address = tls_dynamic_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_dynamic_header + 16) : 0;
    u64 tls_dynamic_size = tls_dynamic_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_dynamic_header + 32) : 0;
    BUSTER_TEST(arguments, thread_local_data_header && link_read_u32(aarch64_tls_executable.executable.pointer, thread_local_data_header + 4) == 1 &&
                               (link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_data_header + 8) & UINT64_C(0x403)) == UINT64_C(0x403));
    BUSTER_TEST(arguments, thread_local_zero_header && link_read_u32(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 4) == 8 &&
                               (link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 8) & UINT64_C(0x403)) == UINT64_C(0x403));
    BUSTER_TEST(arguments, tls_got_address + tls_got_size <= tls_dynamic_address);
    BUSTER_TEST(arguments, tls_dynamic_address + tls_dynamic_size <= thread_local_data_address);
    BUSTER_TEST(arguments, thread_local_data_address + thread_local_data_size <= thread_local_zero_address);
    BUSTER_TEST(arguments, thread_local_zero_size == 32);
    u32 aarch64_data_main_instructions[] = {
        0x58000049, 0x14000003, 0, 0,
    };
    ObjectSymbol aarch64_data_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_data_main_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("external_value"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    ObjectRelocation aarch64_data_relocation = {
        .offset = 2 * sizeof(u32),
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    ObjectFile aarch64_data_object = link_test_object_make(arguments->arena, (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS},
                                                            (ByteSlice){
                                                                .pointer = (u8*)aarch64_data_main_instructions,
                                                                .length = sizeof(aarch64_data_main_instructions),
                                                            },
                                                            aarch64_data_symbols, BUSTER_ARRAY_LENGTH(aarch64_data_symbols), &aarch64_data_relocation, 1);
    String8 aarch64_data_exports[] = {
        S8("external_value"),
    };
    NativeDynamicLibrary aarch64_data_library = {
        .name = S8("external.dll"),
        .exported_symbols = aarch64_data_exports,
        .exported_symbol_count = BUSTER_ARRAY_LENGTH(aarch64_data_exports),
    };
    NativeExecutableLinkResult aarch64_data_executable = link_native_executable(arguments->arena, &aarch64_data_object,
                                                                                  (NativeExecutableLinkOptions){
                                                                                      .entry_symbol = S8("main"),
                                                                                      .dynamic_libraries = &aarch64_data_library,
                                                                                      .dynamic_library_count = 1,
                                                                                  });
    BUSTER_TEST(arguments, aarch64_data_executable.error == LINK_ERROR_NONE);
    u64 aarch64_data_text_offset = 0x400 + align_forward(20, 16);
    BUSTER_TEST(arguments, aarch64_data_executable.executable.length > aarch64_data_text_offset + 12 &&
                           link_read_u32(aarch64_data_executable.executable.pointer, aarch64_data_text_offset + 8) == UINT32_C(0x14000002));
    ObjectFile aarch64_pe_object = aarch64_object;
    aarch64_pe_object.target.os = OPERATING_SYSTEM_WINDOWS;
    String8 aarch64_pe_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-pe-test"), S8(".exe"));
    NativeExecutableLinkResult aarch64_pe_executable = link_native_executable(arguments->arena, &aarch64_pe_object,
                                                                              (NativeExecutableLinkOptions){
                                                                                  .output_path = aarch64_pe_output_path,
                                                                                  .entry_symbol = S8("main"),
                                                                              });
    BUSTER_TEST(arguments, aarch64_pe_executable.error == LINK_ERROR_NONE);
    bool aarch64_pe_header_valid = aarch64_pe_executable.executable.length > 0x88 && aarch64_pe_executable.executable.pointer[0x84] == 0x64 &&
                                   aarch64_pe_executable.executable.pointer[0x85] == 0xaa;
    BUSTER_TEST(arguments, aarch64_pe_header_valid);
    ObjectFile aarch64_pe_libc_object = aarch64_libc_object;
    aarch64_pe_libc_object.target.os = OPERATING_SYSTEM_WINDOWS;
    NativeExecutableLinkResult aarch64_pe_libc_executable = link_native_executable(arguments->arena, &aarch64_pe_libc_object,
                                                                                   (NativeExecutableLinkOptions){
                                                                                       .entry_symbol = S8("main"),
                                                                                   });
    BUSTER_TEST(arguments, aarch64_pe_libc_executable.error == LINK_ERROR_NONE);
    ObjectFile android_object = aarch64_libc_object;
    android_object.target.os = OPERATING_SYSTEM_ANDROID;
    String8 android_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-android-test"), S8(""));
    NativeExecutableLinkResult android_executable = link_native_executable(arguments->arena, &android_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .output_path = android_output_path,
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, android_executable.error == LINK_ERROR_NONE);
    bool android_header_valid =
        android_executable.executable.length > 18 && android_executable.executable.pointer[16] == 3 && android_executable.executable.pointer[17] == 0;
    BUSTER_TEST(arguments, android_header_valid);
    ObjectFile aarch64_mach_object = aarch64_object;
    aarch64_mach_object.target.os = OPERATING_SYSTEM_MACOS;
    String8 aarch64_mach_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-macho-test"), S8(""));
    NativeExecutableLinkResult aarch64_mach_executable = link_native_executable(arguments->arena, &aarch64_mach_object,
                                                                                (NativeExecutableLinkOptions){
                                                                                    .output_path = aarch64_mach_output_path,
                                                                                    .entry_symbol = S8("main"),
                                                                                });
    BUSTER_TEST(arguments, aarch64_mach_executable.error == LINK_ERROR_NONE);
    bool aarch64_mach_header_valid = aarch64_mach_executable.executable.length > 32 && aarch64_mach_executable.executable.pointer[0] == 0xcf &&
                                     aarch64_mach_executable.executable.pointer[1] == 0xfa && aarch64_mach_executable.executable.pointer[2] == 0xed &&
                                     aarch64_mach_executable.executable.pointer[3] == 0xfe && (aarch64_mach_executable.executable.pointer[26] & 0x20) != 0;
    BUSTER_TEST(arguments, aarch64_mach_header_valid);
    CodegenFunctionDescriptor aarch64_mach_cfi_descriptor = {
        .code_size = (u32)sizeof(aarch64_main_instructions),
    };
    DwarfCfiResult aarch64_mach_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                            .functions = &aarch64_mach_cfi_descriptor,
                                                                            .target = aarch64_mach_object.target,
                                                                            .function_count = 1,
                                                                        });
    BUSTER_TEST(arguments, aarch64_mach_cfi.valid && aarch64_mach_cfi.relocation_count == 1);
    ObjectRelocation aarch64_mach_cfi_relocation = {
        .offset = aarch64_mach_cfi.relocations[0].offset,
        .section = OBJECT_SECTION_UNWIND,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_AARCH64_PREL32,
    };
    ObjectFile aarch64_mach_cfi_object = aarch64_mach_object;
    ObjectSection* aarch64_mach_cfi_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(aarch64_mach_cfi_sections, aarch64_mach_object.sections, sizeof(*aarch64_mach_cfi_sections) * OBJECT_SECTION_COUNT);
    aarch64_mach_cfi_object.sections = aarch64_mach_cfi_sections;
    aarch64_mach_cfi_sections[OBJECT_SECTION_UNWIND].data = aarch64_mach_cfi.bytes;
    aarch64_mach_cfi_object.relocations = &aarch64_mach_cfi_relocation;
    aarch64_mach_cfi_object.relocation_count = 1;
    NativeExecutableLinkResult aarch64_mach_cfi_executable = link_native_executable(arguments->arena, &aarch64_mach_cfi_object,
                                                                                   (NativeExecutableLinkOptions){
                                                                                       .entry_symbol = S8("main"),
                                                                                   });
    BUSTER_TEST(arguments, aarch64_mach_cfi_executable.error == LINK_ERROR_NONE);
    u64 aarch64_mach_cfi_section = 0;
    bool aarch64_mach_cfi_found =
        link_test_mach_section_find(aarch64_mach_cfi_executable.executable, S8("__TEXT"), S8("__eh_frame"), &aarch64_mach_cfi_section);
    BUSTER_TEST(arguments, aarch64_mach_cfi_found);
    if (aarch64_mach_cfi_found)
    {
        BUSTER_TEST(arguments, link_read_u32(aarch64_mach_cfi_executable.executable.pointer, aarch64_mach_cfi_section + 64) == 0x6800000b);
        u64 unwind_address = link_read_u64(aarch64_mach_cfi_executable.executable.pointer, aarch64_mach_cfi_section + 32);
        u64 unwind_offset = link_read_u32(aarch64_mach_cfi_executable.executable.pointer, aarch64_mach_cfi_section + 48);
        s32 function_displacement = 0;
        memcpy(&function_displacement, aarch64_mach_cfi_executable.executable.pointer + unwind_offset + aarch64_mach_cfi.relocations[0].offset,
               sizeof(function_displacement));
        BUSTER_TEST(arguments, (s64)unwind_address + (s64)aarch64_mach_cfi.relocations[0].offset + function_displacement ==
                                   (s64)UINT64_C(0x100000000) + (s64)align_forward(32 + link_read_u32(aarch64_mach_cfi_executable.executable.pointer, 20), 16));
    }
    u32 aarch64_mach_data_instructions[] = {
        0x58000049, 0x14000003, 0, 0, 0x52800000, 0xd65f03c0,
    };
    u64 aarch64_mach_data_value = 42;
    ObjectSymbol aarch64_mach_data_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_mach_data_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("data"),
            .size = sizeof(aarch64_mach_data_value),
            .section = OBJECT_SECTION_DATA,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    ObjectRelocation aarch64_mach_data_relocation = {
        .offset = 2 * sizeof(u32),
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    ObjectFile aarch64_mach_data_object =
        link_test_object_make(arguments->arena, aarch64_mach_object.target,
                              (ByteSlice){
                                  .pointer = (u8*)aarch64_mach_data_instructions,
                                  .length = sizeof(aarch64_mach_data_instructions),
                              },
                              aarch64_mach_data_symbols, BUSTER_ARRAY_LENGTH(aarch64_mach_data_symbols), &aarch64_mach_data_relocation, 1);
    aarch64_mach_data_object.sections[OBJECT_SECTION_DATA].data = (ByteSlice){
        .pointer = (u8*)&aarch64_mach_data_value,
        .length = sizeof(aarch64_mach_data_value),
    };
    String8 aarch64_mach_data_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-macho-data-test"), S8(""));
    NativeExecutableLinkResult aarch64_mach_data_executable = link_native_executable(arguments->arena, &aarch64_mach_data_object,
                                                                                     (NativeExecutableLinkOptions){
                                                                                         .output_path = aarch64_mach_data_output_path,
                                                                                         .entry_symbol = S8("main"),
                                                                                     });
    BUSTER_TEST(arguments, aarch64_mach_data_executable.error == LINK_ERROR_NONE);
    u32 aarch64_mach_data_adrp = 0;
    u32 aarch64_mach_data_add = 0;
    u32 aarch64_mach_data_branch = 0;
    if (aarch64_mach_data_executable.error == LINK_ERROR_NONE && aarch64_mach_data_executable.executable.length > 32)
    {
        u64 aarch64_mach_data_text_offset = align_forward(32 + link_read_u32(aarch64_mach_data_executable.executable.pointer, 20), 16);
        if (aarch64_mach_data_text_offset <= aarch64_mach_data_executable.executable.length &&
            3 * sizeof(u32) <= aarch64_mach_data_executable.executable.length - aarch64_mach_data_text_offset)
        {
            aarch64_mach_data_adrp = link_read_u32(aarch64_mach_data_executable.executable.pointer, aarch64_mach_data_text_offset);
            aarch64_mach_data_add = link_read_u32(aarch64_mach_data_executable.executable.pointer, aarch64_mach_data_text_offset + sizeof(u32));
            aarch64_mach_data_branch = link_read_u32(aarch64_mach_data_executable.executable.pointer, aarch64_mach_data_text_offset + 2 * sizeof(u32));
        }
    }
    BUSTER_TEST(arguments, (aarch64_mach_data_adrp & UINT32_C(0x9f00001f)) == UINT32_C(0x90000009));
    BUSTER_TEST(arguments, (aarch64_mach_data_add & UINT32_C(0xffc003ff)) == UINT32_C(0x91000129));
    BUSTER_TEST(arguments, aarch64_mach_data_branch == UINT32_C(0x14000002));
    ObjectFile aarch64_mach_libc_object = aarch64_libc_object;
    aarch64_mach_libc_object.target.os = OPERATING_SYSTEM_MACOS;
    String8 aarch64_mach_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-macho-libc-test"), S8(""));
    NativeExecutableLinkResult aarch64_mach_libc_executable = link_native_executable(arguments->arena, &aarch64_mach_libc_object,
                                                                                     (NativeExecutableLinkOptions){
                                                                                         .output_path = aarch64_mach_libc_output_path,
                                                                                         .entry_symbol = S8("main"),
                                                                                     });
    BUSTER_TEST(arguments, aarch64_mach_libc_executable.error == LINK_ERROR_NONE);
    // Debug sections must reach the Mach-O image in an unmapped __DWARF
    // segment, with their address relocations resolved statically.
    ObjectFile mach_debug_object = aarch64_mach_object;
    ObjectSection* mach_debug_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(mach_debug_sections, mach_debug_object.sections, OBJECT_SECTION_COUNT * sizeof(*mach_debug_sections));
    mach_debug_object.sections = mach_debug_sections;
    u8 mach_debug_info[16] = {0};
    u8 mach_debug_line[8] = {0};
    mach_debug_sections[OBJECT_SECTION_DEBUG_INFO].data = (ByteSlice){
        .pointer = mach_debug_info,
        .length = sizeof(mach_debug_info),
    };
    mach_debug_sections[OBJECT_SECTION_DEBUG_LINE].data = (ByteSlice){
        .pointer = mach_debug_line,
        .length = sizeof(mach_debug_line),
    };
    ObjectRelocation mach_debug_relocation = {
        .offset = 0,
        .section = OBJECT_SECTION_DEBUG_INFO,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    mach_debug_object.relocations = &mach_debug_relocation;
    mach_debug_object.relocation_count = 1;
    NativeExecutableLinkResult mach_debug_executable = link_native_executable(arguments->arena, &mach_debug_object,
                                                                             (NativeExecutableLinkOptions){
                                                                                 .entry_symbol = S8("main"),
                                                                             });
    BUSTER_TEST(arguments, mach_debug_executable.error == LINK_ERROR_NONE);
    if (mach_debug_executable.error == LINK_ERROR_NONE)
    {
        ByteSlice image = mach_debug_executable.executable;
        BUSTER_TEST(arguments, image.length > 32 && link_read_u32(image.pointer, 0) == 0xfeedfacf);
        u32 mach_command_count = link_read_u32(image.pointer, 16);
        u64 mach_command = 32;
        bool found_dwarf = false;
        u64 dwarf_file_offset = 0;
        u32 dwarf_section_count = 0;
        for (u32 command_index = 0; command_index < mach_command_count && mach_command + 8 <= image.length; command_index += 1)
        {
            u32 command_kind = link_read_u32(image.pointer, mach_command);
            u32 command_length = link_read_u32(image.pointer, mach_command + 4);
            if (!command_length || mach_command + command_length > image.length)
            {
                break;
            }
            if (command_kind == 0x19 && memcmp(image.pointer + mach_command + 8, "__DWARF", 8) == 0)
            {
                found_dwarf = true;
                dwarf_file_offset = link_read_u64(image.pointer, mach_command + 40);
                dwarf_section_count = link_read_u32(image.pointer, mach_command + 64);
                // dyld rejects a segment whose file size exceeds its virtual
                // size, so the debug segment covers its bytes with a page
                // rounded read-only mapping.
                BUSTER_TEST(arguments, link_read_u64(image.pointer, mach_command + 48) == sizeof(mach_debug_info) + sizeof(mach_debug_line));
                BUSTER_TEST(arguments, link_read_u64(image.pointer, mach_command + 32) == 0x4000);
                BUSTER_TEST(arguments, link_read_u64(image.pointer, mach_command + 24) == UINT64_C(0x100000000) + dwarf_file_offset);
                BUSTER_TEST(arguments, link_read_u32(image.pointer, mach_command + 60) == 1);
                for (u32 section_index = 0; section_index < dwarf_section_count; section_index += 1)
                {
                    u64 section_command = mach_command + 72 + (u64)section_index * 80;
                    BUSTER_TEST(arguments, memcmp(image.pointer + section_command + 16, "__DWARF", 8) == 0);
                    BUSTER_TEST(arguments, (link_read_u32(image.pointer, section_command + 64) & 0x02000000) != 0);
                    // Sections must stay inside the address range of the
                    // segment that carries them.
                    BUSTER_TEST(arguments, link_read_u64(image.pointer, section_command + 32) ==
                                               UINT64_C(0x100000000) + link_read_u32(image.pointer, section_command + 48));
                }
            }
            mach_command += command_length;
        }
        BUSTER_TEST(arguments, found_dwarf);
        BUSTER_TEST(arguments, dwarf_section_count == 6);
        // The kernel loader refuses images with segments that are not page
        // aligned in the file.
        BUSTER_TEST(arguments, dwarf_file_offset % 0x4000 == 0);
        // The resolved slot must hold the link-time address of "main".
        BUSTER_TEST(arguments, dwarf_file_offset + 8 <= image.length);
        if (dwarf_file_offset + 8 <= image.length)
        {
            BUSTER_TEST(arguments, link_read_u64(image.pointer, dwarf_file_offset) >= UINT64_C(0x100000000));
        }
    }
    ObjectFile ios_mach_object = aarch64_mach_object;
    ios_mach_object.target.os = OPERATING_SYSTEM_IOS;
    String8 ios_mach_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-ios-macho-test"), S8(""));
    NativeExecutableLinkResult ios_mach_executable = link_native_executable(arguments->arena, &ios_mach_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .output_path = ios_mach_output_path,
                                                                                .entry_symbol = S8("main"),
                                                                            });
    BUSTER_TEST(arguments, ios_mach_executable.error == LINK_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
    ObjectFile x86_mach_object = linked.object;
    x86_mach_object.target.os = OPERATING_SYSTEM_MACOS;
    String8 x86_mach_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-x86-macho-test"), S8(""));
    NativeExecutableLinkResult x86_mach_executable = link_native_executable(arguments->arena, &x86_mach_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .output_path = x86_mach_output_path,
                                                                                .entry_symbol = S8("main"),
                                                                            });
    BUSTER_TEST(arguments, x86_mach_executable.error == LINK_ERROR_NONE);
    u8 x86_mach_libc_text[] = {
        0x48, 0x83, 0xec, 0x08, 0xbf, 0xd6, 0xff, 0xff, 0xff, 0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0x48, 0x83, 0xc4, 0x08, 0xc3,
    };
    ObjectSymbol x86_mach_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(x86_mach_libc_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation x86_mach_libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile x86_mach_libc_object = link_test_object_make(arguments->arena, x86_mach_object.target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(x86_mach_libc_text),
                                                            x86_mach_libc_symbols, BUSTER_ARRAY_LENGTH(x86_mach_libc_symbols), &x86_mach_libc_relocation, 1);
    String8 x86_mach_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-x86-macho-libc-test"), S8(""));
    NativeExecutableLinkResult x86_mach_libc_executable = link_native_executable(arguments->arena, &x86_mach_libc_object,
                                                                                 (NativeExecutableLinkOptions){
                                                                                     .output_path = x86_mach_libc_output_path,
                                                                                     .entry_symbol = S8("main"),
                                                                                 });
    BUSTER_TEST(arguments, x86_mach_libc_executable.error == LINK_ERROR_NONE);
#endif
#if BUSTER_MACOS
    String8* native_mach_paths = 0;
    u32 native_mach_path_count = 0;
#if BUSTER_CPU_ARCH_X86_64
    String8 x86_native_mach_paths[] = {
        x86_mach_output_path,
        x86_mach_libc_output_path,
    };
    native_mach_paths = x86_native_mach_paths;
    native_mach_path_count = BUSTER_ARRAY_LENGTH(x86_native_mach_paths);
#elif BUSTER_CPU_ARCH_AARCH64
    String8 aarch64_native_mach_paths[] = {
        aarch64_mach_output_path,
        aarch64_mach_data_output_path,
        aarch64_mach_libc_output_path,
    };
    native_mach_paths = aarch64_native_mach_paths;
    native_mach_path_count = BUSTER_ARRAY_LENGTH(aarch64_native_mach_paths);
#endif
    for (u32 path_index = 0; path_index < native_mach_path_count; path_index += 1)
    {
        String8 run_arguments[] = {
            native_mach_paths[path_index],
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
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    String8 native_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-link-test"), S8(""));
    NativeExecutableLinkResult native_executable = link_native_executable(arguments->arena, &cfi_linked.object,
                                                                          (NativeExecutableLinkOptions){
                                                                              .output_path = native_output_path,
                                                                              .entry_symbol = S8("main"),
                                                                          });
    BUSTER_TEST(arguments, native_executable.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, native_executable.executable.length >= 4);
    if (native_executable.error == LINK_ERROR_NONE)
    {
        u32 unwind_section_index = 0;
        u64 unwind_section_header = 0;
        u32 unwind_header_section_index = 0;
        u64 unwind_header_section_header = 0;
        u32 text_section_index = 0;
        u64 text_section_header = 0;
        bool unwind_section_found = link_test_elf_section_find(native_executable.executable, S8(".eh_frame"), &unwind_section_index, &unwind_section_header);
        bool unwind_header_section_found =
            link_test_elf_section_find(native_executable.executable, S8(".eh_frame_hdr"), &unwind_header_section_index, &unwind_header_section_header);
        bool text_section_found = link_test_elf_section_find(native_executable.executable, S8(".text"), &text_section_index, &text_section_header);
        BUSTER_TEST(arguments, unwind_section_found);
        BUSTER_TEST(arguments, unwind_header_section_found);
        BUSTER_TEST(arguments, text_section_found);
        if (unwind_section_found && unwind_header_section_found && text_section_found)
        {
            u64 unwind_flags = link_read_u64(native_executable.executable.pointer, unwind_section_header + 8);
            u64 unwind_address = link_read_u64(native_executable.executable.pointer, unwind_section_header + 16);
            u64 unwind_offset = link_read_u64(native_executable.executable.pointer, unwind_section_header + 24);
            u64 unwind_size = link_read_u64(native_executable.executable.pointer, unwind_section_header + 32);
            u64 unwind_header_address = link_read_u64(native_executable.executable.pointer, unwind_header_section_header + 16);
            u64 unwind_header_offset = link_read_u64(native_executable.executable.pointer, unwind_header_section_header + 24);
            u64 unwind_header_size = link_read_u64(native_executable.executable.pointer, unwind_header_section_header + 32);
            u64 text_address = link_read_u64(native_executable.executable.pointer, text_section_header + 16);
            BUSTER_TEST(arguments, unwind_flags == 0x2);
            BUSTER_TEST(arguments, unwind_size == cfi_linked.object.sections[OBJECT_SECTION_UNWIND].data.length);
            BUSTER_TEST(arguments, unwind_header_size == 28);
            if (unwind_header_offset + unwind_header_size <= native_executable.executable.length)
            {
                u8* header = native_executable.executable.pointer + unwind_header_offset;
                BUSTER_TEST(arguments, header[0] == 1 && header[1] == 0x1b && header[2] == 0x03 && header[3] == 0x3b);
                BUSTER_TEST(arguments, link_read_u32(header, 8) == 2);
                s32 frame_displacement = 0;
                s32 first_function = 0;
                s32 first_fde = 0;
                s32 second_function = 0;
                s32 second_fde = 0;
                memcpy(&frame_displacement, header + 4, sizeof(frame_displacement));
                memcpy(&first_function, header + 12, sizeof(first_function));
                memcpy(&first_fde, header + 16, sizeof(first_fde));
                memcpy(&second_function, header + 20, sizeof(second_function));
                memcpy(&second_fde, header + 24, sizeof(second_fde));
                BUSTER_TEST(arguments, (s64)unwind_header_address + 4 + frame_displacement == (s64)unwind_address);
                BUSTER_TEST(arguments, first_function < second_function);
                BUSTER_TEST(arguments,
                            (s64)unwind_header_address + first_fde ==
                                (s64)unwind_address + (s64)answer_cfi.relocations[0].offset - 8);
                BUSTER_TEST(arguments,
                            (s64)unwind_header_address + second_fde ==
                                (s64)unwind_address + (s64)answer_cfi.bytes.length + (s64)main_cfi.relocations[0].offset - 8);
            }
            else
            {
                BUSTER_TEST(arguments, false);
            }
            bool unwind_program_header_found = false;
            u64 program_header_offset = link_read_u64(native_executable.executable.pointer, 32);
            u32 program_header_size = native_executable.executable.pointer[54] | ((u32)native_executable.executable.pointer[55] << 8);
            u32 program_header_count = native_executable.executable.pointer[56] | ((u32)native_executable.executable.pointer[57] << 8);
            for (u32 program_header_index = 0; program_header_index < program_header_count; program_header_index += 1)
            {
                u64 header_offset = program_header_offset + (u64)program_header_index * program_header_size;
                if (header_offset + program_header_size <= native_executable.executable.length &&
                    link_read_u32(native_executable.executable.pointer, header_offset) == 0x6474e550)
                {
                    unwind_program_header_found = link_read_u64(native_executable.executable.pointer, header_offset + 8) == unwind_header_offset &&
                                                  link_read_u64(native_executable.executable.pointer, header_offset + 32) == unwind_header_size;
                }
            }
            BUSTER_TEST(arguments, unwind_program_header_found);
            if (unwind_offset + answer_cfi.relocations[0].offset + 4 <= native_executable.executable.length)
            {
                s32 displacement = 0;
                memcpy(&displacement, native_executable.executable.pointer + unwind_offset + answer_cfi.relocations[0].offset, sizeof(displacement));
                BUSTER_TEST(arguments,
                            (s64)unwind_address + (s64)answer_cfi.relocations[0].offset + displacement ==
                                (s64)text_address + (s64)cfi_linked.object.symbols[0].value);
            }
            else
            {
                BUSTER_TEST(arguments, false);
            }
        }
        String8 run_arguments[] = {
            native_output_path,
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
    NativeExecutableLinkResult unresolved_native = link_native_executable(arguments->arena, &permitted.object, (NativeExecutableLinkOptions){0});
    BUSTER_TEST(arguments, unresolved_native.error == LINK_ERROR_NONE);
    u8 libc_main_text[] = {
        0x48, 0x83, 0xec, 0x08, 0xbf, 0xd6, 0xff, 0xff, 0xff, 0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0x48, 0x83, 0xc4, 0x08, 0xc3,
    };
    ObjectSymbol libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(libc_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile libc_object = link_test_object_make(arguments->arena, target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(libc_main_text), libc_symbols,
                                                   BUSTER_ARRAY_LENGTH(libc_symbols), &libc_relocation, 1);
    String8 libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-libc-link-test"), S8(""));
    NativeExecutableLinkResult libc_executable = link_native_executable(arguments->arena, &libc_object,
                                                                        (NativeExecutableLinkOptions){
                                                                            .output_path = libc_output_path,
                                                                            .entry_symbol = S8("main"),
                                                                        });
    BUSTER_TEST(arguments, libc_executable.error == LINK_ERROR_NONE);
    if (libc_executable.error == LINK_ERROR_NONE)
    {
        String8 run_arguments[] = {
            libc_output_path,
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
#else
    BUSTER_UNUSED(target);
#endif
    UnitTestResult runtime_stack_walk = link_test_runtime_stack_walk(arguments);
    result.succeeded_test_count += runtime_stack_walk.succeeded_test_count;
    result.test_count += runtime_stack_walk.test_count;
    return result;
}
