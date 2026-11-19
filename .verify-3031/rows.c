// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// A DebugUnit can have several rows at one address. The ordinary C frontend
// coalesces zero-length source events. Drive the same producer on that input
// and expose its untouched bytes and its published address slots to GNU as.
#include <immintrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "string8.h"
#define BUSTER_S8(s) ((String8){ .pointer = (char*)(s), .length = sizeof(s) - 1 })
#include "debug.c"

int main(void)
{
    const String8 files[] = { BUSTER_S8("alpha.c"), BUSTER_S8("beta.c") };
    const DebugFunction functions[] = {
        { .name = BUSTER_S8("first"), .size = 96, .row_count = 12, .line = 6, .prologue_end = 2 },
        { .name = BUSTER_S8("second"), .size = 4, .first_row = 12, .row_count = 2, .file = 1, .line = 1 },
    };
    const uint32_t offsets[] = { 0, 0, 16, 33, 50, 68, 68, 68, 69, 85, 85, 85, 0, 1 };
    const uint32_t lines[] = { 6, 1, 9, 8, 8, 3, 12, 6, 0, 8, 3, 3, 1, 2 };
    const uint32_t row_files[] = { 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0 };
    const DebugUnit unit = {
        .directory = BUSTER_S8("."),
        .files = files,
        .file_count = 2,
        .functions = functions,
        .function_count = 2,
        .offsets = offsets,
        .lines = lines,
        .row_files = row_files,
        .row_count = sizeof(lines) / sizeof(lines[0]),
    };
    uint32_t slots[2];
    uint8_t bytes[BUSTER_DEBUG_SECTIONS][1024];
    DebugOutput output = { .line_addresses = slots, .address_capacity = 2 };
    for (uint32_t i = 0; i < BUSTER_DEBUG_SECTIONS; i += 1)
    {
        output.sections[i].bytes = bytes[i];
        output.sections[i].capacity = sizeof(bytes[i]);
    }
    const bool valid = debug_write(&unit, &output, false) == DEBUG_OK;
    if (valid)
    {
        puts(".text\n.globl first,second\n.type first,@function\nfirst: .fill 96,1,0x90\n.size first,.-first\n"
             ".type second,@function\nsecond: .fill 4,1,0x90\n.size second,.-second\n.section .note.GNU-stack,\"\",@progbits");
        const char* const names[] = { ".debug_info", ".debug_abbrev", ".debug_line", ".debug_addr", ".debug_rnglists" };
        for (uint32_t i = 0; i < BUSTER_DEBUG_SECTIONS; i += 1)
        {
            printf(".section %s,\"\",@progbits\n.Lsection%u:\n", names[i], i);
            for (uint64_t j = 0; j < output.sections[i].size; j += 1)
            {
                printf(".byte %u\n", bytes[i][j]);
            }
        }
        for (uint32_t i = 0; i < 2; i += 1)
        {
            printf(".section .debug_line\n.reloc .Lsection%u+%u,R_X86_64_64,%s\n", DEBUG_SECTION_LINE, slots[i], functions[i].name.pointer);
            printf(".section .debug_addr\n.reloc .Lsection%u+%u,R_X86_64_64,%s\n", DEBUG_SECTION_ADDR, 8 + 8 * i, functions[i].name.pointer);
        }
    }
    return valid ? 0 : 1;
}
