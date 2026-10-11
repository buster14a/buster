// Public source API for composing Buster's C driver, object writer, and object
// JIT. The compiler handle owns runtime setup; each module owns copied source,
// diagnostics, its object and optional JIT mapping. Release every module before
// destroying its compiler. Function/data imports and generated symbol
// addresses remain borrowed from the host and must outlive generated code.
//
// Entry points: buster_embed_compiler_create/destroy, buster_embed_compile,
// buster_embed_module_object/load/symbol/release, and status/diagnostic access.
// The status and diagnostic types below are the searchable symbol map.
//
// BUSTER_EMBED_API_VERSION versions this source interface; it does not promise
// a stable binary ABI. Create and use a compiler and its modules on one caller
// thread. Creation serially prewarms shared compiler tables; the facade does
// not create compiler worker lanes. Generated code may run on other threads
// while loaded, but module release must not race its execution.
//
// The JIT accepts only host-native objects and does not execute initializers or
// finalizers, support TLS, or register debug/unwind information. Compilation
// may target foreign architectures, but those objects cannot be loaded here.
#ifndef BUSTER_LIB_COMPILER_EMBED_EMBED_H
#define BUSTER_LIB_COMPILER_EMBED_EMBED_H

#include <stddef.h>
#include <stdint.h>

#ifndef BUSTER_EMBED_API
#define BUSTER_EMBED_API extern
#endif

#define BUSTER_EMBED_API_VERSION 1u

typedef struct BusterEmbedString
{
    char const* pointer;
    size_t length;
} BusterEmbedString;

typedef struct BusterEmbedBytes
{
    uint8_t const* pointer;
    size_t length;
} BusterEmbedBytes;

typedef struct BusterEmbedCompiler BusterEmbedCompiler;
typedef struct BusterEmbedModule BusterEmbedModule;

typedef enum BusterEmbedStatus
{
    BUSTER_EMBED_STATUS_OK,
    BUSTER_EMBED_STATUS_INVALID_ARGUMENT,   // null handle/output, bad option/argument (driver ARGUMENT error)
    BUSTER_EMBED_STATUS_RESOURCE,            // arena/thread-context/driver RESOURCE failure
    BUSTER_EMBED_STATUS_COMPILE_FAILED,      // source rejected or any other driver failure; diagnostics on module
    BUSTER_EMBED_STATUS_NOT_COMPILED,        // load/object on a module whose compile failed
    BUSTER_EMBED_STATUS_ALREADY_LOADED,
    BUSTER_EMBED_STATUS_NOT_LOADED,           // symbol lookup before a successful load
    BUSTER_EMBED_STATUS_FOREIGN_TARGET,      // JIT_ERROR_FOREIGN_TARGET
    BUSTER_EMBED_STATUS_UNRESOLVED_IMPORT,   // JIT_ERROR_UNRESOLVED_IMPORT
    BUSTER_EMBED_STATUS_JIT_FAILED,           // any other JitError; detail names it
    BUSTER_EMBED_STATUS_SYMBOL_NOT_FOUND,
    BUSTER_EMBED_STATUS_BUSY,                 // compiler destroy while modules are live
    BUSTER_EMBED_STATUS_COUNT,
} BusterEmbedStatus;

typedef enum BusterEmbedSymbolKind
{
    BUSTER_EMBED_SYMBOL_FUNCTION,
    BUSTER_EMBED_SYMBOL_DATA
} BusterEmbedSymbolKind;

typedef enum BusterEmbedSeverity
{
    BUSTER_EMBED_SEVERITY_ERROR,
    BUSTER_EMBED_SEVERITY_WARNING,
    BUSTER_EMBED_SEVERITY_NOTE
} BusterEmbedSeverity;

typedef struct BusterEmbedImport
{
    BusterEmbedString name;
    void* address;
    BusterEmbedSymbolKind kind;
} BusterEmbedImport;

typedef struct BusterEmbedCompileOptions
{
    BusterEmbedString source_name; // virtual filename; empty => "input.c"
    BusterEmbedString target;      // triple; empty => host native
    BusterEmbedString dialect;     // "gnu17", "c11"...; empty => driver default
    BusterEmbedString const* include_paths;
    uint32_t include_path_count;
    BusterEmbedString const* definitions; // "NAME" or "NAME=VALUE"
    uint32_t definition_count;
    BusterEmbedString const* arguments; // extra cc flags
    uint32_t argument_count;
} BusterEmbedCompileOptions;

typedef struct BusterEmbedDiagnostic
{
    BusterEmbedString code;
    BusterEmbedString message;
    BusterEmbedString path;
    BusterEmbedString rendered; // compiler_diagnostic_render text
    uint32_t line;
    uint32_t column;
    BusterEmbedSeverity severity;
} BusterEmbedDiagnostic;

BUSTER_EMBED_API uint32_t buster_embed_api_version(void);
BUSTER_EMBED_API BusterEmbedString buster_embed_status_string(BusterEmbedStatus status);
BUSTER_EMBED_API BusterEmbedStatus buster_embed_compiler_create(BusterEmbedCompiler** out_compiler);
BUSTER_EMBED_API BusterEmbedStatus buster_embed_compiler_destroy(BusterEmbedCompiler* compiler);

// Source and options are borrowed only during this call. On successful
// allocation, *out_module is set even when compilation fails; the module then
// owns its diagnostics and failure message and must be released by the caller.
BUSTER_EMBED_API BusterEmbedStatus buster_embed_compile(
    BusterEmbedCompiler* compiler,
    BusterEmbedString source,
    BusterEmbedCompileOptions const* options,
    BusterEmbedModule** out_module);

BUSTER_EMBED_API BusterEmbedStatus buster_embed_module_status(BusterEmbedModule const* module);
BUSTER_EMBED_API uint32_t buster_embed_module_diagnostic_count(BusterEmbedModule const* module);
BUSTER_EMBED_API BusterEmbedDiagnostic buster_embed_module_diagnostic(BusterEmbedModule const* module, uint32_t index);
BUSTER_EMBED_API BusterEmbedString buster_embed_module_message(BusterEmbedModule const* module);

// The serialized native object is created lazily and remains module-owned.
BUSTER_EMBED_API BusterEmbedStatus buster_embed_module_object(BusterEmbedModule* module, BusterEmbedBytes* out_bytes);
BUSTER_EMBED_API BusterEmbedStatus buster_embed_module_load(
    BusterEmbedModule* module,
    BusterEmbedImport const* imports,
    uint32_t import_count);
BUSTER_EMBED_API BusterEmbedStatus buster_embed_module_symbol(BusterEmbedModule* module, BusterEmbedString name, void** out_address);

// Null is accepted. Releases a loaded JIT mapping before destroying the
// module's arena; imported addresses must remain live through all executions.
BUSTER_EMBED_API void buster_embed_module_release(BusterEmbedModule* module);

#endif
