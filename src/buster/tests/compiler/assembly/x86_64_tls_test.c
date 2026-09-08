// Independent byte oracles: tests/x86_64_tls_encoding_oracle.s.
#include <buster/tests/compiler/assembly/x86_64_tls_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
static u8 const x86_tls_gd_oracle[16] = {
    0x66, 0x48, 0x8d, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x48, 0xe8, 0x00, 0x00, 0x00, 0x00,
};
static u8 const x86_tls_le_oracle[16] = {
    0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x80, 0xfc, 0xff, 0xff, 0xff,
};
static u8 const x86_tls_ie_oracle[16][7] = {
    {0x48, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x03, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x03, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x03, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x03, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x03, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x03, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x48, 0x03, 0x3d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x0d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x15, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x1d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x25, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x2d, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x35, 0x00, 0x00, 0x00, 0x00},
    {0x4c, 0x03, 0x3d, 0x00, 0x00, 0x00, 0x00},
};
static u8 const x86_tls_add_oracle[16][7] = {
    {0x48, 0x81, 0xc0, 0xfc, 0xff, 0xff, 0xff},
    {0x48, 0x81, 0xc1, 0xfc, 0xff, 0xff, 0xff},
    {0x48, 0x81, 0xc2, 0xfc, 0xff, 0xff, 0xff},
    {0x48, 0x81, 0xc3, 0xfc, 0xff, 0xff, 0xff},
    {0x48, 0x81, 0xc4, 0xfc, 0xff, 0xff, 0xff},
    {0x48, 0x81, 0xc5, 0xfc, 0xff, 0xff, 0xff},
    {0x48, 0x81, 0xc6, 0xfc, 0xff, 0xff, 0xff},
    {0x48, 0x81, 0xc7, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc0, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc1, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc2, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc3, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc4, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc5, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc6, 0xfc, 0xff, 0xff, 0xff},
    {0x49, 0x81, 0xc7, 0xfc, 0xff, 0xff, 0xff},
};

BUSTER_GLOBAL_LOCAL bool x86_tls_test_value(u8 const* bytes, u32 size, s32 value)
{
    u32 offset = size - 4;
    u32 actual = (u32)bytes[offset] | ((u32)bytes[offset + 1] << 8) |
                 ((u32)bytes[offset + 2] << 16) | ((u32)bytes[offset + 3] << 24);
    bool result = actual == (u32)value;
    return result;
}

UnitTestResult x86_64_tls_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, !buster_x86_metadata_emit_tls_general_dynamic(0, UINT32_MAX));
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_tls(0, UINT32_MAX, BUSTER_X86_METADATA_TLS_GENERAL_DYNAMIC, 0));
    // Every alignment and short capacity; failures leave ALL caller storage
    // unchanged. Guard bytes also detect writes past a successful envelope.
    for (u32 alignment = 0; alignment < 64; alignment += 1)
    {
        for (u32 capacity = 0; capacity <= 32; capacity += 1)
        {
            u8 actual[128];
            u8 expected[128];
            memset(actual, 0xa5, sizeof(actual));
            memset(expected, 0xa5, sizeof(expected));
            bool emitted = buster_x86_metadata_emit_tls_general_dynamic(actual + alignment, capacity);
            BUSTER_TEST(arguments, emitted == (capacity >= 16));
            if (capacity >= 16) memcpy(expected + alignment, x86_tls_gd_oracle, 16);
            BUSTER_TEST(arguments, memcmp(actual, expected, sizeof(actual)) == 0);
            for (u32 model_index = 0; model_index < 2; model_index += 1)
            {
                BusterX86MetadataTlsModel model = model_index ? BUSTER_X86_METADATA_TLS_INITIAL_EXEC : BUSTER_X86_METADATA_TLS_GENERAL_DYNAMIC;
                u32 size = model_index ? 7u : 16u;
                u8 const* input = model_index ? x86_tls_ie_oracle[0] : x86_tls_gd_oracle;
                u8 const* output = model_index ? x86_tls_add_oracle[0] : x86_tls_le_oracle;
                memset(actual, 0xa5, sizeof(actual));
                memcpy(actual + alignment, input, size);
                memcpy(expected, actual, sizeof(actual));
                bool relaxed = buster_x86_metadata_relax_tls(actual + alignment, capacity, model, -4);
                BUSTER_TEST(arguments, relaxed == (capacity >= size));
                if (capacity >= size) memcpy(expected + alignment, output, size);
                BUSTER_TEST(arguments, memcmp(actual, expected, sizeof(actual)) == 0);
            }
        }
    }
    // Every value at EVERY GD byte. Only relocation fields are unconstrained.
    for (u32 position = 0; position < 16; position += 1)
    {
        for (u32 value = 0; value < 256; value += 1)
        {
            u8 bytes[16];
            u8 before[16];
            memcpy(bytes, x86_tls_gd_oracle, sizeof(bytes));
            bytes[position] = (u8)value;
            memcpy(before, bytes, sizeof(bytes));
            bool field = (position >= 4 && position < 8) || position >= 12;
            bool valid = field || value == x86_tls_gd_oracle[position];
            bool relaxed = buster_x86_metadata_relax_tls(bytes, sizeof(bytes), BUSTER_X86_METADATA_TLS_GENERAL_DYNAMIC, -4);
            BUSTER_TEST(arguments, relaxed == valid);
            BUSTER_TEST(arguments, memcmp(bytes, valid ? x86_tls_le_oracle : before, sizeof(bytes)) == 0);
        }
    }
    // The entire REX x ModRM domain. Match the independent oracle rows rather
    // than deriving register numbers with the production encoder's formulas.
    for (u32 rex = 0; rex < 256; rex += 1)
    {
        for (u32 modrm = 0; modrm < 256; modrm += 1)
        {
            u8 bytes[7] = {(u8)rex, 0x03, (u8)modrm, 0x12, 0x34, 0x56, 0x78};
            u8 before[7];
            memcpy(before, bytes, sizeof(bytes));
            u32 expected_reg = UINT32_MAX;
            for (u32 reg = 0; reg < 16; reg += 1)
            {
                for (u32 ignored = 0; ignored < 4; ignored += 1)
                {
                    if (rex == ((u32)x86_tls_ie_oracle[reg][0] | ignored) && modrm == x86_tls_ie_oracle[reg][2])
                        expected_reg = reg;
                }
            }
            bool relaxed = buster_x86_metadata_relax_tls(bytes, sizeof(bytes), BUSTER_X86_METADATA_TLS_INITIAL_EXEC, -4);
            BUSTER_TEST(arguments, relaxed == (expected_reg != UINT32_MAX));
            BUSTER_TEST(arguments, memcmp(bytes, expected_reg != UINT32_MAX ? x86_tls_add_oracle[expected_reg] : before, sizeof(bytes)) == 0);
        }
    }
    for (u32 reg = 0; reg < 16; reg += 1)
    {
        for (u32 opcode = 0; opcode < 256; opcode += 1)
        {
            u8 bytes[7];
            u8 before[7];
            memcpy(bytes, x86_tls_ie_oracle[reg], sizeof(bytes));
            bytes[1] = (u8)opcode;
            memcpy(before, bytes, sizeof(bytes));
            bool valid = opcode == x86_tls_ie_oracle[reg][1];
            BUSTER_TEST(arguments, buster_x86_metadata_relax_tls(bytes, sizeof(bytes), BUSTER_X86_METADATA_TLS_INITIAL_EXEC, -4) == valid);
            BUSTER_TEST(arguments, memcmp(bytes, valid ? x86_tls_add_oracle[reg] : before, sizeof(bytes)) == 0);
        }
    }
    // Exhaust four separate field-byte domains plus signed/disp8 boundaries
    // and mixed patterns. This deliberately does NOT claim all 2^32 values.
    static s32 const boundaries[] = {INT32_MIN, -129, -128, -1, 0, 127, 128, INT32_MAX, 0x12345678, -1431655766};
    for (u32 value_index = 0; value_index < 1024 + BUSTER_ARRAY_LENGTH(boundaries); value_index += 1)
    {
        s32 value = value_index < 1024 ? (s32)((value_index & 255u) << ((value_index >> 8) * 8)) : boundaries[value_index - 1024];
        u8 bytes[16];
        memcpy(bytes, x86_tls_gd_oracle, sizeof(bytes));
        BUSTER_TEST(arguments, buster_x86_metadata_relax_tls(bytes, sizeof(bytes), BUSTER_X86_METADATA_TLS_GENERAL_DYNAMIC, value));
        BUSTER_TEST(arguments, memcmp(bytes, x86_tls_le_oracle, 12) == 0 && x86_tls_test_value(bytes, 16, value));
        for (u32 reg = 0; reg < 16; reg += 1)
        {
            for (u32 ignored = 0; ignored < 4; ignored += 1)
            {
                memcpy(bytes, x86_tls_ie_oracle[reg], 7);
                bytes[0] |= (u8)ignored;
                // Arbitrary input fixups are independent of the output value.
                memcpy(bytes + 3, &value, sizeof(value));
                BUSTER_TEST(arguments, buster_x86_metadata_relax_tls(bytes, 7, BUSTER_X86_METADATA_TLS_INITIAL_EXEC, value));
                BUSTER_TEST(arguments, memcmp(bytes, x86_tls_add_oracle[reg], 3) == 0 && x86_tls_test_value(bytes, 7, value));
            }
        }
    }
    u8 invalid[16];
    memcpy(invalid, x86_tls_gd_oracle, sizeof(invalid));
    BUSTER_TEST(arguments, !buster_x86_metadata_relax_tls(invalid, sizeof(invalid), (BusterX86MetadataTlsModel)99, 0));
    BUSTER_TEST(arguments, memcmp(invalid, x86_tls_gd_oracle, sizeof(invalid)) == 0);
    return result;
}
#endif
