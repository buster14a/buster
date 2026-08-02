#pragma once

#include <buster/base.h>
#include <buster/arena.h>

typedef enum DwarfSectionKind
{
    DWARF_SECTION_INFO,
    DWARF_SECTION_ABBREV,
    DWARF_SECTION_LINE,
    DWARF_SECTION_STR,
    DWARF_SECTION_COUNT,
} DwarfSectionKind;

typedef struct DwarfFunction DwarfFunction;
struct DwarfFunction
{
    String8 name;
    u32 code_offset;
    u32 code_size;
    u32 file;
    u32 line;
};

typedef struct DwarfLineEntry DwarfLineEntry;
struct DwarfLineEntry
{
    u32 code_offset;
    u32 file;
    u32 line;
    u32 column;
};

// Address relocations are 64-bit slots that must receive the address of the
// object's text base plus the addend. Non-address relocations are 32-bit slots
// holding an offset into another debug section (target); they must be adjusted
// by that section's placement when objects are concatenated.
typedef struct DwarfRelocation DwarfRelocation;
struct DwarfRelocation
{
    s64 addend;
    u64 offset;
    DwarfSectionKind section;
    DwarfSectionKind target;
    bool address;
    u8 reserved[7];
};

typedef struct DwarfInput DwarfInput;
struct DwarfInput
{
    String8 producer;
    String8 comp_dir;
    String8* file_paths;
    DwarfFunction* functions;
    DwarfLineEntry* lines;
    u64 code_size;
    u32 file_count;
    u32 function_count;
    u32 line_count;
    u16 language;
    u8 reserved[2];
};

typedef struct DwarfResult DwarfResult;
struct DwarfResult
{
    ByteSlice sections[DWARF_SECTION_COUNT];
    DwarfRelocation* relocations;
    u32 relocation_count;
    bool valid;
    u8 reserved[3];
};

BUSTER_F_DECL DwarfResult dwarf_build(Arena* arena, DwarfInput input);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>

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

BUSTER_F_DECL bool dwarf_line_lookup(ByteSlice debug_line, u64 address, DwarfLineRow* row);
BUSTER_F_DECL UnitTestResult dwarf_tests(UnitTestArguments* arguments);
#endif
