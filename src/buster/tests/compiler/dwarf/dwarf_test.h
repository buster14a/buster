#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/debug/debug.h>

enum
{
    DWARF_TEST_VERSION = 4,
    DWARF_TEST_ADDRESS_SIZE = 8,
    DWARF_TEST_LNS_COPY = 1,
    DWARF_TEST_LNS_ADVANCE_PC = 2,
    DWARF_TEST_LNS_ADVANCE_LINE = 3,
    DWARF_TEST_LNS_SET_FILE = 4,
    DWARF_TEST_LNS_SET_COLUMN = 5,
    DWARF_TEST_LNE_END_SEQUENCE = 1,
    DWARF_TEST_LNE_SET_ADDRESS = 2,
};

typedef struct DwarfLineRow DwarfLineRow;
struct DwarfLineRow
{
    u64 address;
    u32 file;
    u32 line;
    u32 column;
    bool end_sequence;
    u8 reserved[3];
};

BUSTER_TEST_F_DECL bool dwarf_line_lookup(ByteSlice debug_line, u64 address, DwarfLineRow* row);

BUSTER_TEST_F_DECL UnitTestResult dwarf_tests(UnitTestArguments* arguments);
