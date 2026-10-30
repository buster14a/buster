// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// A complete replacement for the default callback, selected with
// BUSTER_FUZZ_ENTRY=tests/fuzz/entry_override.c. Exercise the bounded ELF header
// reader directly, without command dispatch, temporary files or executing code.
int buster_fuzz_test_input(const uint8_t* data, size_t size)
{
    static bool entered;
    if (!entered)
    {
        fputs("Buster fuzz entry: ELF import\n", stderr);
        entered = true;
    }
    ImportElf elf = { 0 };
    const String8 bytes = { .pointer = (char*)data, .length = size };
    (void)import_elf(&elf, bytes, ET_REL);
    (void)import_elf(&elf, bytes, ET_DYN);
    return 0;
}
