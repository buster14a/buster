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
    // `@init_array`/`@fini_array`/`@preinit_array` and the `T` flag: the
    // object model gives each its own ELF type or TLS flag.
    ASSEMBLY_UNIT_SECTION_INIT_ARRAY,
    ASSEMBLY_UNIT_SECTION_FINI_ARRAY,
    ASSEMBLY_UNIT_SECTION_THREAD_LOCAL_DATA,
    ASSEMBLY_UNIT_SECTION_THREAD_LOCAL_ZERO,
    // Known DWARF sections retain their nonallocated object identities.
    ASSEMBLY_UNIT_SECTION_DEBUG_INFO,
    ASSEMBLY_UNIT_SECTION_DEBUG_ABBREV,
    ASSEMBLY_UNIT_SECTION_DEBUG_LINE,
    ASSEMBLY_UNIT_SECTION_DEBUG_STR,
    ASSEMBLY_UNIT_SECTION_DEBUG_LOC,
    ASSEMBLY_UNIT_SECTION_DEBUG_RANGES,
    ASSEMBLY_UNIT_SECTION_DEBUG_ADDR,
    ASSEMBLY_UNIT_SECTION_DEBUG_STR_OFFSETS,
    ASSEMBLY_UNIT_SECTION_DEBUG_LINE_STR,
    ASSEMBLY_UNIT_SECTION_DEBUG_RNGLISTS,
    ASSEMBLY_UNIT_SECTION_DEBUG_LOCLISTS,
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
    // STT_FUNC rather than STT_OBJECT. `.type name,@function` sets it; the
    // finished unit also sets it on a defined global or weak label in an
    // executable section, which is an entry point the linker must call.
    bool function;
    // `.type` named this symbol's kind, as function or object.
    bool typed;
    // The ELF writer states STT_NOTYPE: a defined local label in an
    // executable section that no `.type` described, as GNU as writes it.
    // `function` stays what consumers of the unit read; this only changes
    // the symbol type a disassembler sees.
    bool untyped;
    u8 reserved[1];
};

typedef struct AssemblyUnitRelocation AssemblyUnitRelocation;
struct AssemblyUnitRelocation
{
    s64 addend;
    u64 offset;
    u32 section;
    u32 symbol;
    AssemblyRelocationKind kind;
    // Instruction provenance and retained PLT request use existing tail padding.
    bool plt;
    bool x86_branch;
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
