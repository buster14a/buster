#pragma once

// The assembly translation unit: assembly_unit_encode takes the complete text
// of a `.s` file and returns sections, symbols, relocations, and structured
// diagnostics. assembly.h's assembly_encode is the instruction layer beneath
// it -- this file adds what a whole file has that a single statement does not:
// a directive vocabulary, several sections, labels with offsets in them, and
// local numeric labels.
//
// Everything the vocabulary does not cover is a diagnostic naming the
// directive and its line; nothing is silently dropped.

#include <buster/lib/compiler/assembly/assembly.h>

typedef enum AssemblyUnitSectionKind
{
    ASSEMBLY_UNIT_SECTION_TEXT,
    ASSEMBLY_UNIT_SECTION_READ_ONLY_DATA,
    ASSEMBLY_UNIT_SECTION_DATA,
    ASSEMBLY_UNIT_SECTION_ZERO,
    ASSEMBLY_UNIT_SECTION_KIND_COUNT,
} AssemblyUnitSectionKind;

#define ASSEMBLY_UNIT_SECTION_UNDEFINED UINT32_MAX

typedef struct AssemblyUnitSection AssemblyUnitSection;
struct AssemblyUnitSection
{
    String8 name;
    ByteSlice data;
    // Bytes the section occupies without storing them. Only a zero-fill
    // section has one; every other section's size is its data length.
    u64 zero_size;
    u32 alignment;
    AssemblyUnitSectionKind kind;
};

typedef struct AssemblyUnitSymbol AssemblyUnitSymbol;
struct AssemblyUnitSymbol
{
    String8 name;
    u64 value;
    u64 size;
    u32 section;
    bool defined;
    bool global;
    bool weak;
    bool hidden;
    // STT_FUNC rather than STT_OBJECT. `.type name,@function` is the only
    // producer; a label alone does not promote the kind.
    bool function;
    u8 reserved[3];
};

typedef struct AssemblyUnitRelocation AssemblyUnitRelocation;
struct AssemblyUnitRelocation
{
    s64 addend;
    u64 offset;
    u32 section;
    u32 symbol;
    AssemblyRelocationKind kind;
};

typedef struct AssemblyUnitResult AssemblyUnitResult;
struct AssemblyUnitResult
{
    AssemblyUnitSection* sections;
    AssemblyUnitSymbol* symbols;
    AssemblyUnitRelocation* relocations;
    AssemblyDiagnostic* diagnostics;
    u32 section_count;
    u32 symbol_count;
    u32 relocation_count;
    u32 diagnostic_count;
};

// Assembles one source buffer. A non-zero diagnostic_count means the unit was
// refused; the section and symbol arrays are then whatever had been built and
// must not be turned into an object.
BUSTER_F_DECL AssemblyUnitResult assembly_unit_encode(Arena* arena, String8 source, AssemblyEncodeOptions options);
