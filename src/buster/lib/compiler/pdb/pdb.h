#pragma once

#include <buster/lib/base.h>
#include <buster/lib/arena.h>

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

// Exact section-relative range owned by one module. Entries keep caller order;
// gaps and unrelated image sections are never attributed to that module.
typedef struct PdbContribution PdbContribution;
struct PdbContribution
{
    u32 section;
    u32 offset;
    u32 size;
};

typedef struct PdbModule PdbModule;
struct PdbModule
{
    String8 name;
    ByteSlice codeview_symbols;
    ByteSlice codeview_types;
    u32 code_offset;
    u32 code_size;
    u32 code_section;
    u8 reserved[4];
    // When present, this list is authoritative. The legacy code range above is
    // normalized into one entry only when no list is supplied. An empty module
    // has no entries and code_size == 0.
    PdbContribution const* contributions;
    u32 contribution_count;
    u8 contribution_reserved[4];
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
    PdbModule* modules;
    u32 module_count;
};

typedef struct PdbResult PdbResult;
struct PdbResult
{
    ByteSlice bytes;
    bool valid;
    u8 reserved[7];
};

BUSTER_F_DECL PdbResult pdb_build(Arena* arena, PdbInput input);
