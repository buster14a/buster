// Exercise the shared writer against literal bytes and independent range
// arithmetic, including publication and inert writes after a failed operation.
#include <buster/tests/byte_writer_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/byte_writer.h>
#include <string.h>

UnitTestResult byte_writer_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u8 bytes[20];
    u8 expected[20];
    u8 source[] = {0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28};
    ByteSlice output = {.pointer = source, .length = sizeof(source)};
    ByteWriter empty = byte_writer_make(0, 0);
    byte_writer_emit_bytes(&empty, 0, 0);
    byte_writer_emit_zero(&empty, 0);
    byte_writer_patch_bytes(&empty, 0, 0, 0);
    byte_writer_align4(&empty);
    BUSTER_TEST(arguments, byte_writer_commit(&empty, &output) && !output.pointer && !output.length);
    BUSTER_TEST(arguments, !empty.overflow && !empty.count);

    // Integer fields and patches must be little-endian even at unaligned
    // offsets. Alignment fills exactly its padding and preserves both guards.
    memset(bytes, 0xa5, sizeof(bytes));
    ByteWriter writer = byte_writer_make(bytes + 1, 12);
    byte_writer_emit_u8(&writer, 0x7f);
    byte_writer_emit_u16_le(&writer, 0x1234);
    byte_writer_emit_u32_le(&writer, 0x89abcdefu);
    byte_writer_align4(&writer);
    byte_writer_emit_u32_le(&writer, 0x10203040u);
    u8 const fields[] = {0x7f, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x89, 0, 0x40, 0x30, 0x20, 0x10};
    BUSTER_TEST(arguments, !writer.overflow && writer.count == sizeof(fields) && !memcmp(bytes + 1, fields, sizeof(fields)));
    byte_writer_patch_u16_le(&writer, 1, 0x5678);
    byte_writer_patch_u32_le(&writer, 8, 0xfedcba98u);
    u8 const patched[] = {0x7f, 0x78, 0x56, 0xef, 0xcd, 0xab, 0x89, 0, 0x98, 0xba, 0xdc, 0xfe};
    BUSTER_TEST(arguments, !memcmp(bytes + 1, patched, sizeof(patched)) && bytes[0] == 0xa5 && bytes[13] == 0xa5);
    BUSTER_TEST(arguments, byte_writer_commit(&writer, &output) && output.pointer == bytes + 1 && output.length == 12);

    // A commit is a borrowed view. Failure never overwrites a previously
    // published output, and even a later valid patch cannot mutate the prefix.
    memcpy(expected, bytes, sizeof(bytes));
    byte_writer_emit_u16_le(&writer, 0);
    byte_writer_emit_u8(&writer, 0);
    byte_writer_emit_u32_le(&writer, 0);
    byte_writer_emit_zero(&writer, 1);
    byte_writer_emit_bytes(&writer, source, 1);
    byte_writer_patch_u16_le(&writer, 0, 0);
    byte_writer_patch_u32_le(&writer, 0, 0);
    byte_writer_align4(&writer);
    BUSTER_TEST(arguments, writer.overflow && writer.count == 12 && !memcmp(bytes, expected, sizeof(bytes)));
    output = (ByteSlice){.pointer = source, .length = sizeof(source)};
    BUSTER_TEST(arguments, !byte_writer_commit(&writer, &output) && output.pointer == source && output.length == sizeof(source));

    // Exhaust every small patch range, including zero length at/past the
    // written end. Reserved-but-unwritten capacity is never patchable.
    for (u64 offset = 0; offset <= 9; offset += 1)
    {
        for (u64 size = 0; size <= 8; size += 1)
        {
            memset(bytes, 0xa5, sizeof(bytes));
            writer = byte_writer_make(bytes + 1, 12);
            byte_writer_emit_bytes(&writer, source, sizeof(source));
            memcpy(expected, bytes, sizeof(bytes));
            bool valid = offset + size <= sizeof(source);
            if (valid && size)
            {
                memcpy(expected + 1 + offset, source, size);
            }
            byte_writer_patch_bytes(&writer, offset, source, size);
            BUSTER_TEST(arguments, writer.overflow == !valid && writer.count == sizeof(source));
            BUSTER_TEST(arguments, !memcmp(bytes, expected, sizeof(bytes)));
        }
    }

    // Alignment is one bounded operation: a short tail cannot partially fill
    // or leave the old PDB byte-at-a-time loop spinning on a stationary count.
    for (u64 capacity = 0; capacity <= 12; capacity += 1)
    {
        for (u64 count = 0; count <= capacity; count += 1)
        {
            memset(bytes, 0xa5, sizeof(bytes));
            writer = byte_writer_make(bytes + 1, capacity);
            byte_writer_emit_zero(&writer, count);
            memcpy(expected, bytes, sizeof(bytes));
            u64 end = (count + 3) & ~(u64)3;
            bool valid = end <= capacity;
            if (valid)
            {
                memset(expected + 1 + count, 0, end - count);
            }
            byte_writer_align4(&writer);
            BUSTER_TEST(arguments, writer.overflow == !valid && writer.count == (valid ? end : count));
            BUSTER_TEST(arguments, !memcmp(bytes, expected, sizeof(bytes)));
        }
    }

    // Remaining-space arithmetic must reject enormous lengths before a copy,
    // including when adding the length to a nonzero cursor would wrap.
    u64 sizes[] = {0, 1, 2, 4, 8, UINT64_MAX};
    for (u64 capacity = 0; capacity <= 8; capacity += 1)
    {
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(sizes); index += 1)
        {
            memset(bytes, 0xa5, sizeof(bytes));
            writer = byte_writer_make(bytes + 1, capacity);
            u64 count = capacity / 2;
            byte_writer_emit_zero(&writer, count);
            memcpy(expected, bytes, sizeof(bytes));
            u64 size = sizes[index];
            bool valid = size <= capacity - count;
            if (valid && size)
            {
                memcpy(expected + 1 + count, source, size);
            }
            byte_writer_emit_bytes(&writer, source, size);
            BUSTER_TEST(arguments, writer.overflow == !valid && writer.count == count + (valid ? size : 0));
            BUSTER_TEST(arguments, !memcmp(bytes, expected, sizeof(bytes)));
        }
    }

    memset(bytes, 0xa5, sizeof(bytes));
    memcpy(expected, bytes, sizeof(bytes));
    writer = byte_writer_make(bytes, sizeof(bytes));
    byte_writer_emit_zero(&writer, UINT64_MAX);
    BUSTER_TEST(arguments, writer.overflow && !writer.count && !memcmp(bytes, expected, sizeof(bytes)));
    writer = byte_writer_make(bytes, sizeof(bytes));
    byte_writer_patch_u32_le(&writer, UINT64_MAX, 1);
    BUSTER_TEST(arguments, writer.overflow && !writer.count && !memcmp(bytes, expected, sizeof(bytes)));
    writer = byte_writer_make(bytes, sizeof(bytes));
    byte_writer_emit_bytes(&writer, 0, 1);
    BUSTER_TEST(arguments, writer.overflow && !writer.count);
    writer = byte_writer_make(bytes, sizeof(bytes));
    byte_writer_patch_bytes(&writer, 0, 0, 1);
    BUSTER_TEST(arguments, writer.overflow && !writer.count);
    writer = byte_writer_make(0, 1);
    byte_writer_emit_u8(&writer, 1);
    BUSTER_TEST(arguments, writer.overflow && !writer.count);
    writer = (ByteWriter){.bytes = bytes, .count = UINT64_MAX, .capacity = UINT64_MAX};
    byte_writer_align4(&writer);
    BUSTER_TEST(arguments, writer.overflow && writer.count == UINT64_MAX && !memcmp(bytes, expected, sizeof(bytes)));
    writer = (ByteWriter){.bytes = bytes, .count = 2, .capacity = 1};
    byte_writer_emit_zero(&writer, 0);
    BUSTER_TEST(arguments, writer.overflow && writer.count == 2 && !memcmp(bytes, expected, sizeof(bytes)));
    return result;
}
#endif
