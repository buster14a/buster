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
    // Requested debug information cannot be represented by the target format.
    OBJECT_ERROR_DEBUG_INFO,
    // A section's requested alignment cannot be represented by its object format.
    OBJECT_ERROR_UNSUPPORTED_ALIGNMENT,
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
    // The pointer arrays a `constructor`/`destructor` function is registered
    // in: one relocated pointer per entry, in the order they are to run.  ELF
    // spells them SHT_INIT_ARRAY/SHT_FINI_ARRAY `.init_array`/`.fini_array`
    // and Mach-O `__DATA,__mod_init_func`/`__mod_term_func`; COFF keeps this
    // model's neutral names, the way `.rodata` and `.tdata` already do.
    OBJECT_SECTION_INIT_ARRAY,
    OBJECT_SECTION_FINI_ARRAY,
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
    // External DWARF 5 contributions stay opaque; relocations join them to
    // the existing debug sections without changing the compiler's DWARF 4 writer.
    OBJECT_SECTION_DEBUG_ADDR,
    OBJECT_SECTION_DEBUG_STR_OFFSETS,
    OBJECT_SECTION_DEBUG_LINE_STR,
    OBJECT_SECTION_DEBUG_RNGLISTS,
    OBJECT_SECTION_DEBUG_LOCLISTS,
    OBJECT_SECTION_COUNT,
} ObjectSectionKind;

// One entry of OBJECT_SECTION_INIT_ARRAY or OBJECT_SECTION_FINI_ARRAY: a
// pointer to the function it registers.  Every target this model converts
// codegen output for is 64-bit, and both the section's alignment and the
// ABSOLUTE64 relocation the entry takes are stated in terms of this.
#define OBJECT_INITIALIZER_ENTRY_SIZE 8u

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

enum
{
    OBJECT_SYMBOL_THREAD_LOCAL_UNKNOWN,
    OBJECT_SYMBOL_THREAD_LOCAL_NO,
    OBJECT_SYMBOL_THREAD_LOCAL_YES,
};

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
    // R_X86_64_GOTTPOFF: initial-exec, a GOT word the loader fills with the
    // symbol's offset from the thread pointer.
    OBJECT_RELOCATION_X86_64_GOTTPOFF,
    // R_X86_64_TLSGD: general-dynamic, the address of the module/offset pair
    // the loader builds, and R_X86_64_PLT32 against __tls_get_addr for the
    // call that reads it. The two always appear together, eight bytes apart,
    // in that order.
    OBJECT_RELOCATION_X86_64_TLSGD,
    OBJECT_RELOCATION_X86_64_PLT32,
    OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32,
    OBJECT_RELOCATION_PE_TLS_OFFSET32,
    OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP,
    OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12,
    OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12,
    // Ordinary Windows ARM64 page-address pairs.  Keep these separate from
    // the loader-owned TLS sequence even though COFF gives PAGEBASE_REL21
    // the same numeric type as the __tls_index ADRP relocation.
    OBJECT_RELOCATION_AARCH64_PE_PAGEBASE_REL21,
    OBJECT_RELOCATION_AARCH64_PE_PAGEOFFSET_12A,
    OBJECT_RELOCATION_AARCH64_PE_PAGEOFFSET_12L,
    OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12,
    OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12,
    OBJECT_RELOCATION_X86_64_MACH_TLV_PC32,
    OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21,
    OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12,
    OBJECT_RELOCATION_AARCH64_JUMP26,
    // Ordinary ELF address relocations have distinct REL/RELA addend rules.
    OBJECT_RELOCATION_AARCH64_ELF_PAGE21,
    OBJECT_RELOCATION_AARCH64_ELF_ADD_LO12,
    OBJECT_RELOCATION_AARCH64_MACH_PAGE21,
    OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12,
    // The position-independent code model's own form: R_X86_64_GOTPCREL
    // names the linker's slot holding the symbol's address rather than the
    // symbol, and is patched rip-relative like PC32. Its sibling is
    // OBJECT_RELOCATION_X86_64_PLT32 above, which the thread-local models
    // introduced for __tls_get_addr and which every interposable direct call
    // takes under -fPIC.
    OBJECT_RELOCATION_X86_64_GOTPCREL,
    // The psABI's relaxable spellings of the same reference.
    // R_X86_64_GOTPCRELX promises the producer wrote a form a linker may
    // convert and that the site carries no REX prefix; R_X86_64_REX_GOTPCRELX
    // promises the same for a site with exactly one. Both the promise and the
    // prefix width are what let a linker rewrite the instruction instead of
    // patching its displacement, so neither collapses into the plain form
    // above, which carries no promise at all.
    OBJECT_RELOCATION_X86_64_GOTPCRELX,
    OBJECT_RELOCATION_X86_64_REX_GOTPCRELX,
    // R_X86_64_CODE_4_GOTPCRELX: the relaxable REX2 spelling.  The
    // instruction begins four bytes before its relocated field.
    OBJECT_RELOCATION_X86_64_CODE_4_GOTPCRELX,
    OBJECT_RELOCATION_COUNT,
} ObjectRelocationKind;

// The four x86-64 GOT spellings share every rule but relaxation: one
// rip-relative 32-bit field, a -4 addend, and a value taken from the slot
// holding the symbol's address. Ask this instead of naming all three
// wherever only that shared contract matters.
BUSTER_F_DECL bool object_relocation_kind_is_x86_got(ObjectRelocationKind kind);

// Apply the ordinary Windows ARM64 PAGEBASE_REL21/PAGEOFFSET_12A contract to
// one canonical instruction.  The reader removes COFF's inline addend; the
// PE linker supplies it here after final placement is known.
BUSTER_F_DECL bool object_aarch64_pe_page_relocate(ObjectRelocationKind kind, u32 word, u64 place, u64 target, s64 addend, u32* patched);
BUSTER_F_DECL bool object_aarch64_pe_tls_index_lo12_relocate(u32 word, u64 target, s64 addend, u32* patched);

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

// COFF gives every COMDAT source section one of these selection contracts.
// The numeric values deliberately match IMAGE_COMDAT_SELECT_* so the reader
// can validate and preserve the auxiliary section-definition byte directly.
typedef enum ObjectComdatSelection
{
    OBJECT_COMDAT_SELECTION_NONE,
    OBJECT_COMDAT_SELECTION_NO_DUPLICATES,
    OBJECT_COMDAT_SELECTION_ANY,
    OBJECT_COMDAT_SELECTION_SAME_SIZE,
    OBJECT_COMDAT_SELECTION_EXACT_MATCH,
    OBJECT_COMDAT_SELECTION_ASSOCIATIVE,
    OBJECT_COMDAT_SELECTION_LARGEST,
    OBJECT_COMDAT_SELECTION_COUNT,
} ObjectComdatSelection;

#define OBJECT_COMDAT_ASSOCIATED_NONE UINT32_MAX

// One original COFF COMDAT section contribution after the reader has merged
// sections of the same neutral kind. `offset`/`size` identify its bytes in
// `sections[section]`; `source_section` preserves the input section number;
// and `associated` is another record in this array for ASSOCIATIVE sections.
// Relocations stay contiguous per source section and are recorded explicitly
// so EXACT_MATCH can compare their canonical identity without rescanning the
// entire object. A record with selection NONE represents a malformed/pending
// COMDAT and deliberately keeps the old hard-definition behavior.
typedef struct ObjectComdat ObjectComdat;
struct ObjectComdat
{
    String8 key;
    u64 offset;
    u64 size;
    u32 section;
    u32 source_section;
    u32 associated;
    u32 first_relocation;
    u32 relocation_count;
    ObjectComdatSelection selection;
};

// A replaceable definition: a selected COFF COMDAT contribution, ELF
// STB_WEAK, or Mach-O N_WEAK_DEF. COFF selection is resolved as a group before
// this ordinary weak/strong arbitration; `comdat` is zero for every other
// symbol and otherwise names ObjectFile.comdats[comdat - 1].
typedef struct ObjectSymbol ObjectSymbol;
struct ObjectSymbol
{
    String8 name;
    u64 value;
    u64 size;
    u32 section;
    u32 comdat;
    ObjectSymbolKind kind;
    bool global;
    bool weak;
    // Not exported from the final image: ELF STV_HIDDEN.  A `.hidden`
    // directive in module-level assembly is the only producer today, and only
    // the ELF writer and reader carry it -- COFF and Mach-O have no
    // equivalent per-symbol visibility byte.
    bool hidden;
    // The ELF symbol type carries TLS identity even when section is undefined.
    // UNKNOWN is retained for object formats that do not encode this property
    // on an undefined symbol. This occupies the former reserved byte.
    u8 thread_local_state;
};

typedef struct ObjectRelocation ObjectRelocation;
struct ObjectRelocation
{
    s64 addend;
    u64 offset;
    u32 section;
    u32 symbol;
    // Zero outside a COFF COMDAT; otherwise ObjectFile.comdats[comdat - 1].
    // The field fills the structure's former tail padding on 64-bit hosts.
    u32 comdat;
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
    ObjectComdat* comdats;
    Target target;
    ObjectError error;
    // When error is OBJECT_ERROR_UNSUPPORTED_TARGET, this may name the
    // unsupported input feature (for example an ELF relocation) for a
    // user-facing diagnostic.  Empty means no more-specific text exists.
    String8 diagnostic;
    u32 section_count;
    u32 symbol_count;
    u32 relocation_count;
    u32 comdat_count;
    ObjectDebugModule* debug_modules;
    u32 debug_module_count;
    // The GNU `constructor(N)`/`destructor(N)` priority of every entry of
    // OBJECT_SECTION_INIT_ARRAY (index 0) and OBJECT_SECTION_FINI_ARRAY
    // (index 1): one u32 per OBJECT_INITIALIZER_ENTRY_SIZE bytes of that
    // section, in slot order, ascending because the entries were sorted into
    // it, with IR_INITIALIZER_PRIORITY_NONE for an attribute that named none.
    // This is what carries the priority past a model that has one section per
    // kind, in both directions: object_from_canonical_codegen_module records
    // what the attribute named and the ELF and COFF writers split the array
    // back into the sections their linkers order by --
    // `.init_array.NNNNN` and `.CRT$XCA00101`, see
    // object_split_initializer_priorities -- while object_read_elf64 and
    // object_read_coff recover it from those same names so the linker can
    // order one program's whole array (link_initializer_arrays_order).  A
    // producer that has no priorities to state -- the Mach-O reader, whose
    // format has no such convention (issue 795), or a hand-built file --
    // leaves these null, which reads as every entry unprioritized and keeps
    // the arrays in the order they arrived.
    u32* initializer_priorities[2];
};

// A section payload that an artifact names in place instead of copying. The
// payload's bytes belong at file offset `offset` and stay owned by the
// ObjectFile section they came from.
typedef struct ObjectBorrowedPayload ObjectBorrowedPayload;
struct ObjectBorrowedPayload
{
    u64 offset;
    ByteSlice bytes;
};

typedef struct ObjectArtifact ObjectArtifact;
struct ObjectArtifact
{
    ByteSlice bytes;
    // Nonzero only for object_write_borrowing. `bytes` then spans the whole
    // file but leaves each borrowed range unwritten; object_artifact_slices
    // yields the file in order, and the ObjectFile must outlive the artifact.
    ObjectBorrowedPayload const* borrowed_payloads;
    u32 borrowed_payload_count;
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
BUSTER_F_DECL ObjectArtifact object_write_borrowing(Arena* arena, ObjectFile* object, ObjectFormat format);
BUSTER_F_DECL ByteSlice* object_artifact_slices(Arena* arena, ObjectArtifact artifact, u32* slice_count_out);
BUSTER_F_DECL ObjectFile object_read(Arena* arena, ByteSlice bytes, Target target);
BUSTER_F_DECL ObjectArchive object_archive_read(Arena* arena, ByteSlice bytes, Target target);
BUSTER_F_DECL ObjectExecutable object_link_executable(ObjectFile* object);
BUSTER_F_DECL bool object_aarch64_elf_page_relocate(ObjectRelocationKind kind, u32 word, u64 place, u64 target, s64 addend, u32* patched);
BUSTER_F_DECL void object_release_executable(ObjectExecutable executable);

#if BUSTER_FUZZ_AVAILABLE
BUSTER_F_DECL s32 object_fuzz_test_input(const u8* pointer, size_t size);
#endif
