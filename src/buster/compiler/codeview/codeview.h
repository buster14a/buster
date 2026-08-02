#pragma once

#include <buster/base.h>
#include <buster/arena.h>
#include <buster/compiler/debug/debug.h>
// CodeView consumes the same neutral function and line descriptors the DWARF
// emitter uses; the two emitters are alternative backends over one input.
#include <buster/compiler/dwarf/dwarf.h>

typedef enum CodeviewRelocationKind
{
    CODEVIEW_RELOCATION_SECREL32,
    CODEVIEW_RELOCATION_SECTION16,
    CODEVIEW_RELOCATION_COUNT,
} CodeviewRelocationKind;

// Every relocation lives in the symbols (.debug$S) section and resolves
// against the function's own object symbol: SECREL32 slots receive the
// section-relative address and SECTION16 slots the 1-based section index.
typedef struct CodeviewRelocation CodeviewRelocation;
struct CodeviewRelocation
{
    u64 offset;
    u32 function;
    CodeviewRelocationKind kind;
    String8 symbol_name;
};

typedef struct CodeviewInput CodeviewInput;
struct CodeviewInput
{
    DebugModel* model;
    String8 producer;
    String8* file_paths;
    DwarfFunction* functions;
    DwarfLineEntry* lines;
    u32 file_count;
    u32 function_count;
    u32 line_count;
    u16 machine;
    u8 reserved[2];
};

typedef struct CodeviewResult CodeviewResult;
struct CodeviewResult
{
    ByteSlice symbols;
    ByteSlice types;
    CodeviewRelocation* relocations;
    u32 relocation_count;
    bool valid;
    u8 reserved[3];
};

enum
{
    CODEVIEW_MACHINE_X64 = 0xd0,
    CODEVIEW_MACHINE_ARM64 = 0xf6,
};

BUSTER_F_DECL CodeviewResult codeview_build(Arena* arena, CodeviewInput input);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult codeview_tests(UnitTestArguments* arguments);
#endif
