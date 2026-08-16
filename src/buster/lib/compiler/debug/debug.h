#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/model.h>
#include <buster/lib/target.h>

typedef struct IrProgram IrProgram;
typedef struct IrModule IrModule;
typedef struct IrFunction IrFunction;

// Debug information is deliberately kept independent from either object-file
// format. Frontends describe source declarations in terms of canonical IR
// types and the code generator supplies machine locations later.

typedef u32 DebugTypeId;
typedef u32 DebugScopeId;
typedef u32 DebugVariableId;

#define DEBUG_ID_INVALID UINT32_MAX
#define DEBUG_SCOPE_INVALID UINT32_MAX

typedef enum DebugTypeKind
{
    DEBUG_TYPE_BASE,
    DEBUG_TYPE_POINTER,
    DEBUG_TYPE_ARRAY,
    DEBUG_TYPE_STRUCT,
    DEBUG_TYPE_UNION,
    DEBUG_TYPE_ENUM,
    DEBUG_TYPE_TYPEDEF,
    DEBUG_TYPE_FUNCTION,
    DEBUG_TYPE_QUALIFIED,
    DEBUG_TYPE_VECTOR,
    DEBUG_TYPE_VOID,
    DEBUG_TYPE_COUNT,
} DebugTypeKind;

// Embedded in every debug type, field, variable, scope, and function row,
// so it is 20 bytes: the file is named by `source` (an index into the
// model's source_paths) rather than a per-record copy of the path, and
// offset/length are u32 like the IrSourceRange they come from. `line == 0`
// means the record carries no declaration.
typedef struct DebugSourceLocation DebugSourceLocation;
struct DebugSourceLocation
{
    u32 source;
    u32 line;
    u32 column;
    u32 offset;
    u32 length;
};

BUSTER_CT_CHECK(sizeof(DebugSourceLocation) == 20);

typedef struct DebugTypeField DebugTypeField;
struct DebugTypeField
{
    String8 name;
    DebugTypeId type;
    DebugSourceLocation declaration;
    u64 offset;
    u32 bit_offset;
    u32 bit_width;
    bool is_bit_field;
    u8 reserved[3];
};

typedef struct DebugEnumMember DebugEnumMember;
struct DebugEnumMember
{
    String8 name;
    DebugSourceLocation declaration;
    u64 value;
};

typedef struct DebugType DebugType;
struct DebugType
{
    String8 name;
    String8 declaration_name;
    IrTypeId canonical_type;
    DebugTypeId unqualified_type;
    DebugTypeId element_type;
    DebugTypeId return_type;
    DebugTypeField* fields;
    DebugEnumMember* enum_members;
    DebugTypeId* parameter_types;
    DebugSourceLocation declaration;
    DebugTypeKind kind;
    u64 size;
    u64 element_count;
    u32 alignment;
    u32 field_count;
    u32 enum_member_count;
    u32 parameter_count;
    u32 bit_width;
    bool is_signed;
    bool is_variadic;
    bool is_const;
    bool is_volatile;
    u8 reserved[4];
};

typedef enum DebugRegister
{
    DEBUG_REGISTER_NONE,
    DEBUG_REGISTER_X86_RAX,
    DEBUG_REGISTER_X86_RCX,
    DEBUG_REGISTER_X86_RDX,
    DEBUG_REGISTER_X86_RBX,
    DEBUG_REGISTER_X86_RSP,
    DEBUG_REGISTER_X86_RBP,
    DEBUG_REGISTER_X86_RSI,
    DEBUG_REGISTER_X86_RDI,
    DEBUG_REGISTER_X86_R8,
    DEBUG_REGISTER_X86_R9,
    DEBUG_REGISTER_X86_R10,
    DEBUG_REGISTER_X86_R11,
    DEBUG_REGISTER_X86_R12,
    DEBUG_REGISTER_X86_R13,
    DEBUG_REGISTER_X86_R14,
    DEBUG_REGISTER_X86_R15,
    DEBUG_REGISTER_X86_XMM0,
    DEBUG_REGISTER_X86_XMM1,
    DEBUG_REGISTER_X86_XMM2,
    DEBUG_REGISTER_X86_XMM3,
    DEBUG_REGISTER_X86_XMM4,
    DEBUG_REGISTER_X86_XMM5,
    DEBUG_REGISTER_X86_XMM6,
    DEBUG_REGISTER_X86_XMM7,
    DEBUG_REGISTER_X86_XMM8,
    DEBUG_REGISTER_X86_XMM9,
    DEBUG_REGISTER_X86_XMM10,
    DEBUG_REGISTER_X86_XMM11,
    DEBUG_REGISTER_X86_XMM12,
    DEBUG_REGISTER_X86_XMM13,
    DEBUG_REGISTER_X86_XMM14,
    DEBUG_REGISTER_X86_XMM15,
    DEBUG_REGISTER_AARCH64_X0,
    DEBUG_REGISTER_AARCH64_X1,
    DEBUG_REGISTER_AARCH64_X2,
    DEBUG_REGISTER_AARCH64_X3,
    DEBUG_REGISTER_AARCH64_X4,
    DEBUG_REGISTER_AARCH64_X5,
    DEBUG_REGISTER_AARCH64_X6,
    DEBUG_REGISTER_AARCH64_X7,
    DEBUG_REGISTER_AARCH64_X8,
    DEBUG_REGISTER_AARCH64_X9,
    DEBUG_REGISTER_AARCH64_X10,
    DEBUG_REGISTER_AARCH64_X11,
    DEBUG_REGISTER_AARCH64_X12,
    DEBUG_REGISTER_AARCH64_X13,
    DEBUG_REGISTER_AARCH64_X14,
    DEBUG_REGISTER_AARCH64_X15,
    DEBUG_REGISTER_AARCH64_X16,
    DEBUG_REGISTER_AARCH64_X17,
    DEBUG_REGISTER_AARCH64_X18,
    DEBUG_REGISTER_AARCH64_X19,
    DEBUG_REGISTER_AARCH64_X20,
    DEBUG_REGISTER_AARCH64_X21,
    DEBUG_REGISTER_AARCH64_X22,
    DEBUG_REGISTER_AARCH64_X23,
    DEBUG_REGISTER_AARCH64_X24,
    DEBUG_REGISTER_AARCH64_X25,
    DEBUG_REGISTER_AARCH64_X26,
    DEBUG_REGISTER_AARCH64_X27,
    DEBUG_REGISTER_AARCH64_X28,
    DEBUG_REGISTER_AARCH64_X29,
    DEBUG_REGISTER_AARCH64_X30,
    DEBUG_REGISTER_AARCH64_SP,
    DEBUG_REGISTER_AARCH64_V0,
    DEBUG_REGISTER_AARCH64_V1,
    DEBUG_REGISTER_AARCH64_V2,
    DEBUG_REGISTER_AARCH64_V3,
    DEBUG_REGISTER_AARCH64_V4,
    DEBUG_REGISTER_AARCH64_V5,
    DEBUG_REGISTER_AARCH64_V6,
    DEBUG_REGISTER_AARCH64_V7,
    DEBUG_REGISTER_AARCH64_V8,
    DEBUG_REGISTER_AARCH64_V9,
    DEBUG_REGISTER_AARCH64_V10,
    DEBUG_REGISTER_AARCH64_V11,
    DEBUG_REGISTER_AARCH64_V12,
    DEBUG_REGISTER_AARCH64_V13,
    DEBUG_REGISTER_AARCH64_V14,
    DEBUG_REGISTER_AARCH64_V15,
    DEBUG_REGISTER_AARCH64_V16,
    DEBUG_REGISTER_AARCH64_V17,
    DEBUG_REGISTER_AARCH64_V18,
    DEBUG_REGISTER_AARCH64_V19,
    DEBUG_REGISTER_AARCH64_V20,
    DEBUG_REGISTER_AARCH64_V21,
    DEBUG_REGISTER_AARCH64_V22,
    DEBUG_REGISTER_AARCH64_V23,
    DEBUG_REGISTER_AARCH64_V24,
    DEBUG_REGISTER_AARCH64_V25,
    DEBUG_REGISTER_AARCH64_V26,
    DEBUG_REGISTER_AARCH64_V27,
    DEBUG_REGISTER_AARCH64_V28,
    DEBUG_REGISTER_AARCH64_V29,
    DEBUG_REGISTER_AARCH64_V30,
    DEBUG_REGISTER_AARCH64_V31,
    DEBUG_REGISTER_COUNT,
} DebugRegister;

typedef enum DebugLocationKind
{
    DEBUG_LOCATION_REGISTER,
    DEBUG_LOCATION_FRAME,
    DEBUG_LOCATION_UNAVAILABLE,
    DEBUG_LOCATION_CONSTANT,
    DEBUG_LOCATION_PIECEWISE,
    DEBUG_LOCATION_COUNT,
} DebugLocationKind;

typedef struct DebugLocationPiece DebugLocationPiece;
struct DebugLocationPiece
{
    DebugLocationKind kind;
    DebugRegister reg;
    s32 frame_offset;
    u64 constant;
    u32 value_offset;
    u32 size;
};

typedef struct DebugLocation DebugLocation;
struct DebugLocation
{
    DebugLocationPiece* pieces;
    DebugLocationKind kind;
    DebugRegister reg;
    s32 frame_offset;
    u64 constant;
    u32 piece_count;
    u8 reserved[4];
};

typedef struct DebugLocationRange DebugLocationRange;
struct DebugLocationRange
{
    u32 start;
    u32 end;
    DebugLocation location;
};

typedef struct DebugLocationSeed DebugLocationSeed;
struct DebugLocationSeed
{
    IrSymbolId function_symbol;
    IrLocalId local;
    u32 start;
    u32 end;
    DebugLocation location;
};

// Accelerator over a `DebugLocationSeed` array, grouping seed indexes by their
// owning function symbol.  Every variable needs the seeds of exactly one
// symbol, and debug info is built for every function by default, so scanning
// the whole seed array per variable is quadratic across a translation unit.
typedef struct DebugLocationIndex DebugLocationIndex;
struct DebugLocationIndex
{
    // The exact array this index describes.  Consumers compare it against the
    // seeds they were handed and fall back to a linear scan on a mismatch, so a
    // stale index can never silently drop locations.
    DebugLocationSeed* locations;
    // `bucket_ends[bucket]` is the end offset of the bucket inside `order`; the
    // bucket starts at `bucket ? bucket_ends[bucket - 1] : 0`.
    u32* bucket_ends;
    u32* order;
    u32 bucket_count;
    u32 location_count;
};

typedef enum DebugVariableKind
{
    DEBUG_VARIABLE_PARAMETER,
    DEBUG_VARIABLE_LOCAL,
    DEBUG_VARIABLE_GLOBAL,
    DEBUG_VARIABLE_COUNT,
} DebugVariableKind;

typedef enum DebugScopeKind
{
    DEBUG_SCOPE_FUNCTION,
    DEBUG_SCOPE_LEXICAL,
    DEBUG_SCOPE_INLINE,
    DEBUG_SCOPE_COUNT,
} DebugScopeKind;

typedef struct DebugVariable DebugVariable;
struct DebugVariable
{
    String8 name;
    String8 linkage_name;
    DebugTypeId type;
    DebugSourceLocation declaration;
    DebugLocationRange* locations;
    IrSymbolId symbol;
    IrLocalId local;
    DebugScopeId scope;
    DebugVariableKind kind;
    u32 location_count;
    bool is_artificial;
    u8 reserved[3];
};

typedef struct DebugScope DebugScope;
struct DebugScope
{
    DebugScopeId parent;
    DebugSourceLocation declaration;
    DebugScopeKind kind;
    u32 start;
    u32 end;
    DebugVariableId* variables;
    u32 variable_count;
};

typedef struct DebugFunction DebugFunction;
struct DebugFunction
{
    String8 name;
    DebugSourceLocation declaration;
    IrSymbolId symbol;
    DebugTypeId type;
    DebugScopeId scope;
    u32 code_offset;
    u32 code_size;
    u32 variable_start;
    u32 variable_count;
};

typedef struct DebugInlineSite DebugInlineSite;
struct DebugInlineSite
{
    DebugFunction* function;
    DebugInlineSite* parent;
    DebugSourceLocation call_site;
    u32 start;
    u32 end;
    bool has_ranges;
    u8 reserved[3];
};

typedef struct DebugFunctionSeed DebugFunctionSeed;
struct DebugFunctionSeed
{
    String8 name;
    DebugSourceLocation declaration;
    IrSymbolId symbol;
    u32 code_offset;
    u32 code_size;
};

typedef struct DebugInlineSeed DebugInlineSeed;
struct DebugInlineSeed
{
    u32 function_index;
    u32 parent_index;
    DebugSourceLocation call_site;
    u32 start;
    u32 end;
};

typedef struct DebugModelInput DebugModelInput;
struct DebugModelInput
{
    IrProgram* program;
    IrModule* module;
    String8 producer;
    String8 comp_dir;
    DebugFunctionSeed* functions;
    DebugLocationSeed* locations;
    DebugInlineSeed* inline_sites;
    // Optional; `debug_model_build` builds one for its own use when a caller
    // leaves this null.  It is only consulted when it describes exactly the
    // `locations`/`location_count` pair beside it.
    DebugLocationIndex* location_index;
    u32 function_count;
    u32 location_count;
    u32 inline_site_count;
};

typedef struct DebugModel DebugModel;
struct DebugModel
{
    String8 producer;
    String8 comp_dir;
    String8* source_paths;
    DebugType* types;
    DebugFunction* functions;
    DebugScope* scopes;
    DebugVariable* variables;
    DebugInlineSite* inline_sites;
    u32 source_count;
    DebugScopeId root_scope;
    u32 type_count;
    u32 function_count;
    u32 scope_count;
    u32 variable_count;
    u32 inline_site_count;
    bool valid;
    u8 reserved[3];
};

BUSTER_F_DECL DebugLocationIndex debug_location_index_build(Arena* arena, DebugLocationSeed* locations, u32 location_count);
BUSTER_F_DECL DebugModel debug_model_build(Arena* arena, DebugModelInput input);
BUSTER_F_DECL DebugTypeId debug_model_find_canonical_type(DebugModel* model, IrTypeId type);
BUSTER_F_DECL u32 debug_register_dwarf_number(Target target, DebugRegister reg);
BUSTER_F_DECL u32 debug_register_codeview_number(Target target, DebugRegister reg);
