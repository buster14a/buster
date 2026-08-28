#pragma once

// Format-neutral object model: the ObjectFile shape shared by the ELF64,
// COFF, and Mach-O readers/writers, the codegen-module converter, archive
// reading, the disassembly printer, and in-process execution of a linked
// object. Readers bounds-check hostile input and return an invalid file
// rather than crashing.

#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/codeview/codeview.h>

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
    OBJECT_SECTION_ZERO,
    OBJECT_SECTION_THREAD_LOCAL_DATA,
    OBJECT_SECTION_THREAD_LOCAL_ZERO,
    OBJECT_SECTION_UNWIND,
    OBJECT_SECTION_WINDOWS_PDATA,
    OBJECT_SECTION_WINDOWS_XDATA,
    OBJECT_SECTION_DEBUG_INFO,
    OBJECT_SECTION_DEBUG_ABBREV,
    OBJECT_SECTION_DEBUG_LINE,
    OBJECT_SECTION_DEBUG_STR,
    OBJECT_SECTION_DEBUG_LOC,
    OBJECT_SECTION_DEBUG_RANGES,
    OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
    OBJECT_SECTION_DEBUG_CODEVIEW_TYPES,
    OBJECT_SECTION_COUNT,
} ObjectSectionKind;

BUSTER_F_DECL bool object_section_kind_is_debug(ObjectSectionKind kind);
BUSTER_F_DECL bool object_section_kind_is_zero_fill(ObjectSectionKind kind);
BUSTER_F_DECL String8 object_section_name_for_kind(ObjectSectionKind kind);
BUSTER_F_DECL u32 object_section_default_alignment(ObjectSectionKind kind);
bool object_mach_compact_decode(Arena* arena, ByteSlice text, u32 function_offset, u32 function_size, u32 encoding, Target target,
                                                  CodegenFunctionDescriptor* descriptor);

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
    OBJECT_RELOCATION_AARCH64_PREL32,
    OBJECT_RELOCATION_ABSOLUTE64,
    OBJECT_RELOCATION_ABSOLUTE32,
    // x86-64 R_X86_64_32S: absolute S + A, required to sign-extend from
    // 32 bits.  Keep this distinct from the unsigned ABSOLUTE32 forms.
    OBJECT_RELOCATION_X86_64_ABSOLUTE32S,
    OBJECT_RELOCATION_COFF_SECREL32,
    OBJECT_RELOCATION_COFF_SECTION16,
    OBJECT_RELOCATION_COFF_ADDR32NB,
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
    OBJECT_RELOCATION_AARCH64_JUMP26,
    OBJECT_RELOCATION_AARCH64_MACH_PAGE21,
    OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12,
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

// A replaceable definition: COFF selectany COMDAT (IMAGE_SCN_LNK_COMDAT with
// a selection other than NODUPLICATES), ELF STB_WEAK, or Mach-O N_WEAK_DEF.
// The linker keeps the first such definition instead of diagnosing a
// duplicate, and any non-replaceable definition of the same name wins over
// every replaceable one regardless of input order.
typedef struct ObjectSymbol ObjectSymbol;
struct ObjectSymbol
{
    String8 name;
    u64 value;
    u64 size;
    u32 section;
    ObjectSymbolKind kind;
    bool global;
    bool weak;
    // Not exported from the final image: ELF STV_HIDDEN.  A `.hidden`
    // directive in module-level assembly is the only producer today, and only
    // the ELF writer and reader carry it -- COFF and Mach-O have no
    // equivalent per-symbol visibility byte.
    bool hidden;
    u8 reserved;
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

// A debug module is kept separate from the merged section storage.  Its
// offsets are relative to the corresponding ObjectFile sections, so linking
// can adjust every input module without concatenating translation units into
// one indistinguishable CodeView stream.
typedef struct ObjectDebugModule ObjectDebugModule;
struct ObjectDebugModule
{
    String8 name;
    u64 code_offset;
    u64 code_size;
    u64 symbols_offset;
    u64 symbols_size;
    u64 types_offset;
    u64 types_size;
};

typedef struct ObjectFile ObjectFile;
struct ObjectFile
{
    ObjectSection* sections;
    ObjectSymbol* symbols;
    ObjectRelocation* relocations;
    Target target;
    ObjectError error;
    // When error is OBJECT_ERROR_UNSUPPORTED_TARGET, this may name the
    // unsupported input feature (for example an ELF relocation) for a
    // user-facing diagnostic.  Empty means no more-specific text exists.
    String8 diagnostic;
    u32 section_count;
    u32 symbol_count;
    u32 relocation_count;
    ObjectDebugModule* debug_modules;
    u32 debug_module_count;
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
BUSTER_F_DECL ObjectFile object_from_canonical_codegen_module(Arena* arena, IrProgram* program, CodegenModule* module, Target target);
BUSTER_F_DECL String8 object_print_assembly(Arena* arena, ObjectFile* object);
BUSTER_F_DECL ObjectArtifact object_write(Arena* arena, ObjectFile* object, ObjectFormat format);
BUSTER_F_DECL ObjectFile object_read(Arena* arena, ByteSlice bytes, Target target);
BUSTER_F_DECL ObjectArchive object_archive_read(Arena* arena, ByteSlice bytes, Target target);
BUSTER_F_DECL ObjectExecutable object_link_executable(ObjectFile* object);
BUSTER_F_DECL void object_release_executable(ObjectExecutable executable);

#if BUSTER_FUZZ_AVAILABLE
BUSTER_F_DECL s32 object_fuzz_test_input(const u8* pointer, size_t size);
#endif
