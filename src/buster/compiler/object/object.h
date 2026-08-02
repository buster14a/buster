#pragma once

#include <buster/compiler/codegen/codegen.h>
#include <buster/compiler/dwarf/dwarf.h>
#include <buster/compiler/codeview/codeview.h>

typedef enum ObjectFormat
{
    OBJECT_FORMAT_ELF64,
    OBJECT_FORMAT_COFF,
    OBJECT_FORMAT_MACH_O64,
    OBJECT_FORMAT_COUNT,
} ObjectFormat;

typedef enum ObjectError
{
    OBJECT_ERROR_NONE,
    OBJECT_ERROR_INVALID_INPUT,
    OBJECT_ERROR_UNSUPPORTED_TARGET,
    OBJECT_ERROR_CAPACITY,
    OBJECT_ERROR_UNRESOLVED_SYMBOL,
    OBJECT_ERROR_EXECUTABLE_MEMORY,
    OBJECT_ERROR_COUNT,
} ObjectError;

typedef enum ObjectSectionKind
{
    OBJECT_SECTION_TEXT,
    OBJECT_SECTION_READ_ONLY_DATA,
    OBJECT_SECTION_DATA,
    OBJECT_SECTION_THREAD_LOCAL_DATA,
    OBJECT_SECTION_THREAD_LOCAL_ZERO,
    OBJECT_SECTION_DEBUG_INFO,
    OBJECT_SECTION_DEBUG_ABBREV,
    OBJECT_SECTION_DEBUG_LINE,
    OBJECT_SECTION_DEBUG_STR,
    OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
    OBJECT_SECTION_DEBUG_CODEVIEW_TYPES,
    OBJECT_SECTION_COUNT,
} ObjectSectionKind;

BUSTER_F_DECL bool object_section_kind_is_debug(ObjectSectionKind kind);
BUSTER_F_DECL String8 object_section_name_for_kind(ObjectSectionKind kind);
BUSTER_F_DECL u32 object_section_default_alignment(ObjectSectionKind kind);

typedef enum ObjectSymbolKind
{
    OBJECT_SYMBOL_FUNCTION,
    OBJECT_SYMBOL_DATA,
    OBJECT_SYMBOL_COUNT,
} ObjectSymbolKind;

typedef enum ObjectRelocationKind
{
    OBJECT_RELOCATION_X86_64_PC32,
    OBJECT_RELOCATION_AARCH64_CALL26,
    OBJECT_RELOCATION_ABSOLUTE64,
    OBJECT_RELOCATION_ABSOLUTE32,
    OBJECT_RELOCATION_COFF_SECREL32,
    OBJECT_RELOCATION_COFF_SECTION16,
    OBJECT_RELOCATION_X86_64_TPOFF32,
    OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32,
    OBJECT_RELOCATION_PE_TLS_OFFSET32,
    OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP,
    OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12,
    OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12,
    OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12,
    OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12,
    OBJECT_RELOCATION_X86_64_MACH_TLV_PC32,
    OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21,
    OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12,
    OBJECT_RELOCATION_COUNT,
} ObjectRelocationKind;

#define OBJECT_SECTION_UNDEFINED UINT32_MAX

typedef struct ObjectSection ObjectSection;
struct ObjectSection
{
    String8 name;
    ByteSlice data;
    u64 virtual_size;
    ObjectSectionKind kind;
    u32 alignment;
};

typedef struct ObjectSymbol ObjectSymbol;
struct ObjectSymbol
{
    String8 name;
    u64 value;
    u64 size;
    u32 section;
    ObjectSymbolKind kind;
    bool global;
    u8 reserved[3];
};

typedef struct ObjectRelocation ObjectRelocation;
struct ObjectRelocation
{
    s64 addend;
    u64 offset;
    u32 section;
    u32 symbol;
    ObjectRelocationKind kind;
};

typedef struct ObjectFile ObjectFile;
struct ObjectFile
{
    ObjectSection* sections;
    ObjectSymbol* symbols;
    ObjectRelocation* relocations;
    Target target;
    ObjectError error;
    u32 section_count;
    u32 symbol_count;
    u32 relocation_count;
};

typedef struct ObjectArtifact ObjectArtifact;
struct ObjectArtifact
{
    ByteSlice bytes;
    ObjectError error;
    ObjectFormat format;
};

typedef struct ObjectArchive ObjectArchive;
struct ObjectArchive
{
    ObjectFile* objects;
    String8* member_names;
    ObjectError error;
    u32 object_count;
    u32 reserved;
};

typedef struct ObjectExecutable ObjectExecutable;
struct ObjectExecutable
{
    void* address;
    u64 allocation_size;
    ObjectError error;
};

BUSTER_F_DECL ObjectFormat object_format_for_target(Target target);
BUSTER_F_DECL ObjectFile object_from_codegen_module(Arena* arena, AnalysisResult* analysis, CodegenModule* module, Target target);
BUSTER_F_DECL ObjectFile object_from_canonical_codegen_module(Arena* arena, IrProgram* program, CodegenModule* module, Target target);
BUSTER_F_DECL String8 object_print_assembly(Arena* arena, ObjectFile* object);
BUSTER_F_DECL ObjectArtifact object_write(Arena* arena, ObjectFile* object, ObjectFormat format);
BUSTER_F_DECL ObjectFile object_read(Arena* arena, ByteSlice bytes, Target target);
BUSTER_F_DECL ObjectArchive object_archive_read(Arena* arena, ByteSlice bytes, Target target);
BUSTER_F_DECL ObjectExecutable object_link_executable(ObjectFile* object);
BUSTER_F_DECL void object_release_executable(ObjectExecutable executable);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult object_tests(UnitTestArguments* arguments);
#endif
