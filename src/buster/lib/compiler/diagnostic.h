#pragma once

// Shared diagnostic publication, independent of grammar and backend enums.
// Producers supply stable textual codes and canonical source positions only
// when a diagnostic exists. The driver owns ordering; copying makes records
// survive per-translation-unit arena release. A zero-length range is a point,
// and line zero means an unavailable position, never an invented source span.
#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/model.h>

typedef enum CompilerDiagnosticSeverity
{
    COMPILER_DIAGNOSTIC_ERROR,
    COMPILER_DIAGNOSTIC_WARNING,
    COMPILER_DIAGNOSTIC_NOTE,
} CompilerDiagnosticSeverity;

typedef struct CompilerDiagnosticLocation CompilerDiagnosticLocation;
struct CompilerDiagnosticLocation
{
    String8 path;
    String8 original_path;
    IrSourceRange range;
    IrSourcePosition position;
    IrSourcePosition original_position;
    bool has_range;
};

typedef struct CompilerDiagnosticNote CompilerDiagnosticNote;
struct CompilerDiagnosticNote
{
    String8 message;
    CompilerDiagnosticLocation location;
};

// Optional backend evidence. Symbolic names belong in messages; numeric IDs
// remain available to tools. UINT32_MAX means an inapplicable internal ID.
// Fallback counters remain owned by CodegenStatistics, not this record.
typedef struct CompilerDiagnosticBackend CompilerDiagnosticBackend;
struct CompilerDiagnosticBackend
{
    String8 target;
    String8 allocator;
    String8 function;
    String8 opcode;
    String8 operation;
    String8 reason;
    String8 referenced_symbol;
    u32 error_id;
    u32 function_id;
    u32 instruction_id;
    u32 opcode_id;
    u32 operation_id;
};

typedef struct CompilerDiagnostic CompilerDiagnostic;
struct CompilerDiagnostic
{
    String8 code;
    // Linker symbol evidence has no source range unless a producer supplies one.
    String8 symbol;
    String8 message;
    CompilerDiagnosticLocation primary;
    CompilerDiagnosticNote* notes;
    CompilerDiagnosticBackend* backend;
    u32 note_count;
    CompilerDiagnosticSeverity severity;
};

BUSTER_F_DECL CompilerDiagnostic compiler_diagnostic_copy(Arena* arena, CompilerDiagnostic diagnostic);
BUSTER_F_DECL String8 compiler_diagnostic_render(Arena* arena, CompilerDiagnostic diagnostic);
