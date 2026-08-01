#pragma once

#include <buster/base.h>

typedef u32 IrIdUnderlying;

typedef struct IrTypeId IrTypeId;
struct IrTypeId
{
    IrIdUnderlying value;
};

typedef struct IrSymbolId IrSymbolId;
struct IrSymbolId
{
    IrIdUnderlying value;
};

typedef struct IrLocalId IrLocalId;
struct IrLocalId
{
    IrIdUnderlying value;
};

typedef struct IrSourceId IrSourceId;
struct IrSourceId
{
    IrIdUnderlying value;
};

#define IR_ID_UNDERLYING_INVALID UINT32_MAX
#define IR_TYPE_ID_INVALID ((IrTypeId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_SYMBOL_ID_INVALID ((IrSymbolId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_LOCAL_ID_INVALID ((IrLocalId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_SOURCE_ID_INVALID ((IrSourceId){.value = IR_ID_UNDERLYING_INVALID})

BUSTER_CT_CHECK(sizeof(IrTypeId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrSymbolId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrLocalId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrSourceId) == sizeof(IrIdUnderlying));

typedef struct IrSourceRange IrSourceRange;
struct IrSourceRange
{
    IrSourceId source;
    u64 offset;
    u64 length;
    u32 line;
    u32 column;
};

typedef enum IrTypeKind
{
    IR_TYPE_VOID,
    IR_TYPE_BOOLEAN,
    IR_TYPE_INTEGER,
    IR_TYPE_FLOAT,
    IR_TYPE_VA_LIST,
    IR_TYPE_POINTER,
    IR_TYPE_SLICE,
    IR_TYPE_ARRAY,
    IR_TYPE_VECTOR,
    IR_TYPE_FUNCTION,
    IR_TYPE_RANGE,
    IR_TYPE_STRUCT,
    IR_TYPE_UNION,
    IR_TYPE_ENUM,
    IR_TYPE_COUNT,
} IrTypeKind;

typedef enum IrCallingConvention
{
    IR_CALLING_CONVENTION_C,
    IR_CALLING_CONVENTION_SYSTEMV,
    IR_CALLING_CONVENTION_WIN64,
    IR_CALLING_CONVENTION_COUNT,
} IrCallingConvention;

typedef enum IrAbiClass
{
    IR_ABI_CLASS_NONE,
    IR_ABI_CLASS_INTEGER,
    IR_ABI_CLASS_FLOAT,
    IR_ABI_CLASS_VECTOR,
    IR_ABI_CLASS_POINTER,
    IR_ABI_CLASS_AGGREGATE,
    IR_ABI_CLASS_MEMORY,
    IR_ABI_CLASS_COUNT,
} IrAbiClass;

typedef struct IrTypeLayout IrTypeLayout;
struct IrTypeLayout
{
    u64 size;
    u32 alignment;
    IrAbiClass abi_class;
    bool resolved;
    u8 reserved[3];
};

typedef struct IrField IrField;
struct IrField
{
    String8 name;
    IrSourceRange source;
    IrTypeId type;
    u64 offset;
    u32 bit_offset;
    u32 bit_width;
    bool is_bit_field;
    u8 reserved[7];
};

typedef struct IrEnumMember IrEnumMember;
struct IrEnumMember
{
    String8 name;
    IrSourceRange source;
    u64 value;
};

typedef struct IrType IrType;
struct IrType
{
    String8 name;
    IrField* fields;
    IrEnumMember* enum_members;
    IrTypeId* parameter_types;
    IrTypeId id;
    IrTypeId element_type;
    IrTypeId return_type;
    IrTypeId unqualified_type;
    IrTypeLayout layout;
    IrTypeKind kind;
    IrCallingConvention calling_convention;
    u64 element_count;
    u32 field_count;
    u32 enum_member_count;
    u32 parameter_count;
    u32 bit_width;
    bool is_signed;
    bool is_variadic;
    bool is_atomic;
    bool is_nullptr;
};

typedef struct IrTypeTable IrTypeTable;
struct IrTypeTable
{
    IrType* types;
    u32 count;
    u32 capacity;
};

typedef enum IrSymbolKind
{
    IR_SYMBOL_FUNCTION,
    IR_SYMBOL_DATA,
    IR_SYMBOL_TYPE,
    IR_SYMBOL_COUNT,
} IrSymbolKind;

typedef enum IrLinkage
{
    IR_LINKAGE_INTERNAL,
    IR_LINKAGE_EXTERNAL,
    IR_LINKAGE_IMPORT,
    IR_LINKAGE_COUNT,
} IrLinkage;

typedef struct IrSymbol IrSymbol;
struct IrSymbol
{
    String8 name;
    String8 link_name;
    IrSourceRange source;
    IrTypeId type;
    IrSymbolId id;
    IrSymbolKind kind;
    IrLinkage linkage;
    bool is_definition;
    bool is_thread_local;
    u8 reserved[2];
};

typedef struct IrSymbolTable IrSymbolTable;
struct IrSymbolTable
{
    IrSymbol* symbols;
    u32 count;
    u32 capacity;
};

typedef struct IrSource IrSource;
struct IrSource
{
    String8 path;
    IrSourceId id;
};

typedef struct IrSourceTable IrSourceTable;
struct IrSourceTable
{
    IrSource* sources;
    u32 count;
    u32 capacity;
};

BUSTER_F_DECL IrType* ir_type_from_id(IrTypeTable* table, IrTypeId id);
BUSTER_F_DECL IrSymbol* ir_symbol_from_id(IrSymbolTable* table, IrSymbolId id);
BUSTER_F_DECL IrSource* ir_source_from_id(IrSourceTable* table, IrSourceId id);
