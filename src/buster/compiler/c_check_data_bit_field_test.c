// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent per-bit oracle for static bit-field serialization. Included by c_check_data_test.c.
// The guarded helper checks cover both allocation edges; checked plans also cover ordered overlaps and fixups.
#ifndef BUSTER_C_CHECK_DATA_BIT_FIELD_TEST_INCLUDED
#define BUSTER_C_CHECK_DATA_BIT_FIELD_TEST_INCLUDED

static void check_data_test_bit_field_reference(uint8_t* bytes, uint32_t offset, uint32_t width, unsigned __int128 value, bool clear)
{
    for (uint32_t bit = 0; bit < width; bit += 1)
    {
        const uint32_t destination = offset + bit;
        const uint8_t mask = (uint8_t)(1u << (destination % 8));
        const bool set = !clear && ((value >> bit) & 1) != 0;
        if (set)
        {
            bytes[destination / 8] |= mask;
        }
        else
        {
            bytes[destination / 8] &= (uint8_t)~mask;
        }
    }
}

static void check_data_test_bit_field_tails(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
    check_test_expect(test, ready, "static bit-field guarded destination storage");
    uint64_t random = UINT64_C(0x6019e3779b97f4a7);
    for (uint32_t width = 0; ready && width <= 128; width += 1)
    {
        for (uint32_t shift = 0; shift < 8; shift += 1)
        {
            const uint32_t size = width != 0 ? (shift + width + 7) / 8 : 0;
            for (uint32_t pattern = 0; pattern < 8; pattern += 1)
            {
                random = random * UINT64_C(6364136223846793005) + 1;
                unsigned __int128 value = ((unsigned __int128)random << 64) | (random ^ UINT64_C(0xa5a55a5af00f0ff0));
                if (pattern == 0)
                {
                    value = 0;
                }
                else if (pattern == 1)
                {
                    value = ~(unsigned __int128)0;
                }
                else if (pattern == 2)
                {
                    value = (unsigned __int128)1 << (width != 0 ? width - 1 : 0);
                }
                for (uint32_t edge = 0; edge < 2; edge += 1)
                {
                    uint8_t* bytes = edge == 0 ? mapping + page : mapping + page * 2 - size;
                    uint8_t expected[17];
                    memset(expected, (pattern & 1) != 0 ? 0xa5 : 0x5a, sizeof(expected));
                    memset(mapping + page, 0x69, page);
                    memcpy(bytes, expected, size);
                    check_data_test_bit_field_reference(expected, shift, width, value, false);
                    check_data_bit_field_value(bytes, shift, width, value);
                    const bool outside = edge == 0 ? bytes[size] == 0x69 : bytes[-1] == 0x69;
                    check_test_expect(test, memcmp(bytes, expected, size) == 0 && outside,
                                      "static bit-field value matches independent bits at both guarded edges for every width and shift");
                    check_data_test_bit_field_reference(expected, shift, width, 0, true);
                    check_data_bit_field_clear(bytes, shift, width);
                    check_test_expect(test, memcmp(bytes, expected, size) == 0,
                                      "static bit-field clear preserves both boundary bytes for every scalar width and shift");
                }
            }
        }
    }
    for (uint32_t width = 129; ready && width <= 2049; width += 1)
    {
        for (uint32_t shift = 0; shift < 8; shift += 1)
        {
            const uint32_t size = (shift + width + 7) / 8;
            uint8_t expected[257];
            memset(expected, 0xa5, sizeof(expected));
            uint8_t* bytes = mapping + page * 2 - size;
            memset(bytes, 0xa5, size);
            bytes[-1] = 0x69;
            check_data_test_bit_field_reference(expected, shift, width, 0, true);
            check_data_bit_field_clear(bytes, shift, width);
            check_test_expect(test, memcmp(bytes, expected, size) == 0 && bytes[-1] == 0x69,
                              "static bit-field clear handles ranges beyond 128 bits with an exact guarded tail");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 3);
    }
}

static void check_data_test_bit_field_plans(CheckTest* test, TypeTable* types)
{
    CheckDataTestLink fixture;
    const bool ready = check_data_test_open(&fixture, 8);
    check_test_expect(test, ready, "static bit-field ordered plan storage");
    if (ready)
    {
        const TypeId array = type_array(types, BUSTER_TYPE_UCHAR, 40);
        const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
        const TypeId signed_bit65 = type_bit_int(types, 65, false);
        const SymbolId map[] = {
            0,
            check_data_test_symbol(&fixture.link, "fields", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL),
            check_data_test_symbol(&fixture.link, "target", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL),
        };
        for (uint32_t width = 1; width <= 128; width += 1)
        {
            for (uint32_t shift = 0; shift < 8; shift += 1)
            {
                uint8_t expected[40];
                memset(expected, 0xa5, sizeof(expected));
                const TypeId type = width <= 65 ? signed_bit65 : BUSTER_TYPE_INT128;
                const unsigned __int128 value = width <= 65 ? ((unsigned __int128)1 << 65) - 3 : ~(unsigned __int128)2;
                CheckValue first_value = check_test_integer(type, value);
                unsigned __int128 first_bits = value;
                if (width <= 64 && (shift & 1) != 0)
                {
                    first_value = (CheckValue){
                        .type = BUSTER_TYPE_ULONG,
                        .category = CHECK_RVALUE,
                        .constant_kind = CHECK_CONSTANT_ADDRESS,
                        .integer = 1,
                        .offset = -18,
                    };
                    first_bits = (uint64_t)first_value.offset;
                }
                CheckInitPart parts[] = {
                    {
                        .kind = CHECK_INIT_BYTES,
                        .type = array,
                        .bytes = expected,
                        .byte_count = sizeof(expected),
                    },
                    {
                        .kind = CHECK_INIT_VALUE,
                        .type = first_value.type,
                        .bit_offset = 32 + shift,
                        .bit_width = width,
                        .value = first_value,
                    },
                    {
                        .kind = CHECK_INIT_CLEAR,
                        .type = array,
                        .bit_offset = 35 + shift,
                        .bit_width = width + 3,
                    },
                    {
                        .kind = CHECK_INIT_VALUE,
                        .type = BUSTER_TYPE_UINT128,
                        .bit_offset = 33 + shift,
                        .bit_width = width,
                        .value = check_test_integer(BUSTER_TYPE_UINT128, value ^ (value >> 1)),
                    },
                    {
                        .kind = CHECK_INIT_VALUE,
                        .type = pointer,
                        .bit_offset = 256,
                        .value = {
                            .type = pointer,
                            .category = CHECK_RVALUE,
                            .flags = BUSTER_CHECK_STATIC_OBJECT,
                            .constant_kind = CHECK_CONSTANT_ADDRESS,
                            .object = 2,
                            .offset = 12,
                        },
                    },
                };
                const CheckInitializer initializer = {
                    .type = array,
                    .parts = parts,
                    .count = BUSTER_ARRAY_LENGTH(parts),
                };
                const CheckDataObject object = {
                    .type = array,
                    .object = 1,
                    .initializer = BUSTER_CHECK_NONE,
                    .initializer_plan = &initializer,
                };
                CheckDataPlan plan;
                const CheckDataStatus status = check_data_plan_object(types, NULL, &object, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, &fixture.scratch, &plan);
                check_data_test_bit_field_reference(expected, 32 + shift, width, first_bits, false);
                check_data_test_bit_field_reference(expected, 35 + shift, width + 3, 0, true);
                check_data_test_bit_field_reference(expected, 33 + shift, width, value ^ (value >> 1), false);
                memset(expected + 32, 0, 8);
                const bool fixup = plan.fixup_count == 1 && plan.fixups[0].offset == 32 && plan.fixups[0].target == map[2] && plan.fixups[0].addend == 12;
                check_test_expect(test, status == CHECK_DATA_OK && plan.size == sizeof(expected) && memcmp(plan.bytes, expected, sizeof(expected)) == 0 && fixup,
                                  "static bit-field plans preserve ordered signed/wide overlapping writes and an adjacent relocation");
                arena_pop_to(&fixture.scratch, 0);
            }
        }
    }
    check_data_test_close(&fixture);
}

static void check_data_test_bit_fields(CheckTest* test, TypeTable* types)
{
    check_data_test_bit_field_tails(test);
    check_data_test_bit_field_plans(test, types);
}

#endif
