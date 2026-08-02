#pragma once

#include <buster/base.h>
#include <buster/arena.h>

// One image section, used for the section headers stream and for the module's
// section contribution entries.
typedef struct PdbSection PdbSection;
struct PdbSection
{
    String8 name;
    u32 virtual_address;
    u32 virtual_size;
    u32 raw_size;
    u32 raw_offset;
    u32 characteristics;
};

typedef struct PdbInput PdbInput;
struct PdbInput
{
    String8 module_name;
    // CodeView C13 blob as emitted into .debug$S, with its SECREL32/SECTION
    // slots already resolved to image values.
    ByteSlice codeview_symbols;
    PdbSection* sections;
    u32 section_count;
    // Identity shared with the image's RSDS debug directory entry.
    u8 guid[16];
    u32 age;
    u32 code_section;
    u32 code_size;
    u16 machine;
    u8 reserved[2];
};

typedef struct PdbResult PdbResult;
struct PdbResult
{
    ByteSlice bytes;
    bool valid;
    u8 reserved[7];
};

BUSTER_F_DECL PdbResult pdb_build(Arena* arena, PdbInput input);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult pdb_tests(UnitTestArguments* arguments);
#endif
