#pragma once

#include <buster/lib/base.h>
#include <buster/lib/arena.h>
#include <buster/lib/compiler/debug/debug.h>
// CodeView consumes the same neutral function and line descriptors the DWARF
// emitter uses; the two emitters are alternative backends over one input.
#include <buster/lib/compiler/dwarf/dwarf.h>

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
    // Function-relative byte offset a SECREL32 slot adds to its symbol. The
    // same value is stored in the slot (COFF in-place addend); consumers that
    // overwrite the slot must take it from here.
    u32 addend;
    String8 symbol_name;
    // The program symbol a named relocation refers to; meaningful only with
    // `symbol_name`, which remains the spelling-based fallback.
    IrSymbolId symbol;
    u32 reserved;
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
    bool record_function_ranges;
    u8 reserved[1];
};

// Consecutive function symbols/lines subsections in the shared symbols image.
// Ranges exclude its C13 signature; split contributions add their own signature.
typedef struct CodeviewFunctionRange CodeviewFunctionRange;
struct CodeviewFunctionRange
{
    u64 offset;
    u64 size;
};

typedef struct CodeviewResult CodeviewResult;
struct CodeviewResult
{
    ByteSlice symbols;
    ByteSlice types;
    CodeviewRelocation* relocations;
    CodeviewFunctionRange* functions;
    u32 function_count;
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
