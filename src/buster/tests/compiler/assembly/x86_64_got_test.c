// Independent GNU as / LLVM bytes: tests/x86_64_got_encoding_oracle.s.
#include <buster/tests/compiler/assembly/x86_64_got_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
static u8 const x86_got_got_oracle[16][7] = {
    {0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8b, 0x3d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8b, 0x3d, 0x00, 0x00, 0x00, 0x00},
};
static u8 const x86_got_address_oracle[16][7] = {
    {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x8d, 0x3d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x8d, 0x3d, 0x00, 0x00, 0x00, 0x00},
};

UnitTestResult x86_64_got_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_load(0, 3, 7));
    u8 bytes[128];
    u8 expected[128];
    static u32 const values[] = {0, 1, 127, 128, 0x7fffffff, 0x80000000, 0xffffff80, 0xffffffff};
    for (u32 reg = 0; reg < 16; reg += 1)
    {
        // Numeric field contents are not part of instruction-shape matching.
        for (u32 variant = 0; variant < 4; variant += 1)
        {
            for (u32 value = 0; value < BUSTER_ARRAY_LENGTH(values); value += 1)
            {
                memset(bytes, 0xa5, sizeof(bytes));
                memcpy(bytes + 11, x86_got_got_oracle[reg], 7);
                bytes[11] |= (u8)variant;
                memcpy(bytes + 14, &values[value], 4);
                memcpy(expected, bytes, sizeof(bytes));
                memcpy(expected + 11, x86_got_address_oracle[reg], 3);
                BUSTER_TEST(arguments, buster_x86_metadata_relax_got_load(bytes, 14, 18));
                BUSTER_TEST(arguments, memcmp(bytes, expected, sizeof(bytes)) == 0);
            }
        }
        BusterX86MetadataPhysicalOperand operands[2] = {
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
             .reg = {.index = (u16)reg, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY, .width = 64,
             .memory = {.has_symbol = true, .symbol = S8("target"), .rip_relative = true,
                        .address_size = 64, .scale = 1}},
        };
        for (u32 output = 0; output < 2; output += 1)
        {
            BusterX86MetadataPhysicalQuery query = {
                .mnemonic = output ? S8("LEA") : S8("MOV"), .operands = operands, .operand_count = 2,
                .address_size = 64, .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            };
            BusterX86MetadataRelocation field = {0};
            BusterX86MetadataEmitResult checked = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
                .physical = query, .output = bytes, .output_capacity = sizeof(bytes), .relocations = &field, .relocation_capacity = 1,
            });
            BUSTER_TEST(arguments, checked.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && checked.byte_count == 7 && checked.relocation_count == 1);
            BUSTER_TEST(arguments, field.offset == 3 && field.width == 4 && field.addend == -4 && field.kind == BUSTER_X86_METADATA_RELOCATION_PC32);
            BUSTER_TEST(arguments, memcmp(bytes, output ? x86_got_address_oracle[reg] : x86_got_got_oracle[reg], 7) == 0);
            BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(query);
            BusterX86MetadataEmitResult exact = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = query, .form_id = selected.form_id, .output = expected, .output_capacity = sizeof(expected),
                .relocations = &field, .relocation_capacity = 1,
            });
            BUSTER_TEST(arguments, exact.status == checked.status && exact.byte_count == checked.byte_count && memcmp(bytes, expected, 7) == 0);
        }
    }
    for (u32 alignment = 0; alignment < 32; alignment += 1)
    {
        for (u32 size = 0; size <= 32; size += 1)
        {
            memset(bytes, 0xa5, sizeof(bytes));
            memcpy(bytes + alignment, x86_got_got_oracle[0], 7);
            memcpy(expected, bytes, sizeof(bytes));
            bool success = buster_x86_metadata_relax_got_load(bytes, alignment + 3, size);
            bool valid = size >= alignment + 7;
            if (valid) memcpy(expected + alignment, x86_got_address_oracle[0], 3);
            BUSTER_TEST(arguments, success == valid && memcmp(bytes, expected, sizeof(bytes)) == 0);
        }
    }
    // Exhaust the finite REX/ModRM domain using independent oracle matching.
    for (u32 rex = 0; rex < 256; rex += 1)
    {
        for (u32 modrm = 0; modrm < 256; modrm += 1)
        {
            memcpy(bytes, x86_got_got_oracle[0], 7);
            bytes[0] = (u8)rex;
            bytes[2] = (u8)modrm;
            memcpy(expected, bytes, 7);
            bool valid = false;
            for (u32 reg = 0; reg < 16; reg += 1)
            {
                for (u32 variant = 0; variant < 4; variant += 1)
                {
                    if (rex == (u32)(x86_got_got_oracle[reg][0] | variant) && modrm == x86_got_got_oracle[reg][2])
                    {
                        memcpy(expected, x86_got_address_oracle[reg], 7);
                        valid = true;
                    }
                }
            }
            BUSTER_TEST(arguments, buster_x86_metadata_relax_got_load(bytes, 3, 7) == valid);
            BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
        }
    }
    for (u32 opcode = 0; opcode < 256; opcode += 1)
    {
        memcpy(bytes, x86_got_got_oracle[0], 7);
        bytes[1] = (u8)opcode;
        memcpy(expected, bytes, 7);
        bool valid = opcode == x86_got_got_oracle[0][1];
        if (valid) memcpy(expected, x86_got_address_oracle[0], 7);
        BUSTER_TEST(arguments, buster_x86_metadata_relax_got_load(bytes, 3, 7) == valid && memcmp(bytes, expected, 7) == 0);
    }
    memcpy(bytes, x86_got_got_oracle[0], 7);
    memcpy(expected, bytes, 7);
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_load(bytes, 2, 7));
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_load(bytes, UINT64_MAX, 7));
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_got_load(bytes, UINT64_MAX, UINT64_MAX));
    BUSTER_TEST(arguments, memcmp(bytes, expected, 7) == 0);
    return result;
}
#endif
