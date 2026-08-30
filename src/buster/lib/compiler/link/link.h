#pragma once

// The linker's public API: link_objects merges ObjectFiles into one, and
// link_native_executable lays the merged file out as a runnable image —
// static/dynamic ELF64 (x86-64, AArch64, Android), hosted PE64,
// imports-free UEFI PE64, and Mach-O.

#include <buster/lib/compiler/object/object.h>

typedef enum LinkError
{
    LINK_ERROR_NONE,
    LINK_ERROR_INVALID_INPUT,
    LINK_ERROR_TARGET_MISMATCH,
    LINK_ERROR_DUPLICATE_SYMBOL,
    LINK_ERROR_UNRESOLVED_SYMBOL,
    LINK_ERROR_OBJECT_WRITE,
    LINK_ERROR_FILE_WRITE,
    LINK_ERROR_PROCESS_SPAWN,
    LINK_ERROR_PROCESS_FAILED,
    LINK_ERROR_UNSUPPORTED_HOST,
    LINK_ERROR_UNSUPPORTED_FEATURE,
    LINK_ERROR_ENTRY_SYMBOL,
    LINK_ERROR_RELOCATION,
    LINK_ERROR_SYMBOL_VERSION,
    LINK_ERROR_COUNT,
} LinkError;

typedef struct LinkOptions LinkOptions;
struct LinkOptions
{
    bool allow_undefined_symbols;
    u8 reserved[7];
};

typedef struct LinkObjectResult LinkObjectResult;
struct LinkObjectResult
{
    ObjectFile object;
    String8 symbol;
    LinkError error;
};

typedef struct NativeExecutableLinkOptions NativeExecutableLinkOptions;
typedef struct NativeDynamicDataSymbol NativeDynamicDataSymbol;
// One data object a shared library exports, as its own dynamic symbol table
// spells it.  A copy relocation reserves executable-owned storage for such an
// object, and both fields are what the reservation needs that the referencing
// name alone does not carry: the address groups the names the library exports
// for one object (glibc publishes `environ`, `_environ` and `__environ` at one
// address), and the size states how much the loader may write into the slot.
struct NativeDynamicDataSymbol
{
    String8 name;
    u64 address;
    u64 size;
};

typedef struct NativeDynamicVersionedSymbol NativeDynamicVersionedSymbol;
// One name a shared library defines, with the symbol version it publishes that
// definition under.  `version` is empty when the library exports the name
// without a version; `has_default` is false for a `name@VER` definition, which
// an unversioned reference cannot bind to.  One record per dynamic symbol, so
// a name published under several versions -- glibc has four of `sys_errlist`,
// all of them non-default -- appears once per version.
struct NativeDynamicVersionedSymbol
{
    String8 name;
    String8 version;
    bool has_default;
    u8 reserved[7];
};

typedef struct NativeDynamicLibrary NativeDynamicLibrary;
struct NativeDynamicLibrary
{
    String8 name;
    // PE only: the names the DLL's export directory lists.  The ELF export
    // list is `versioned_symbols`, which carries the same names and the
    // version each is published under, so ELF needs no second copy.
    String8* exported_symbols;
    // ELF only, and read only for links that import data: the PE writers
    // resolve imports by name and need no address.
    NativeDynamicDataSymbol* exported_data_symbols;
    // ELF only: every name this library defines, with its version.  Read for
    // every hosted ELF link, because an unversioned reference to a name whose
    // definitions are all non-default has nothing to bind to, the image has
    // to record the version of every reference that does bind, and a weak
    // reference to a name no library defines resolves to zero.
    NativeDynamicVersionedSymbol* versioned_symbols;
    u32 exported_symbol_count;
    u32 exported_data_symbol_count;
    u32 versioned_symbol_count;
    // Whether the driver read this library at all.  An empty export list is
    // not evidence that the library defines nothing: a library that was never
    // found on disk exports whatever it happens to export, so only a link
    // whose libraries were all read may read an absent name as absent.
    bool exports_known;
    u8 reserved[3];
};

struct NativeExecutableLinkOptions
{
    String8 output_path;
    String8 entry_symbol;
    String8 sysroot;
    String8* library_paths;
    String8* framework_paths;
    String8* frameworks;
    String8* linker_arguments;
    NativeDynamicLibrary* dynamic_libraries;
    String8* runtime_exported_symbols;
    // The implicit runtime library's exports: ucrtbase.dll for hosted Windows,
    // libc.so.6 for hosted ELF.  Neither appears in dynamic_libraries because
    // the writers name it themselves.
    NativeDynamicDataSymbol* runtime_data_symbols;
    NativeDynamicVersionedSymbol* runtime_versioned_symbols;
    u32 library_path_count;
    u32 framework_path_count;
    u32 framework_count;
    u32 linker_argument_count;
    u32 dynamic_library_count;
    u32 runtime_exported_symbol_count;
    u32 runtime_data_symbol_count;
    u32 runtime_versioned_symbol_count;
    bool runtime_exports_known;
    bool debug_info;
    u8 reserved[6];
};

typedef struct NativeExecutableLinkResult NativeExecutableLinkResult;
struct NativeExecutableLinkResult
{
    ByteSlice executable;
    ByteSlice pdb;
    String8 pdb_path;
    String8 symbol;
    LinkError error;
};

// The enumerator's own spelling, so a failed link names its reason rather than
// only its number.
BUSTER_F_DECL String8 link_error_name(LinkError error);
BUSTER_F_DECL LinkObjectResult link_objects(Arena* arena, ObjectFile* objects, u32 object_count, LinkOptions options);
// Synthetic compiler-runtime input for hosted Windows executable links only;
// object and relocatable output paths, UEFI, and non-Windows targets do not use it.
BUSTER_F_DECL ObjectFile link_windows_runtime_object(Arena* arena, Target target);
// The same for hosted ELF executable links: the glibc stubs that live in
// libc_nonshared.a rather than in the shared object the ELF writers import
// from.  Selected the way an archive member is, so a program that references
// none of them never sees it.
BUSTER_F_DECL ObjectFile link_elf_libc_runtime_object(Arena* arena, Target target);
// The UCRT counterpart for hosted Windows executable links: `atexit` and
// `at_quick_exit` live in the import library rather than in ucrtbase.dll, so
// this supplies them as weak stubs over the `_crt_` forms it does export.
// Selected the same way, which is what keeps the `_crt_` import out of an
// image that never registers a handler.
BUSTER_F_DECL ObjectFile link_windows_libc_runtime_object(Arena* arena, Target target);
BUSTER_F_DECL NativeExecutableLinkResult link_native_executable(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options);
BUSTER_F_DECL ByteSlice link_pe_resolved_codeview(Arena* arena, ObjectFile* object, ObjectDebugModule* debug_module,
                                                       u32 const* object_output_sections, u64 const* object_section_offsets,
                                                       u32 output_section_count);
