// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// CI-only planted failure: proves the build-time entry override, --fuzz handoff,
// crash-artifact persistence and exact-input replay together.
int buster_fuzz_test_input(const uint8_t* data, size_t size)
{
    static const char trigger[] = "BUSTER_FUZZ_ENTRY_PROBE";
    if (size == sizeof(trigger) - 1 && memcmp(data, trigger, sizeof(trigger) - 1) == 0)
    {
        abort();
    }
    return 0;
}
