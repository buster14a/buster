#pragma once

#include <buster/compiler/codegen/codegen.h>

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
    OBJECT_SECTION_COUNT,
} ObjectSectionKind;

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
    OBJECT_RELOCATION_COUNT,
} ObjectRelocationKind;

#define OBJECT_SECTION_UNDEFINED UINT32_MAX

typedef struct ObjectSection ObjectSection;
struct ObjectSection
{
    String8 name;
    ByteSlice data;
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

typedef struct ObjectExecutable ObjectExecutable;
struct ObjectExecutable
{
    void* address;
    u64 allocation_size;
    ObjectError error;
};

BUSTER_F_DECL ObjectFormat object_format_for_target(Target target);
BUSTER_F_DECL ObjectFile object_from_codegen_module(
    Arena* arena,
    AnalysisResult* analysis,
    CodegenModule* module,
    Target target);
BUSTER_F_DECL ObjectArtifact object_write(
    Arena* arena,
    ObjectFile* object,
    ObjectFormat format);
BUSTER_F_DECL ObjectExecutable object_link_executable(
    ObjectFile* object);
BUSTER_F_DECL void object_release_executable(
    ObjectExecutable executable);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult object_tests(
    UnitTestArguments* arguments);
#endif
