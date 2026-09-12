// Diagnostic caller for #113; build against each frozen checkout.
#define main build_main
#include "build.c"
#undef main

int main(void)
{
    const u64 counts[] = {4096, 8192, 16383};
    int status = 0;
    for (u64 case_i = 0; case_i < BUSTER_ARRAY_LENGTH(counts); case_i += 1)
    {
        u64 count = counts[case_i];
        char16 command_line[32767];
        for (u64 i = 0; i < count; i += 1)
        {
            command_line[2 * i] = (char16)('a' + i % 26);
            command_line[2 * i + 1] = i & 1 ? '\t' : ' ';
        }
        command_line[2 * count] = 0;
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
        u64 before = arena->position;
        SliceString8 parts = slice_string_from_windows_string_list(arena, command_line);
        u64 peak = arena_dirty_position(arena) - before;
        bool valid = parts.length == count;
        for (u64 i = 0; i < parts.length && valid; i += 1)
        {
            valid = parts.pointer[i].length == 1 && parts.pointer[i].pointer[0] == 'a' + i % 26 && parts.pointer[i].pointer[1] == 0;
        }
        printf("arguments=%llu utf16_units=%llu before=%llu after=%llu peak_growth=%llu valid=%d\n",
               (unsigned long long)count, (unsigned long long)(2 * count), (unsigned long long)before,
               (unsigned long long)arena->position, (unsigned long long)peak, valid);
        status |= !valid;
        status |= !arena_destroy(arena, 1);
    }
    return status;
}
