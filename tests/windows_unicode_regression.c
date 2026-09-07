// Opt-in Windows UTF-16 serialization regression. The serializers are portable,
// so Linux can check their exact buffers without mocking CreateProcessW.
// From the repository root, build and run with:
// clang -Isrc -Ibuild/generated -O1 -g -funsigned-char -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined tests/windows_unicode_regression.c -lm -ldl -lpthread -o /tmp/buster-unicode-regression && ASAN_OPTIONS=detect_leaks=0 /tmp/buster-unicode-regression
// This standalone runner is not part of test_all. Its arenas and thread context
// live until process exit; leak detection is disabled, not address/UB checking.

#define BUSTER_USE_GRAPHICS 0
#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#include <buster/lib/base.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <stdio.h>

BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState unicode_regression_program;
BUSTER_V_IMPL ProgramState* program_state = &unicode_regression_program;
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/os.c>
#include <buster/lib/string.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/float.c>

BUSTER_GLOBAL_LOCAL unsigned unicode_regression_failures;
BUSTER_GLOBAL_LOCAL unsigned unicode_regression_checks;

BUSTER_GLOBAL_LOCAL void unicode_regression_check(bool condition, char const* name)
{
    unicode_regression_checks += 1;
    if (!condition)
    {
        unicode_regression_failures += 1;
        fprintf(stderr, "FAIL: %s\n", name);
    }
}

BUSTER_GLOBAL_LOCAL void unicode_regression_check_strings(SliceString8 got, SliceString8 expected)
{
    unicode_regression_check(got.length == expected.length, "entry count");
    if (got.length == expected.length)
    {
        for (u64 i = 0; i < got.length; i += 1)
        {
            unicode_regression_check(string_equal(got.pointer[i], expected.pointer[i]), "entry contents");
        }
    }
}

int main(void)
{
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    String8 inputs[] = {
        S8(""), S8("ASCII"), S8("\xc3\xa9"), S8("\xe2\x82\xac"), S8("\xf0\x9f\x98\x80"),
        S8("x\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"),
    };
    char16 expected[][6] = {
        {0}, {'A', 'S', 'C', 'I', 'I'}, {0xe9}, {0x20ac}, {0xd83d, 0xde00}, {'x', 0xe9, 0x20ac, 0xd83d, 0xde00},
    };
    u64 lengths[] = {0, 5, 1, 1, 2, 5};
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(inputs) == BUSTER_ARRAY_LENGTH(expected));
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(inputs) == BUSTER_ARRAY_LENGTH(lengths));

    for (u32 dirty = 0; dirty < 2; dirty += 1)
    {
        for (u32 null_terminate = 0; null_terminate < 2; null_terminate += 1)
        {
            for (u32 pad = 0; pad < 2; pad += 1)
            {
                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
                {
                    u64 start = arena->position;
                    if (dirty)
                    {
                        memset(arena_allocate(arena, u8, 256), 0x5a, 256);
                        arena_set_position(arena, start);
                    }
                    arena_allocate(arena, u8, pad);
                    String16 got = string16_from_string8(arena, inputs[i], null_terminate);
                    unicode_regression_check(got.length == lengths[i], "UTF-16 length");
                    unicode_regression_check(!memcmp(got.pointer, expected[i], lengths[i] * sizeof(char16)), "UTF-16 units");
                    unicode_regression_check(arena_get_current_pointer(arena, char16) == got.pointer + got.length + null_terminate,
                                             "exact cursor");
                    if (null_terminate)
                    {
                        unicode_regression_check(got.pointer[got.length] == 0, "terminator");
                    }
                    unicode_regression_check(arena_dirty_position(arena) >= arena->position, "dirty cursor accounting");
                    if (dirty)
                    {
                        unicode_regression_check(arena_dirty_position(arena) >= start + 256, "dirty high-water retained");
                    }
                    *arena_allocate(arena, char16, 1) = 0x1234;
                    unicode_regression_check(got.pointer[got.length + null_terminate] == 0x1234, "next append location");
                    // Do not rewind: fresh cases must not reuse dirty storage.
                }
            }
        }
    }

    for (u32 dirty = 0; dirty < 2; dirty += 1)
    {
        u64 start = arena->position;
        memset(arena_allocate(arena, u8, 4096), dirty ? 0x5a : 0, 4096);
        arena_set_position(arena, start);
        String8 args[] = {
            S8("prog"), S8("\xc3\xa9"), S8("two \xe2\x82\xac"), S8("\xf0\x9f\x98\x80\"\\"), S8(""), S8("tail"),
        };
        char16* command = windows_string_list_from_slice_string(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(args));
        unicode_regression_check_strings(slice_string_from_windows_string_list(arena, command), (SliceString8)BUSTER_ARRAY_TO_SLICE(args));

        String8 env[] = {S8("NAME=\xc3\xa9"), S8("\xe2\x82\xac=\xf0\x9f\x98\x80"), S8("TAIL=yes")};
        start = arena->position;
        memset(arena_allocate(arena, u8, 4096), dirty ? 0x5a : 0, 4096);
        arena_set_position(arena, start);
        char16* block = windows_environment_block_from_slice_string(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(env));
        unicode_regression_check_strings(string16_environment_block_to_slice_string(arena, block), (SliceString8)BUSTER_ARRAY_TO_SLICE(env));

        String8 keys[] = {S8("NAME"), S8("\xe2\x82\xac"), S8("TAIL")};
        String8 values[] = {S8("\xc3\xa9"), S8("\xf0\x9f\x98\x80"), S8("yes")};
        start = arena->position;
        memset(arena_allocate(arena, u8, 4096), dirty ? 0x5a : 0, 4096);
        arena_set_position(arena, start);
        char16* kv = windows_environment_from_keys_and_values(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(keys),
                                                              (SliceString8)BUSTER_ARRAY_TO_SLICE(values));
        unicode_regression_check_strings(string16_environment_block_to_slice_string(arena, kv), (SliceString8)BUSTER_ARRAY_TO_SLICE(env));
    }

    printf("Unicode regression: %u/%u checks passed\n", unicode_regression_checks - unicode_regression_failures, unicode_regression_checks);
    return unicode_regression_failures != 0;
}
