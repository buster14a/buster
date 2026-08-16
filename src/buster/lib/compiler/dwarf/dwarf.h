#pragma once

#include <buster/lib/base.h>
#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/model.h>
#include <buster/lib/target.h>

typedef struct DebugModel DebugModel;
typedef struct CodegenFunctionDescriptor CodegenFunctionDescriptor;

typedef enum DwarfSectionKind
{
    DWARF_SECTION_INFO,
    DWARF_SECTION_ABBREV,
    DWARF_SECTION_LINE,
    DWARF_SECTION_STR,
    DWARF_SECTION_LOC,
    DWARF_SECTION_RANGES,
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

// Mirrors CodegenLineEntry's 12-byte shape; file and column arrive already
// saturated to u16 by codegen_record_line.
typedef struct DwarfLineEntry DwarfLineEntry;
struct DwarfLineEntry
{
    u32 code_offset;
    u32 line;
    u16 file;
    u16 column;
};

BUSTER_CT_CHECK(sizeof(DwarfLineEntry) == 12);

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
    String8 symbol_name;
    IrSymbolId symbol;
    bool address;
    bool symbol_address;
    u8 reserved[2];
};

typedef struct DwarfInput DwarfInput;
struct DwarfInput
{
    DebugModel* model;
    Target target;
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

typedef struct DwarfCfiRelocation DwarfCfiRelocation;
struct DwarfCfiRelocation
{
    u64 offset;
    u32 function;
    u32 reserved;
};

typedef struct DwarfCfiInput DwarfCfiInput;
struct DwarfCfiInput
{
    CodegenFunctionDescriptor* functions;
    Target target;
    u32 function_count;
    u32 reserved;
};

typedef struct DwarfCfiResult DwarfCfiResult;
struct DwarfCfiResult
{
    ByteSlice bytes;
    DwarfCfiRelocation* relocations;
    u32 relocation_count;
    bool valid;
    u8 reserved[3];
};

BUSTER_F_DECL DwarfResult dwarf_build(Arena* arena, DwarfInput input);
BUSTER_F_DECL DwarfCfiResult dwarf_cfi_build(Arena* arena, DwarfCfiInput input);
