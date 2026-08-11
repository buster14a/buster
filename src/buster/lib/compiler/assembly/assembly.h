#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/target.h>

typedef enum AssemblySyntax
{
    ASSEMBLY_SYNTAX_DEFAULT,
    ASSEMBLY_SYNTAX_ATT,
    ASSEMBLY_SYNTAX_INTEL,
    ASSEMBLY_SYNTAX_COUNT,
} AssemblySyntax;

typedef enum AssemblyDiagnosticKind
{
    ASSEMBLY_DIAGNOSTIC_INVALID_TARGET,
    ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX,
    ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT,
    ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION,
    ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION,
    ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS,
    ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE,
    ASSEMBLY_DIAGNOSTIC_DUPLICATE_SYMBOL,
    ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE,
    ASSEMBLY_DIAGNOSTIC_COUNT,
} AssemblyDiagnosticKind;

typedef struct AssemblyDiagnostic AssemblyDiagnostic;
struct AssemblyDiagnostic
{
    String8 message;
    u32 line;
    u32 column;
    u32 length;
    AssemblyDiagnosticKind kind;
};

typedef enum AssemblyRelocationKind
{
    ASSEMBLY_RELOCATION_X86_PC32,
    ASSEMBLY_RELOCATION_X86_32,
    // X86_32 is the historical generic absolute-32 relocation.  Keep that
    // value and name stable while exposing the complete metadata emitter
    // taxonomy below.
    ASSEMBLY_RELOCATION_X86_ABSOLUTE32 = ASSEMBLY_RELOCATION_X86_32,
    ASSEMBLY_RELOCATION_AARCH64_BRANCH26,
    ASSEMBLY_RELOCATION_AARCH64_JUMP26 = ASSEMBLY_RELOCATION_AARCH64_BRANCH26,
    ASSEMBLY_RELOCATION_X86_ABSOLUTE8,
    ASSEMBLY_RELOCATION_X86_ABSOLUTE16,
    ASSEMBLY_RELOCATION_X86_ABSOLUTE64,
    ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED,
    ASSEMBLY_RELOCATION_X86_ABSOLUTE32_ZERO_EXTENDED,
    ASSEMBLY_RELOCATION_X86_PC8,
    ASSEMBLY_RELOCATION_X86_PC16,
    ASSEMBLY_RELOCATION_X86_PC64,
    ASSEMBLY_RELOCATION_AARCH64_CALL26,
    ASSEMBLY_RELOCATION_COUNT,
} AssemblyRelocationKind;

typedef struct AssemblyRelocation AssemblyRelocation;
struct AssemblyRelocation
{
    s64 addend;
    u64 offset;
    u32 symbol;
    AssemblyRelocationKind kind;
};

typedef struct AssemblySymbol AssemblySymbol;
struct AssemblySymbol
{
    String8 name;
    u64 offset;
    bool defined;
    u8 reserved[7];
};

typedef struct AssemblyEncodeOptions AssemblyEncodeOptions;
struct AssemblyEncodeOptions
{
    Target target;
    AssemblySyntax syntax;
};

typedef struct AssemblyEncodeResult AssemblyEncodeResult;
struct AssemblyEncodeResult
{
    ByteSlice bytes;
    AssemblyRelocation* relocations;
    AssemblySymbol* symbols;
    AssemblyDiagnostic* diagnostics;
    u32 relocation_count;
    u32 symbol_count;
    u32 diagnostic_count;
    u32 reserved;
};

// Encodes one source buffer without retaining parser scratch. Labels defined in
// the buffer are resolved immediately; unresolved names remain as relocations.
// Scalar x86 register/immediate forms and the bootstrap AArch64 control-flow
// forms use the same instruction-local representation that generated ISA
// tables extend without changing this public contract.
BUSTER_F_DECL AssemblyEncodeResult assembly_encode(Arena* arena, String8 source, AssemblyEncodeOptions options);
