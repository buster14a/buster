#pragma once

#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/link/link.h>

// A unity C translation unit retains preprocessing, semantic, typed IR, and
// object/debug data through the driver call. The reservation is virtual and
// uses demand-paged commits, so this headroom does not eagerly consume 8 GiB
// of physical memory.
#define COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE BUSTER_GB(8)

typedef enum CompilerDriverError
{
    COMPILER_DRIVER_ERROR_NONE,
    COMPILER_DRIVER_ERROR_ARGUMENT,
    COMPILER_DRIVER_ERROR_INVALID_INPUT,
    COMPILER_DRIVER_ERROR_FILE_READ,
    COMPILER_DRIVER_ERROR_TOKENIZE,
    COMPILER_DRIVER_ERROR_PARSE,
    COMPILER_DRIVER_ERROR_ANALYSIS,
    COMPILER_DRIVER_ERROR_IR,
    COMPILER_DRIVER_ERROR_CODEGEN,
    COMPILER_DRIVER_ERROR_OBJECT,
    COMPILER_DRIVER_ERROR_LINK,
    COMPILER_DRIVER_ERROR_COUNT,
} CompilerDriverError;

typedef enum CompilerDriverLanguage
{
    COMPILER_DRIVER_LANGUAGE_AUTOMATIC,
    COMPILER_DRIVER_LANGUAGE_BUSTER,
    COMPILER_DRIVER_LANGUAGE_C,
    COMPILER_DRIVER_LANGUAGE_COUNT,
} CompilerDriverLanguage;

typedef enum CompilerDriverAction
{
    COMPILER_DRIVER_ACTION_LINK,
    COMPILER_DRIVER_ACTION_PREPROCESS,
    COMPILER_DRIVER_ACTION_ASSEMBLY,
    COMPILER_DRIVER_ACTION_OBJECT,
    COMPILER_DRIVER_ACTION_SYNTAX_ONLY,
    COMPILER_DRIVER_ACTION_COUNT,
} CompilerDriverAction;

typedef enum CompilerDriverCDialect
{
    COMPILER_DRIVER_C_DIALECT_GNU11,
    COMPILER_DRIVER_C_DIALECT_GNU17,
    COMPILER_DRIVER_C_DIALECT_GNU23,
    COMPILER_DRIVER_C_DIALECT_C11,
    COMPILER_DRIVER_C_DIALECT_C17,
    COMPILER_DRIVER_C_DIALECT_C23,
    COMPILER_DRIVER_C_DIALECT_COUNT,
} CompilerDriverCDialect;

typedef struct CompilerDriverInvocation CompilerDriverInvocation;
struct CompilerDriverInvocation
{
    String8* input_paths;
    String8* include_paths;
    String8* system_include_paths;
    String8* definitions;
    String8* undefinitions;
    String8* library_paths;
    String8* libraries;
    String8* framework_paths;
    String8* frameworks;
    String8* linker_arguments;
    String8 output_path;
    String8 sysroot;
    String8 module_root;
    // Where to write the source measurement as key=value text. `-v` prints the
    // same numbers as a table for a human; this is the form another program
    // reads, so a build driver can divide its own instruction count by them.
    String8 source_metrics_path;
    String8 diagnostic;
    Target target;
    u32 input_count;
    u32 include_path_count;
    u32 system_include_path_count;
    u32 definition_count;
    u32 undefinition_count;
    u32 library_path_count;
    u32 library_count;
    u32 framework_path_count;
    u32 framework_count;
    u32 linker_argument_count;
    CompilerDriverLanguage language;
    CompilerDriverAction action;
    CompilerDriverCDialect c_dialect;
    CompilerDriverError error;
    AssemblySyntax assembly_syntax;
    bool verbose;
    bool no_standard_includes;
    bool debug_info;
    u8 reserved;
};

typedef struct CompilerDriverOptions CompilerDriverOptions;
struct CompilerDriverOptions
{
    String8 source_path;
    String8 output_path;
    String8 module_root;
    Target target;
    bool debug_info;
    u8 reserved[7];
};

typedef struct CompilerDriverResult CompilerDriverResult;
struct CompilerDriverResult
{
    String8 diagnostic;
    String8 warning;
    String8 output;
    NativeExecutableLinkResult native_link;
    ObjectFile object;
    CodegenStatistics codegen_statistics;
    // What the C frontend consumed, per inclusion and per distinct file, and
    // what preprocessing made of it.
    CSourceMetrics source_lexed;
    CSourceMetrics source_unique;
    CPreprocessedMetrics preprocessed;
    CompilerDriverError error;
    CodegenError codegen_error;
    ObjectError object_error;
    u32 tokenizer_error_count;
    u32 tokenizer_warning_count;
    u32 parser_diagnostic_count;
    u32 analysis_diagnostic_count;
    bool has_object;
    u8 reserved[3];
};

// Fills every table the compile pipeline builds on first use -- tokenizer, C
// frontend, codegen -- on the calling thread. Call this before lane_run:
// those tables are written once and read afterwards through plain loads, so
// they must be complete before a second lane can reach them, and a gang that
// finds one unwarmed reports through BUSTER_CHECK_SERIAL_INITIALIZATION
// rather than racing. x86 assembly metadata is deliberately not included: no
// part of the compile path queries it, and its prewarm costs a full table
// decode, so a caller that runs the assembler in a gang asks
// buster_x86_metadata_prewarm() for it directly.
BUSTER_F_DECL void compiler_prewarm(void);
BUSTER_F_DECL CompilerDriverResult compiler_driver_compile(Arena* arena, CompilerDriverOptions options);
BUSTER_F_DECL CompilerDriverInvocation compiler_driver_parse_arguments(Arena* arena, SliceString8 arguments);
BUSTER_F_DECL CompilerDriverResult compiler_driver_execute_invocation(Arena* arena, CompilerDriverInvocation invocation);
