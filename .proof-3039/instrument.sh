#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# In a disposable checkout only. Count the two full-table copies separately
# from the eight payload bytes each radix scatter writes.
set -euo pipefail
file=src/buster/compiler/executable_impl.c
sed -i '/^static bool executable_eh_frame_sort(/,/^}/ {
    /    const __m512i evens =/i\    const uint64_t mark = arena->length; uint64_t copied = 0, scattered = 0;
    /memcpy(from, table, 8 \* fdes);/a\            copied += 8 * fdes;
    /memcpy(table, from, 8 \* fdes);/a\            copied += 8 * fdes;
    /to\[starts\[digit\]\] = from\[e\];/a\                scattered += 8;
    /memcpy(to + .*starts\[digit\]/a\                scattered += 8;
    /    return result;/i\    if (getenv("FDE_TRACE")) fprintf(stderr, "FDE n=%llu sorted=%d exact=%d ok=%d arena=%llu copy=%llu scatter=%llu\\n", (unsigned long long)fdes, sorted, exact, result, (unsigned long long)(arena->length-mark), (unsigned long long)copied, (unsigned long long)scattered);
}' "$file"
test "$(grep -c 'scattered += 8;' "$file")" -eq 1
