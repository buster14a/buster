// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Black-box option-order checks for the terminal unwind pass. The original composition fixtures execute all live
// functions; the ELF's unwind section measures actual CIE removal. Include after the driver/composition helpers.

static bool foreign_driver_dedup_test(const char* directory, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = { .check = "foreign command unwind deduplication" };
    ForeignTestLine line = { 0 };
    const bool ready = foreign_unwind_dedup_link_test_compile(&tally, directory, &line, arena, false);
    String8 baseline[2] = { 0 };
    String8 pooled[2] = { 0 };
    uint64_t baseline_size[2] = { 0 };
    for (uint32_t mode = 0; ready && mode < 8; mode += 1)
    {
        char name[64];
        snprintf(name, sizeof(name), "unwind_option_%u", mode);
        char* arguments[BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS];
        uint32_t count = line.our_count;
        memcpy(arguments, line.ours, count * sizeof(char*));
        const uint32_t gc = mode >= 4 && mode <= 6 ? 1 : 0;
        if (gc != 0)
        {
            arguments[count++] = (char*)"--gc-sections";
        }
        if (mode == 1 || mode == 2 || mode == 5 || mode == 6 || mode == 7)
        {
            arguments[count++] = (char*)"--dedup-unwind";
        }
        if (mode == 2 || mode == 3 || mode == 4)
        {
            arguments[count++] = (char*)"--no-dedup-unwind";
        }
        if (mode == 3)
        {
            arguments[count++] = (char*)"--dedup-unwind";
        }
        if (mode == 6)
        {
            arguments[count++] = (char*)"--scalar";
        }
        if (mode == 7)
        {
            arguments[count++] = (char*)"--lanes";
            arguments[count++] = (char*)"8";
        }
        const String8 bytes = foreign_driver_test_invoke(&tally, directory, name, "-o", arguments, count, true, 42, arena);
        const ExecutableTestImage image = executable_test_image(bytes);
        const Elf64_Shdr* frames = executable_test_section(&image, ".eh_frame");
        foreign_test_expect(&tally, frames != 0 && frames->sh_size != 0, "driver output retains unwind records", name);
        if (mode == 0 || mode == 4)
        {
            baseline[gc] = bytes;
            baseline_size[gc] = frames != 0 ? frames->sh_size : 0;
        }
        else if (mode == 2)
        {
            foreign_test_expect(&tally, foreign_driver_test_same(baseline[0], bytes), "last disabling option preserves the default output", name);
        }
        else
        {
            foreign_test_expect(&tally, frames != 0 && frames->sh_size < baseline_size[gc], "enabled pass removes duplicate CIE bytes", name);
            if (pooled[gc].pointer == 0)
            {
                pooled[gc] = bytes;
            }
            else
            {
                foreign_test_expect(&tally, foreign_driver_test_same(pooled[gc], bytes), "option order, scalar path and lane count preserve complete pooled output", name);
            }
        }
    }
    arena_pop_to(arena, saved);
    return foreign_test_report(&tally);
}
