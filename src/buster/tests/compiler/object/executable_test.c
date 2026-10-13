// Ordinary mutable globals in a JIT-linked object: independent OS mapping
// queries, PC-relative and absolute addressing, zero-fill and release.
#include <buster/lib/system_headers.h>
#if BUSTER_LINUX || BUSTER_ANDROID
#include <stdio.h>
#endif
#if BUSTER_APPLE
#include <mach/mach.h>
#include <mach/vm_region.h>
#endif

BUSTER_GLOBAL_LOCAL bool object_test_mapping(void* pointer, ProtectionFlags expected)
{
    bool result = false;
#if BUSTER_WINDOWS
    MEMORY_BASIC_INFORMATION information = {0};
    SIZE_T queried = VirtualQuery(pointer, &information, sizeof(information));
    DWORD protection = expected.execute ? PAGE_EXECUTE_READ : expected.write ? PAGE_READWRITE : PAGE_READONLY;
    result = queried == sizeof(information) && information.State == MEM_COMMIT && information.Protect == protection;
#elif BUSTER_APPLE
    vm_address_t address = (vm_address_t)(uintptr_t)pointer;
    vm_size_t size = 0;
    vm_region_basic_info_data_64_t information = {0};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t status = vm_region_64(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                                         (vm_region_info_t)&information, &count, &object);
    vm_prot_t protection = VM_PROT_READ | (expected.write ? VM_PROT_WRITE : 0) | (expected.execute ? VM_PROT_EXECUTE : 0);
    result = status == KERN_SUCCESS && address <= (vm_address_t)(uintptr_t)pointer &&
             (vm_address_t)(uintptr_t)pointer - address < size && information.protection == protection;
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
#elif BUSTER_LINUX || BUSTER_ANDROID
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps)
    {
        char line[1024];
        while (!result && fgets(line, sizeof(line), maps))
        {
            unsigned long long begin = 0;
            unsigned long long end = 0;
            char permissions[5] = {0};
            if (sscanf(line, "%llx-%llx %4s", &begin, &end, permissions) == 3 &&
                begin <= (u64)(uintptr_t)pointer && (u64)(uintptr_t)pointer < end)
            {
                result = permissions[0] == 'r' && (permissions[1] == 'w') == (bool)expected.write &&
                         (permissions[2] == 'x') == (bool)expected.execute;
                break;
            }
        }
        (void)fclose(maps);
    }
#else
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(expected);
#endif
    return result;
}

// Apple Silicon keeps JIT code in its nominal RWX MAP_JIT mapping.
#define OBJECT_TEST_JIT_CODE_PROTECTION ((ProtectionFlags){.read = true, .write = BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64, .execute = true})

// The first mapped text section, where these fixtures place their entry.
BUSTER_GLOBAL_LOCAL void* object_test_jit_text(JitProgram const* program)
{
    void* result = 0;
    for (u32 section = 0; program->object && section < program->object->section_count && !result; section += 1)
    {
        if (program->object->sections[section].kind == OBJECT_SECTION_TEXT)
        {
            result = program->section_addresses[section];
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult object_test_executable_sections(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 page_size = os_get_page_size();
    u32 initial = 41;
    u64 pointers[2] = {0};
    u8 x86_relative[] = {0x8b, 0x05, 0, 0, 0, 0, 0x83, 0xc0, 1, 0x89, 0x05, 0, 0, 0, 0, 0xc3};
    u8 x86_absolute[] = {0x48, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0, 0x8b, 0x01, 0x83, 0xc0, 1, 0x89, 0x01, 0xc3};
    u32 arm_relative[] = {0x90000009, 0x91000129, 0xb9400120, 0x11000400, 0xb9000120, 0xd65f03c0};
    u32 arm_absolute[] = {0x580000c9, 0xb9400120, 0x11000400, 0xb9000120, 0xd65f03c0, 0xd503201f, 0, 0};
    for (u32 absolute = 0; absolute < 2; absolute += 1)
    {
        for (u32 zero = 0; zero < 2; zero += 1)
        {
            ByteSlice text = target_native.cpu_arch == CPU_ARCH_X86_64
                ? (absolute ? BUSTER_ARRAY_TO_BYTE_SLICE(x86_absolute) : BUSTER_ARRAY_TO_BYTE_SLICE(x86_relative))
                : (absolute ? BUSTER_ARRAY_TO_BYTE_SLICE(arm_absolute) : BUSTER_ARRAY_TO_BYTE_SLICE(arm_relative));
            ObjectSection sections[] = {
                {.name = S8(".text"), .kind = OBJECT_SECTION_TEXT, .data = text, .alignment = (u32)page_size},
                {.name = S8(".rodata"), .kind = OBJECT_SECTION_READ_ONLY_DATA, .data = BUSTER_ARRAY_TO_BYTE_SLICE(pointers), .alignment = 8},
                {.name = S8(".data"), .kind = OBJECT_SECTION_DATA, .data = {.pointer = (u8*)&initial, .length = sizeof(initial)}, .alignment = 4},
                {.name = S8(".bss"), .kind = OBJECT_SECTION_ZERO, .virtual_size = sizeof(initial), .alignment = 4},
            };
            ObjectSymbol symbols[] = {
                {.name = S8("counter"), .section = 2, .kind = OBJECT_SYMBOL_DATA, .size = sizeof(initial), .global = true},
                {.name = S8("zero_counter"), .section = 3, .kind = OBJECT_SYMBOL_DATA, .size = sizeof(initial), .global = true},
            };
            ObjectRelocation relocations[] = {
                {.section = 1, .offset = 0, .symbol = 0, .kind = OBJECT_RELOCATION_ABSOLUTE64},
                {.section = 1, .offset = 8, .symbol = 1, .kind = OBJECT_RELOCATION_ABSOLUTE64},
                {.section = 0, .symbol = zero},
                {.section = 0, .symbol = zero},
            };
            if (absolute)
            {
                relocations[2].kind = OBJECT_RELOCATION_ABSOLUTE64;
                relocations[2].offset = target_native.cpu_arch == CPU_ARCH_X86_64 ? 2 : 24;
            }
            else if (target_native.cpu_arch == CPU_ARCH_X86_64)
            {
                relocations[2].kind = relocations[3].kind = OBJECT_RELOCATION_X86_64_PC32;
                relocations[2].offset = 2;
                relocations[3].offset = 11;
                relocations[2].addend = relocations[3].addend = -4;
            }
            else
            {
                // The JIT applies the ADRP/ADD page pair through its Mach-O
                // relocation kinds on every AArch64 host.
                relocations[2].kind = OBJECT_RELOCATION_AARCH64_MACH_PAGE21;
                relocations[3].kind = OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12;
                relocations[3].offset = 4;
            }
            ObjectFile object = {.target = target_native, .sections = sections, .symbols = symbols, .relocations = relocations,
                .section_count = BUSTER_ARRAY_LENGTH(sections), .symbol_count = BUSTER_ARRAY_LENGTH(symbols), .relocation_count = absolute ? 3 : 4};
            // Unlike the retired object loader, the JIT refuses section
            // alignment beyond one host page before mapping anything.
            sections[0].alignment = (u32)(page_size * 2);
            JitProgram over_aligned = jit_link_object(&object, (JitOptions){0});
            BUSTER_TEST(arguments, over_aligned.error == JIT_ERROR_INVALID_INPUT && !over_aligned.allocation_base);
            sections[0].alignment = (u32)page_size;
            JitProgram program = jit_link_object(&object, (JitOptions){0});
            u8* text_address = (u8*)object_test_jit_text(&program);
            if (BUSTER_REQUIRE(arguments, program.error == JIT_ERROR_NONE && text_address && program.allocation_base))
            {
                u64 addresses[2] = {0};
                memcpy(addresses, program.section_addresses[1], sizeof(addresses));
                BUSTER_TEST(arguments, is_aligned((u64)(uintptr_t)text_address, page_size));
                BUSTER_TEST(arguments, object_test_mapping(text_address, OBJECT_TEST_JIT_CODE_PROTECTION));
                BUSTER_TEST(arguments, object_test_mapping(program.section_addresses[1], (ProtectionFlags){.read = true}));
                BUSTER_TEST(arguments, addresses[0] == (u64)(uintptr_t)program.section_addresses[2]);
                BUSTER_TEST(arguments, addresses[1] == (u64)(uintptr_t)program.section_addresses[3]);
                for (u32 index = 0; index < 2; index += 1)
                {
                    u32* state = (u32*)(uintptr_t)addresses[index];
                    BUSTER_TEST(arguments, object_test_mapping(state, (ProtectionFlags){.read = true, .write = true}));
                    BUSTER_TEST(arguments, *state == (index ? 0u : 41u));
                }
#if (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_SANITIZE
                u32 (*increment)(void) = 0;
                memcpy(&increment, &text_address, sizeof(increment));
                BUSTER_TEST(arguments, increment() == (zero ? 1u : 42u));
                BUSTER_TEST(arguments, increment() == (zero ? 2u : 43u));
#endif
            }
            jit_program_release(&program);
            // A valid compact object need not put its first text section at
            // index zero; the entry is still that text section's address.
            ObjectSection leading_sections[] = {sections[2], sections[1], sections[0], sections[3]};
            symbols[0].section = 0;
            relocations[2].section = relocations[3].section = 2;
            object.sections = leading_sections;
            JitProgram leading = jit_link_object(&object, (JitOptions){0});
            u8* entry_address = (u8*)object_test_jit_text(&leading);
            if (BUSTER_REQUIRE(arguments, leading.error == JIT_ERROR_NONE && entry_address == leading.section_addresses[2] && leading.allocation_base))
            {
                u64 states[2] = {0};
                memcpy(states, leading.section_addresses[1], sizeof(states));
                BUSTER_TEST(arguments, states[0] == (u64)(uintptr_t)leading.section_addresses[0]);
                BUSTER_TEST(arguments, states[1] == (u64)(uintptr_t)leading.section_addresses[3]);
                BUSTER_TEST(arguments, object_test_mapping(entry_address, OBJECT_TEST_JIT_CODE_PROTECTION));
                BUSTER_TEST(arguments, object_test_mapping(leading.section_addresses[1], (ProtectionFlags){.read = true}));
                BUSTER_TEST(arguments, object_test_mapping((void*)(uintptr_t)states[0], (ProtectionFlags){.read = true, .write = true}));
#if (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_SANITIZE
                u32 (*increment)(void) = 0;
                memcpy(&increment, &entry_address, sizeof(increment));
                BUSTER_TEST(arguments, increment() == (zero ? 1u : 42u));
                BUSTER_TEST(arguments, increment() == (zero ? 2u : 43u));
#endif
            }
            jit_program_release(&leading);
            BUSTER_TEST(arguments, !leading.allocation_base && !object_test_mapping(entry_address, OBJECT_TEST_JIT_CODE_PROTECTION));
        }
    }
    // Without nonempty text the JIT still maps data, but publishes no code;
    // an object with nothing to map at all is invalid input.
    ObjectSection data_only_sections[] = {
        {.kind = OBJECT_SECTION_TEXT},
        {.kind = OBJECT_SECTION_DATA, .data = {.pointer = (u8*)&initial, .length = sizeof(initial)}},
    };
    ObjectFile data_only = {.target = target_native, .sections = data_only_sections, .section_count = BUSTER_ARRAY_LENGTH(data_only_sections)};
    JitProgram no_entry = jit_link_object(&data_only, (JitOptions){0});
    BUSTER_TEST(arguments, no_entry.error == JIT_ERROR_NONE && !no_entry.executable_size && !object_test_jit_text(&no_entry) &&
                               no_entry.section_addresses[1] && *(u32*)no_entry.section_addresses[1] == initial);
    jit_program_release(&no_entry);
    ObjectFile empty = {.target = target_native, .sections = data_only_sections, .section_count = 1};
    JitProgram nothing = jit_link_object(&empty, (JitOptions){0});
    BUSTER_TEST(arguments, nothing.error == JIT_ERROR_INVALID_INPUT && !nothing.allocation_base);
    return result;
}
