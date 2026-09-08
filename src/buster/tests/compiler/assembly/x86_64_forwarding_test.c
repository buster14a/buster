// The closed forwarding recipe, its two object consumers and checked/exact
// agreement. Independent instruction/relocation oracles live in
// tests/x86_64_forwarding_encoding_oracle.s; expectations below are not
// calculated with the metadata encoder under test.
#include <buster/tests/compiler/assembly/x86_64_forwarding_test.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
#if BUSTER_CPU_ARCH_X86_64 && BUSTER_LINUX && !BUSTER_SANITIZE
BUSTER_GLOBAL_LOCAL u64 x86_64_forwarding_probe(u64 first, u64 second, u64 third)
{
    return first ^ (second | third);
}
#endif

UnitTestResult x86_64_forwarding_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u8 const oracle[] = {0x31, 0xf6, 0x31, 0xd2, 0xe9, 0, 0, 0, 0};
    u8 actual[96];
    u8 expected[sizeof(actual)];
    BusterX86MetadataRelocation branch;
    u8 saved_branch[sizeof(branch)];
    // Invalid kinds include the negative and count boundaries. Check every
    // byte of both outputs on failure, including relocation padding.
    for (s32 model = -1; model <= BUSTER_X86_METADATA_FORWARDING_KIND_COUNT; model += 1)
    {
        bool valid_kind = model == BUSTER_X86_METADATA_FORWARDING_JUMP || model == BUSTER_X86_METADATA_FORWARDING_ZERO_ARGUMENTS;
        u32 size = model == BUSTER_X86_METADATA_FORWARDING_JUMP ? 5 : 9;
        for (u32 alignment = 0; alignment < 64; alignment += 1)
        {
            for (u32 capacity = 0; capacity <= 16; capacity += 1)
            {
                memset(actual, 0xa5, sizeof(actual));
                memset(expected, 0xa5, sizeof(expected));
                memset(&branch, 0xa5, sizeof(branch));
                memcpy(saved_branch, &branch, sizeof(branch));
                bool emitted = buster_x86_metadata_emit_forwarding(actual + alignment, capacity, (BusterX86MetadataForwardingKind)model, &branch);
                bool success = valid_kind && capacity >= size;
                BUSTER_TEST(arguments, emitted == success);
                if (success)
                {
                    memcpy(expected + alignment, oracle + sizeof(oracle) - size, size);
                    BUSTER_TEST(arguments, branch.offset == size - 4 && branch.width == 4 &&
                                           branch.kind == BUSTER_X86_METADATA_RELOCATION_PC32 && branch.addend == -4 &&
                                           !branch.symbol.pointer && !branch.symbol.length);
                }
                else
                {
                    BUSTER_TEST(arguments, memcmp(&branch, saved_branch, sizeof(branch)) == 0);
                }
                BUSTER_TEST(arguments, memcmp(actual, expected, sizeof(actual)) == 0);
            }
        }
    }
    memset(actual, 0xa5, sizeof(actual));
    memcpy(expected, actual, sizeof(actual));
    memset(&branch, 0xa5, sizeof(branch));
    memcpy(saved_branch, &branch, sizeof(branch));
    BUSTER_TEST(arguments, !buster_x86_metadata_emit_forwarding(0, UINT32_MAX, BUSTER_X86_METADATA_FORWARDING_JUMP, &branch));
    BUSTER_TEST(arguments, memcmp(&branch, saved_branch, sizeof(branch)) == 0);
    BUSTER_TEST(arguments, !buster_x86_metadata_emit_forwarding(actual, UINT32_MAX, BUSTER_X86_METADATA_FORWARDING_JUMP, 0));
    BUSTER_TEST(arguments, memcmp(actual, expected, sizeof(actual)) == 0);

    // Resolve the actual instruction operands independently, then use the
    // returned form ID for exact emission. The symbol is deliberately
    // unresolved: substituting numeric zero could select a short JMP.
    u16 const registers[] = {6, 2};
    for (u32 instruction = 0; instruction < 3; instruction += 1)
    {
        BusterX86MetadataPhysicalOperand operands[2] = {0};
        BusterX86MetadataPhysicalQuery physical = {
            .mnemonic = S8("XOR"), .operands = operands, .operand_count = 2,
            .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        };
        u32 size = 2;
        u32 start = instruction * 2;
        if (instruction < 2)
        {
            operands[0] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 32,
                .reg = {.index = registers[instruction], .width = 32, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            };
            operands[1] = operands[0];
        }
        else
        {
            operands[0] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE, .width = 32,
                .has_symbol = true, .symbol = S8("independent_target"),
            };
            physical.mnemonic = S8("JMP");
            physical.operand_count = 1;
            size = 5;
        }
        u8 checked[16] = {0};
        u8 exact[16] = {0};
        BusterX86MetadataRelocation checked_field = {0};
        BusterX86MetadataRelocation exact_field = {0};
        BusterX86MetadataEmitResult checked_result = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = physical, .output = checked, .output_capacity = sizeof(checked),
            .relocations = &checked_field, .relocation_capacity = 1,
        });
        BUSTER_TEST(arguments, checked_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && checked_result.byte_count == size);
        if (checked_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && checked_result.byte_count == size)
        {
            BusterX86MetadataEmitResult exact_result = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = physical, .form_id = checked_result.form_id, .output = exact, .output_capacity = sizeof(exact),
                .relocations = &exact_field, .relocation_capacity = 1,
            });
            BUSTER_TEST(arguments, exact_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && exact_result.byte_count == size);
            BUSTER_TEST(arguments, memcmp(checked, oracle + start, size) == 0 && memcmp(exact, checked, size) == 0);
            BUSTER_TEST(arguments, checked_result.relocation_count == (u32)(instruction == 2) &&
                                   exact_result.relocation_count == checked_result.relocation_count);
            if (instruction == 2)
            {
                BUSTER_TEST(arguments, checked_field.offset == 1 && checked_field.width == 4 && checked_field.addend == -4 &&
                                       checked_field.kind == BUSTER_X86_METADATA_RELOCATION_PC32);
                BUSTER_TEST(arguments, exact_field.offset == checked_field.offset && exact_field.width == checked_field.width &&
                                       exact_field.addend == checked_field.addend && exact_field.kind == checked_field.kind &&
                                       string_equal(exact_field.symbol, operands[0].symbol) && string_equal(checked_field.symbol, operands[0].symbol));
            }
        }
    }

    // Both object producers share the migrated helper. Preserve symbol and
    // argument policy, relative fields, weak definitions, and AArch64 bytes.
    u8 const arm_oracle[] = {1, 0, 0x80, 0xd2, 2, 0, 0x80, 0xd2, 0, 0, 0, 0x14};
    String8 const stubs[] = {S8_INITIALIZER("atexit"), S8_INITIALIZER("at_quick_exit")};
    String8 const elf_targets[] = {S8_INITIALIZER("__cxa_atexit"), S8_INITIALIZER("__cxa_at_quick_exit")};
    String8 const windows_targets[] = {S8_INITIALIZER("_crt_atexit"), S8_INITIALIZER("_crt_at_quick_exit")};
    for (u32 architecture = 0; architecture < 2; architecture += 1)
    {
        for (u32 windows = 0; windows < 2; windows += 1)
        {
            bool x86 = architecture == 0;
            Target target = {.cpu_arch = x86 ? CPU_ARCH_X86_64 : CPU_ARCH_AARCH64,
                             .os = windows ? OPERATING_SYSTEM_WINDOWS : OPERATING_SYSTEM_LINUX};
            ObjectFile object = windows ? link_windows_libc_runtime_object(arguments->arena, target)
                                        : link_elf_libc_runtime_object(arguments->arena, target);
            BUSTER_TEST(arguments, object.error == OBJECT_ERROR_NONE && object.section_count == OBJECT_SECTION_COUNT &&
                                   object.symbol_count == 4 && object.relocation_count == 2);
            if (object.error == OBJECT_ERROR_NONE && object.section_count == OBJECT_SECTION_COUNT &&
                object.symbol_count == 4 && object.relocation_count == 2)
            {
                u32 size = x86 ? (windows ? 5 : 9) : (windows ? 4 : 12);
                u8 const* bytes = x86 ? oracle + sizeof(oracle) - size : arm_oracle + sizeof(arm_oracle) - size;
                ByteSlice text = object.sections[OBJECT_SECTION_TEXT].data;
                BUSTER_TEST(arguments, text.length == size * 2 && object.sections[OBJECT_SECTION_TEXT].virtual_size == text.length);
                if (text.length == size * 2)
                {
                    BUSTER_TEST(arguments, memcmp(text.pointer, bytes, size) == 0 && memcmp(text.pointer + size, bytes, size) == 0);
                }
                for (u32 stub = 0; stub < 2; stub += 1)
                {
                    ObjectSymbol defined = object.symbols[stub * 2];
                    ObjectSymbol imported = object.symbols[stub * 2 + 1];
                    ObjectRelocation relocation = object.relocations[stub];
                    BUSTER_TEST(arguments, string_equal(defined.name, stubs[stub]) && defined.weak && defined.global &&
                                           defined.kind == OBJECT_SYMBOL_FUNCTION && defined.section == OBJECT_SECTION_TEXT &&
                                           defined.value == stub * size && defined.size == size);
                    BUSTER_TEST(arguments, string_equal(imported.name, windows ? windows_targets[stub] : elf_targets[stub]) &&
                                           imported.global && !imported.weak && imported.kind == OBJECT_SYMBOL_FUNCTION &&
                                           imported.section == OBJECT_SECTION_UNDEFINED);
                    BUSTER_TEST(arguments, relocation.offset == stub * size + size - 4 && relocation.symbol == stub * 2 + 1 &&
                                           relocation.section == OBJECT_SECTION_TEXT && relocation.addend == (x86 ? -4 : 0) &&
                                           relocation.kind == (x86 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_JUMP26));
                }
            }
        }
    }
#if BUSTER_CPU_ARCH_X86_64 && BUSTER_LINUX && !BUSTER_SANITIZE
    // Execute both ELF tails through the production object JIT. Nonzero high
    // halves prove that 32-bit XOR clears the whole argument register; the
    // returned checksum also proves that the tail preserves the caller's
    // return address and the complete first argument. Like the existing JIT
    // suite, native calls are excluded from sanitizer-instrumented builds.
    typedef u64 ForwardingProbe(u64, u64, u64);
    ForwardingProbe* host = &x86_64_forwarding_probe;
    void* host_address = 0;
    BUSTER_CT_CHECK(sizeof(host) == sizeof(host_address));
    memcpy(&host_address, &host, sizeof(host_address));
    JitHostBinding bindings[] = {
        {.name = S8("__cxa_atexit"), .address = host_address, .kind = OBJECT_SYMBOL_FUNCTION},
        {.name = S8("__cxa_at_quick_exit"), .address = host_address, .kind = OBJECT_SYMBOL_FUNCTION},
    };
    ObjectFile runtime = link_elf_libc_runtime_object(arguments->arena, target_native);
    JitProgram program = jit_link_object(&runtime, (JitOptions){.bindings = bindings, .binding_count = BUSTER_ARRAY_LENGTH(bindings)});
    BUSTER_TEST(arguments, program.error == JIT_ERROR_NONE);
    if (program.error == JIT_ERROR_NONE)
    {
        for (u32 stub = 0; stub < BUSTER_ARRAY_LENGTH(stubs); stub += 1)
        {
            void* address = jit_program_symbol(&program, stubs[stub]);
            ForwardingProbe* function = 0;
            memcpy(&function, &address, sizeof(function));
            BUSTER_TEST(arguments, function && function(UINT64_C(0xfedcba9876543210), UINT64_C(0xf000000000000123),
                                                       UINT64_C(0x8000000000000042)) == UINT64_C(0xfedcba9876543210));
        }
    }
    jit_program_release(&program);
#endif
    return result;
}
#endif
