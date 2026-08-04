#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/os.h>

#define IDE_DOCUMENT_INDEX_INVALID UINT32_MAX
#define IDE_DOCUMENT_DEFAULT_MAX_DISCOVERED_FILES 4096u
#define IDE_DOCUMENT_DEFAULT_MAX_TRAVERSAL_ENTRIES 65536u
#define IDE_DOCUMENT_DEFAULT_MAX_DIAGNOSTICS 16384u

typedef enum IdeDocumentErrorKind
{
    IDE_DOCUMENT_ERROR_NONE,
    IDE_DOCUMENT_ERROR_INVALID_ARGUMENT,
    IDE_DOCUMENT_ERROR_ROOT_NOT_FOUND,
    IDE_DOCUMENT_ERROR_ROOT_NOT_DIRECTORY,
    IDE_DOCUMENT_ERROR_PATH_NOT_FOUND,
    IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT,
    IDE_DOCUMENT_ERROR_NOT_REGULAR_FILE,
    IDE_DOCUMENT_ERROR_FILE_READ,
    IDE_DOCUMENT_ERROR_FILE_WRITE,
    IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT,
    IDE_DOCUMENT_ERROR_OPEN_PATH_NOT_SUPPORTED,
    IDE_DOCUMENT_ERROR_ALREADY_INITIALIZED,
    IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT,
    IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND,
    IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN,
    IDE_DOCUMENT_ERROR_ACTIVE_DOCUMENT_REQUIRED,
    IDE_DOCUMENT_ERROR_DIRTY_RELOAD_CONFLICT,
    IDE_DOCUMENT_ERROR_INVALID_SELECTION,
    IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT,
    IDE_DOCUMENT_ERROR_COUNT,
} IdeDocumentErrorKind;

typedef enum IdeDocumentReloadMode
{
    IDE_DOCUMENT_RELOAD_REJECT_DIRTY,
    IDE_DOCUMENT_RELOAD_DISCARD_DIRTY,
    IDE_DOCUMENT_RELOAD_MODE_COUNT,
} IdeDocumentReloadMode;

typedef enum IdeDocumentImportState
{
    IDE_DOCUMENT_IMPORT_RESOLVED,
    IDE_DOCUMENT_IMPORT_MISSING,
    IDE_DOCUMENT_IMPORT_AMBIGUOUS,
    IDE_DOCUMENT_IMPORT_CYCLE,
    IDE_DOCUMENT_IMPORT_STATE_COUNT,
} IdeDocumentImportState;

typedef enum IdeDocumentDiagnosticSeverity
{
    IDE_DOCUMENT_DIAGNOSTIC_INFO,
    IDE_DOCUMENT_DIAGNOSTIC_WARNING,
    IDE_DOCUMENT_DIAGNOSTIC_ERROR,
    IDE_DOCUMENT_DIAGNOSTIC_SEVERITY_COUNT,
} IdeDocumentDiagnosticSeverity;

typedef enum IdeDocumentDiagnosticSource
{
    IDE_DOCUMENT_DIAGNOSTIC_SOURCE_PARSER,
    IDE_DOCUMENT_DIAGNOSTIC_SOURCE_ANALYSIS,
    IDE_DOCUMENT_DIAGNOSTIC_SOURCE_COMPILER,
    IDE_DOCUMENT_DIAGNOSTIC_SOURCE_USER,
    IDE_DOCUMENT_DIAGNOSTIC_SOURCE_COUNT,
} IdeDocumentDiagnosticSource;

typedef enum IdeDocumentCompileStatus
{
    IDE_DOCUMENT_COMPILE_NOT_BUILT,
    IDE_DOCUMENT_COMPILE_SUCCEEDED,
    IDE_DOCUMENT_COMPILE_FAILED,
    IDE_DOCUMENT_COMPILE_STALE,
    IDE_DOCUMENT_COMPILE_STATUS_COUNT,
} IdeDocumentCompileStatus;

typedef struct IdeDocumentModelOptions IdeDocumentModelOptions;
struct IdeDocumentModelOptions
{
    String8 workspace_root;
    String8 open_path;
    Arena* expression_arena;
    u32 max_discovered_files;
    u32 max_traversal_entries;
    u32 max_diagnostics;
};

typedef struct IdeDocumentViewState IdeDocumentViewState;
struct IdeDocumentViewState
{
    // Offsets are UTF-8 byte offsets, not Unicode scalar indices. Source
    // replacement clamps them to the new byte length and preserves selection
    // ordering; offsets are not rounded to code-point boundaries.
    u64 cursor_offset;
    u64 selection_start;
    u64 selection_end;
    f32 scroll_x;
    f32 scroll_y;
    f32 zoom;
};

typedef struct IdeDocumentSearchState IdeDocumentSearchState;
struct IdeDocumentSearchState
{
    String8 query;
    String8 replacement;
    u32 match_count;
    bool case_sensitive;
    bool whole_word;
    bool regular_expression;
    u8 reserved[5];
};

typedef struct IdeDocumentWorkspaceFilterState IdeDocumentWorkspaceFilterState;
struct IdeDocumentWorkspaceFilterState
{
    String8 query;
    bool show_open_only;
    bool show_dirty_only;
    bool show_diagnostics_only;
    bool case_sensitive;
    bool whole_word;
    bool regular_expression;
    u8 reserved[2];
};

typedef struct IdeDocumentCompileMetadata IdeDocumentCompileMetadata;
struct IdeDocumentCompileMetadata
{
    IdeDocumentCompileStatus status;
    u64 compiled_revision;
    u64 artifact_hash;
    String8 artifact_path;
    String8 command_line;
    String8 message;
};

typedef struct IdeDocumentDiagnosticInput IdeDocumentDiagnosticInput;
struct IdeDocumentDiagnosticInput
{
    String8 file_path;
    ParserSourceRange range;
    String8 message;
    u64 identity;
    IdeDocumentDiagnosticSeverity severity;
    IdeDocumentDiagnosticSource source;
    u8 reserved[6];
};

typedef struct IdeDocumentDiagnostic IdeDocumentDiagnostic;
struct IdeDocumentDiagnostic
{
    String8 file_path;
    ParserSourceRange range;
    String8 message;
    u64 identity;
    IdeDocumentDiagnosticSeverity severity;
    IdeDocumentDiagnosticSource source;
    u8 reserved[6];
};

typedef struct IdeDocumentImport IdeDocumentImport;
struct IdeDocumentImport
{
    String8 source_path;
    String8 name_space;
    String8 requested_path;
    String8 target_path;
    ParserSourceRange range;
    ParserSourceRange path_range;
    IdeDocumentImportState state;
    u8 reserved[4];
};

typedef struct IdeDocument IdeDocument;
struct IdeDocument
{
    String8 path;
    String8 identity;
    String8 module_name;
    String8 source;
    String8 saved_source;
    FileStats external_stats;
    u64 external_hash;
    u64 saved_hash;
    u64 revision;
    u64 saved_revision;
    u64 open_order;
    IdeDocumentViewState view;
    IdeDocumentSearchState search;
    IdeDocumentCompileMetadata compile;
    IdeDocumentDiagnostic* diagnostics;
    u32 diagnostic_count;
    bool external_exists;
    bool external_modified;
    bool dirty;
    bool is_open;
    u8 reserved[4];
};

typedef struct IdeDocumentWorkspace IdeDocumentWorkspace;
struct IdeDocumentWorkspace
{
    String8 root_path;
    IdeDocument* documents;
    IdeDocumentImport* imports;
    IdeDocumentWorkspaceFilterState filter;
    u32 document_count;
    u32 import_count;
    u32 active_document_index;
    u32 open_document_count;
    u64 next_open_order;
};

typedef struct IdeDocumentModel IdeDocumentModel;
struct IdeDocumentModel
{
    Arena* active_arena;
    Arena* staging_arena;
    Arena* expression_arena;
    IdeDocumentWorkspace workspace;
    u32 max_discovered_files;
    u32 max_traversal_entries;
    u32 max_diagnostics;
    bool initialized;
    bool owns_expression_arena;
    u8 reserved[2];
};

BUSTER_F_DECL String8 ide_document_error_kind_name(IdeDocumentErrorKind kind);
BUSTER_F_DECL String8 ide_document_path_canonical(Arena* arena, String8 path);
BUSTER_F_DECL String8 ide_document_path_identity(Arena* arena, String8 canonical_path);
BUSTER_F_DECL bool ide_document_path_is_within(String8 root, String8 path);

BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_initialize(IdeDocumentModel* model, Arena* arena, Arena* staging_arena,
                                                                 IdeDocumentModelOptions options);
BUSTER_F_DECL void ide_document_model_deinitialize(IdeDocumentModel* model);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_refresh_workspace(IdeDocumentModel* model);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_open(IdeDocumentModel* model, String8 path);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_close(IdeDocumentModel* model, String8 path);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_set_active(IdeDocumentModel* model, String8 path);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_set_text(IdeDocumentModel* model, String8 path, String8 source);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_reload(IdeDocumentModel* model, String8 path, IdeDocumentReloadMode mode);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_poll_external(IdeDocumentModel* model, String8 path);
// Save performs a fresh best-effort byte/hash comparison against the saved source immediately before replacement.
// An uncooperative external writer can still race that read because this is not a portable compare-and-swap.
// POSIX and Windows replacement is atomic for live-process readers, exposing either the old or new file;
// parent-directory fsync and crash-durability guarantees are outside this contract.
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_save(IdeDocumentModel* model, String8 path);
#if BUSTER_INCLUDE_TESTS
BUSTER_TEST_F_DECL void ide_document_model_test_set_save_replace_failure(bool enabled);
#endif
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_set_view(IdeDocumentModel* model, String8 path, IdeDocumentViewState view);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_set_compile_metadata(IdeDocumentModel* model, String8 path,
                                                                            IdeDocumentCompileMetadata metadata);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_set_search_state(IdeDocumentModel* model, String8 path, IdeDocumentSearchState search);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_set_filter_state(IdeDocumentModel* model, IdeDocumentWorkspaceFilterState filter);
BUSTER_F_DECL IdeDocumentErrorKind ide_document_model_replace_diagnostics(IdeDocumentModel* model, const IdeDocumentDiagnosticInput* inputs,
                                                                          u32 input_count);

// Returned document and import pointers belong to the current committed arena. Any successful model mutation or deinitialization
// invalidates every previously returned pointer, including pointers reacquired from an earlier lookup; reacquire after the mutation.
// Failed mutations preserve the current committed pointers.
BUSTER_F_DECL IdeDocument* ide_document_model_find(IdeDocumentModel* model, String8 path);
BUSTER_F_DECL IdeDocument* ide_document_model_document_at(IdeDocumentModel* model, u32 index);
BUSTER_F_DECL IdeDocument* ide_document_model_active_document(IdeDocumentModel* model);
BUSTER_F_DECL IdeDocumentImport* ide_document_model_import_at(IdeDocumentModel* model, u32 index);
BUSTER_F_DECL u32 ide_document_model_document_count(IdeDocumentModel* model);
BUSTER_F_DECL u32 ide_document_model_open_document_count(IdeDocumentModel* model);
