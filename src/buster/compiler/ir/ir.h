#pragma once
#include <buster/base.h>
#include <buster/target.h>

typedef struct InternTable
{
    Arena* string_arena;
    Arena* slot_arena;
    Arena* hash_table_arena;
    u32 hash_table_item_count;
    u32 reserved;
} InternTable;

typedef enum IrValueId
{
    IR_VALUE_CONSTANT_INTEGER,
    IR_VALUE_COUNT,
} IrValueId;

typedef struct InternSlot InternSlot;
struct InternSlot
{
    String8 name;
    u64 hash;
};

typedef struct IrRef { u32 v; } IrRef;
BUSTER_CT_CHECK(sizeof(IrRef) == sizeof(u32));
#define ir_ref_get_raw(x) (((x).v).v)
#define ir_ref_is_valid(x) (ir_ref_get_raw(x) != 0)
#define ir_ref_get(x) (ir_ref_get_raw(x) - 1)
#define ir_ref_unwrap(x) (BUSTER_CHECK(ir_ref_is_valid(x)), ir_ref_get(x))

#define IR_REF_DECL(T) typedef struct T ## Ref { IrRef v; } T ## Ref

IR_REF_DECL(IrType);
IR_REF_DECL(IrDebugType);
IR_REF_DECL(IrInternSlot);
IR_REF_DECL(IrBasicBlock);
IR_REF_DECL(IrInstruction);
IR_REF_DECL(IrFunction);
IR_REF_DECL(IrFunctionDeclaration);
IR_REF_DECL(IrValue);
IR_REF_DECL(IrBlock);

IR_REF_DECL(IrConstant1);
IR_REF_DECL(IrConstant2);
IR_REF_DECL(IrConstant4);
IR_REF_DECL(IrConstant8);

typedef enum IrConstantDataId
{
    IR_CONSTANT_DATA_8,
    IR_CONSTANT_DATA_16,
    IR_CONSTANT_DATA_32,
    IR_CONSTANT_DATA_64,
    IR_CONSTANT_DATA_128,
    IR_CONSTANT_DATA_256,
    IR_CONSTANT_DATA_512,
    IR_CONSTANT_DATA_COUNT,
} IrConstantDataId;

typedef struct SliceIrInternSlotRef SliceIrInternSlotRef;
struct SliceIrInternSlotRef
{
    IrInternSlotRef* pointer;
    u64 length;
};

typedef struct SliceIrTypeRef SliceIrTypeRef;
struct SliceIrTypeRef
{
    IrTypeRef* pointer;
    u64 length;
};

typedef struct SliceIrDebugTypeRef SliceIrDebugTypeRef;
struct SliceIrDebugTypeRef
{
    IrDebugTypeRef* pointer;
    u64 length;
};

typedef struct IrValue
{
    union
    {
        IrConstant1Ref constant1;
        IrConstant2Ref constant2;
        IrConstant4Ref constant4;
        IrConstant8Ref constant8;
    };
    IrTypeRef type;
    IrValueId id;
} IrValue;

typedef enum IrStatementId
{
    IR_STATEMENT_RETURN,
    IR_STATEMENT_COUNT,
} IrStatementId;

typedef enum IrCallingConvention
{
    IR_CALLING_CONVENTION_C,
    IR_CALLING_CONVENTION_SYSTEMV,
    IR_CALLING_CONVENTION_WIN64,
    IR_CALLING_CONVENTION_COUNT,
} IrCallingConvention;

typedef enum IrInstructionId
{
    IR_INSTRUCTION_RETURN,
    IR_INSTRUCTION_COUNT,
} IrInstructionId;

typedef struct IrInstruction IrInstruction;
struct IrInstruction
{
    IrValue* value;
    IrInstructionRef previous;
    IrInstructionRef next;
    IrInstructionId id;
    u8 reserved[4];
};

typedef struct IrBasicBlock IrBasicBlock;
struct IrBasicBlock
{
    IrInstructionRef first;
    IrInstructionRef last;
};

typedef struct IrFunctionAttributes IrFunctionAttributes;
struct IrFunctionAttributes
{
    IrCallingConvention calling_convention;
};

typedef enum IrLinkage
{
    IR_LINKAGE_INTERNAL,
    IR_LINKAGE_EXTERNAL,
    IR_LINKAGE_COUNT,
} IrLinkage;

typedef struct IrSymbolAttributes IrSymbolAttributes;
struct IrSymbolAttributes
{
    IrLinkage linkage;
    bool exported;
};

// We need to hold some type of debug type reference in order to maintain semantics, since semantics are not fully explained by semantic/ABI IR types
typedef struct IrTypeFunctionCreationArguments IrTypeFunctionCreationArguments;
struct IrTypeFunctionCreationArguments
{
    IrTypeRef* argument_types;
    Target* target;
    IrDebugTypeRef debug_type;
    IrTypeRef return_type;
    u16 argument_count;
    IrFunctionAttributes attributes;
    u8 reserved[7];
};

typedef struct IrDebugTypeFunctionCreationArguments IrDebugTypeFunctionCreationArguments;
struct IrDebugTypeFunctionCreationArguments
{
    IrDebugTypeRef* argument_types;
    IrDebugTypeRef return_type;
    IrFunctionAttributes attributes;
    u16 argument_count;
    u8 reserved[1];
};

typedef struct IrFunctionType IrFunctionType;
struct IrFunctionType
{
    // IrType* return_type;
    // IrType** argument_types;
    u64 argument_count;
    Target* target;
    // IrType type;
    IrFunctionAttributes attributes;
    u8 reserved[7];
};

typedef enum IrGlobalSymbolId
{
    IR_GLOBAL_SYMBOL_FUNCTION,
    IR_GLOBAL_SYMBOL_VARIABLE,
    IR_GLOBAL_SYMBOL_COUNT,
} IrGlobalSymbolId;

typedef struct IrGlobalSymbol IrGlobalSymbol;
struct IrGlobalSymbol
{
    IrInternSlotRef name;
    IrTypeRef type;
    IrDebugTypeRef debug_type;
    IrGlobalSymbolId id;
    IrLinkage linkage;
    u8 reserved[2];
};

typedef struct IrGlobalVariable IrGlobalVariable;
struct IrGlobalVariable
{
    IrGlobalSymbol symbol;
};

typedef struct IrFunctionDeclaration IrFunctionDeclaration;
struct IrFunctionDeclaration
{
    IrGlobalSymbol symbol;
};

typedef struct IrFunction IrFunction;
struct IrFunction
{
    IrInternSlotRef* argument_names;
    IrFunctionDeclaration declaration;
    IrBasicBlockRef entry_block;
    IrBasicBlockRef current_basic_block;
};

typedef struct SliceIrFunction SliceIrFunction;
struct SliceIrFunction
{
    IrFunction* pointer;
    u64 length;
};

enum IrTypeId
{
    IR_TYPE_I1,
    IR_TYPE_I2,
    IR_TYPE_I3,
    IR_TYPE_I4,
    IR_TYPE_I5,
    IR_TYPE_I6,
    IR_TYPE_I7,
    IR_TYPE_I8,
    IR_TYPE_I9,
    IR_TYPE_I10,
    IR_TYPE_I11,
    IR_TYPE_I12,
    IR_TYPE_I13,
    IR_TYPE_I14,
    IR_TYPE_I15,
    IR_TYPE_I16,
    IR_TYPE_I17,
    IR_TYPE_I18,
    IR_TYPE_I19,
    IR_TYPE_I20,
    IR_TYPE_I21,
    IR_TYPE_I22,
    IR_TYPE_I23,
    IR_TYPE_I24,
    IR_TYPE_I25,
    IR_TYPE_I26,
    IR_TYPE_I27,
    IR_TYPE_I28,
    IR_TYPE_I29,
    IR_TYPE_I30,
    IR_TYPE_I31,
    IR_TYPE_I32,
    IR_TYPE_I33,
    IR_TYPE_I34,
    IR_TYPE_I35,
    IR_TYPE_I36,
    IR_TYPE_I37,
    IR_TYPE_I38,
    IR_TYPE_I39,
    IR_TYPE_I40,
    IR_TYPE_I41,
    IR_TYPE_I42,
    IR_TYPE_I43,
    IR_TYPE_I44,
    IR_TYPE_I45,
    IR_TYPE_I46,
    IR_TYPE_I47,
    IR_TYPE_I48,
    IR_TYPE_I49,
    IR_TYPE_I50,
    IR_TYPE_I51,
    IR_TYPE_I52,
    IR_TYPE_I53,
    IR_TYPE_I54,
    IR_TYPE_I55,
    IR_TYPE_I56,
    IR_TYPE_I57,
    IR_TYPE_I58,
    IR_TYPE_I59,
    IR_TYPE_I60,
    IR_TYPE_I61,
    IR_TYPE_I62,
    IR_TYPE_I63,
    IR_TYPE_I64,
    IR_TYPE_I128,
    IR_TYPE_VOID,
    IR_TYPE_POINTER,
    IR_TYPE_HF16,
    IR_TYPE_BF16,
    IR_TYPE_F32,
    IR_TYPE_F64,
    IR_TYPE_F128,
    IR_TYPE_V64,
    IR_TYPE_V128,
    IR_TYPE_V256,
    IR_TYPE_V512,
    IR_TYPE_VECTOR, // VECTOR IS NOT BUILTIN
    IR_TYPE_AGGREGATE,
    IR_TYPE_FUNCTION
};
typedef u8 IrTypeId;

#define IR_TYPE_BUILTIN_COUNT IR_TYPE_VECTOR

enum IrDebugTypeId
{
      IR_DEBUG_TYPE_U1,
      IR_DEBUG_TYPE_U2,
      IR_DEBUG_TYPE_U3,
      IR_DEBUG_TYPE_U4,
      IR_DEBUG_TYPE_U5,
      IR_DEBUG_TYPE_U6,
      IR_DEBUG_TYPE_U7,
      IR_DEBUG_TYPE_U8,
      IR_DEBUG_TYPE_U9,
      IR_DEBUG_TYPE_U10,
      IR_DEBUG_TYPE_U11,
      IR_DEBUG_TYPE_U12,
      IR_DEBUG_TYPE_U13,
      IR_DEBUG_TYPE_U14,
      IR_DEBUG_TYPE_U15,
      IR_DEBUG_TYPE_U16,
      IR_DEBUG_TYPE_U17,
      IR_DEBUG_TYPE_U18,
      IR_DEBUG_TYPE_U19,
      IR_DEBUG_TYPE_U20,
      IR_DEBUG_TYPE_U21,
      IR_DEBUG_TYPE_U22,
      IR_DEBUG_TYPE_U23,
      IR_DEBUG_TYPE_U24,
      IR_DEBUG_TYPE_U25,
      IR_DEBUG_TYPE_U26,
      IR_DEBUG_TYPE_U27,
      IR_DEBUG_TYPE_U28,
      IR_DEBUG_TYPE_U29,
      IR_DEBUG_TYPE_U30,
      IR_DEBUG_TYPE_U31,
      IR_DEBUG_TYPE_U32,
      IR_DEBUG_TYPE_U33,
      IR_DEBUG_TYPE_U34,
      IR_DEBUG_TYPE_U35,
      IR_DEBUG_TYPE_U36,
      IR_DEBUG_TYPE_U37,
      IR_DEBUG_TYPE_U38,
      IR_DEBUG_TYPE_U39,
      IR_DEBUG_TYPE_U40,
      IR_DEBUG_TYPE_U41,
      IR_DEBUG_TYPE_U42,
      IR_DEBUG_TYPE_U43,
      IR_DEBUG_TYPE_U44,
      IR_DEBUG_TYPE_U45,
      IR_DEBUG_TYPE_U46,
      IR_DEBUG_TYPE_U47,
      IR_DEBUG_TYPE_U48,
      IR_DEBUG_TYPE_U49,
      IR_DEBUG_TYPE_U50,
      IR_DEBUG_TYPE_U51,
      IR_DEBUG_TYPE_U52,
      IR_DEBUG_TYPE_U53,
      IR_DEBUG_TYPE_U54,
      IR_DEBUG_TYPE_U55,
      IR_DEBUG_TYPE_U56,
      IR_DEBUG_TYPE_U57,
      IR_DEBUG_TYPE_U58,
      IR_DEBUG_TYPE_U59,
      IR_DEBUG_TYPE_U60,
      IR_DEBUG_TYPE_U61,
      IR_DEBUG_TYPE_U62,
      IR_DEBUG_TYPE_U63,
      IR_DEBUG_TYPE_U64,
      IR_DEBUG_TYPE_S1,
      IR_DEBUG_TYPE_S2,
      IR_DEBUG_TYPE_S3,
      IR_DEBUG_TYPE_S4,
      IR_DEBUG_TYPE_S5,
      IR_DEBUG_TYPE_S6,
      IR_DEBUG_TYPE_S7,
      IR_DEBUG_TYPE_S8,
      IR_DEBUG_TYPE_S9,
      IR_DEBUG_TYPE_S10,
      IR_DEBUG_TYPE_S11,
      IR_DEBUG_TYPE_S12,
      IR_DEBUG_TYPE_S13,
      IR_DEBUG_TYPE_S14,
      IR_DEBUG_TYPE_S15,
      IR_DEBUG_TYPE_S16,
      IR_DEBUG_TYPE_S17,
      IR_DEBUG_TYPE_S18,
      IR_DEBUG_TYPE_S19,
      IR_DEBUG_TYPE_S20,
      IR_DEBUG_TYPE_S21,
      IR_DEBUG_TYPE_S22,
      IR_DEBUG_TYPE_S23,
      IR_DEBUG_TYPE_S24,
      IR_DEBUG_TYPE_S25,
      IR_DEBUG_TYPE_S26,
      IR_DEBUG_TYPE_S27,
      IR_DEBUG_TYPE_S28,
      IR_DEBUG_TYPE_S29,
      IR_DEBUG_TYPE_S30,
      IR_DEBUG_TYPE_S31,
      IR_DEBUG_TYPE_S32,
      IR_DEBUG_TYPE_S33,
      IR_DEBUG_TYPE_S34,
      IR_DEBUG_TYPE_S35,
      IR_DEBUG_TYPE_S36,
      IR_DEBUG_TYPE_S37,
      IR_DEBUG_TYPE_S38,
      IR_DEBUG_TYPE_S39,
      IR_DEBUG_TYPE_S40,
      IR_DEBUG_TYPE_S41,
      IR_DEBUG_TYPE_S42,
      IR_DEBUG_TYPE_S43,
      IR_DEBUG_TYPE_S44,
      IR_DEBUG_TYPE_S45,
      IR_DEBUG_TYPE_S46,
      IR_DEBUG_TYPE_S47,
      IR_DEBUG_TYPE_S48,
      IR_DEBUG_TYPE_S49,
      IR_DEBUG_TYPE_S50,
      IR_DEBUG_TYPE_S51,
      IR_DEBUG_TYPE_S52,
      IR_DEBUG_TYPE_S53,
      IR_DEBUG_TYPE_S54,
      IR_DEBUG_TYPE_S55,
      IR_DEBUG_TYPE_S56,
      IR_DEBUG_TYPE_S57,
      IR_DEBUG_TYPE_S58,
      IR_DEBUG_TYPE_S59,
      IR_DEBUG_TYPE_S60,
      IR_DEBUG_TYPE_S61,
      IR_DEBUG_TYPE_S62,
      IR_DEBUG_TYPE_S63,
      IR_DEBUG_TYPE_S64,
      IR_DEBUG_TYPE_U128,
      IR_DEBUG_TYPE_S128,
      IR_DEBUG_TYPE_VOID,
      IR_DEBUG_TYPE_NORETURN,
      IR_DEBUG_TYPE_HF16, // 16-bit “brain” floating-point value (7-bit significand). Provides the same number of exponent bits as float, so that it matches its dynamic range, but with greatly reduced precision. Used in Intel’s AVX-512 BF16 extensions and Arm’s ARMv8.6-A extensions, among others.
      IR_DEBUG_TYPE_F16, // IEEE 754 binary16 (_Float16)
      IR_DEBUG_TYPE_F32, // IEEE 754 binary32 (float)
      IR_DEBUG_TYPE_F64, // IEEE 754 binary64 (double)
      IR_DEBUG_TYPE_F128, // IEEE 754 binary128 (__float128)
      IR_DEBUG_TYPE_POINTER, // Pointer is not builtin
      IR_DEBUG_TYPE_VECTOR,
      IR_DEBUG_TYPE_AGGREGATE,
      IR_DEBUG_TYPE_FUNCTION,
};
typedef u8 IrDebugTypeId;

#define IR_DEBUG_TYPE_BUILTIN_COUNT IR_DEBUG_TYPE_POINTER

typedef struct IrModule IrModule;
struct IrModule
{
    Arena* untyped_arena;
    Arena* function_arena;
    Arena* value_arena;
    Arena* global_variable_arena;
    Arena* block_arena;
    Arena* statement_arena;
    Arena* basic_block_arena;
    Arena* constant_arenas[(u64)IR_CONSTANT_DATA_COUNT];
    Target* default_target;
    InternTable intern_table;
    String8 name;
    struct
    {
        struct
        {
            Arena* ir;
            Arena* debug;
        } arenas;
        struct
        {
            IrTypeRef ir[IR_TYPE_BUILTIN_COUNT];
            IrDebugTypeRef debug[IR_DEBUG_TYPE_BUILTIN_COUNT];
        } builtin;
        struct
        {
            IrTypeRef first_function_type;
        } ir;
        struct
        {
            IrDebugTypeRef first_pointer_type;
            IrDebugTypeRef first_function_type;
        } debug;
    } types;
};

typedef struct IrDebugScope IrDebugScope;
struct IrDebugScope
{
    u8 foo;
};

typedef struct IrBlock IrBlock;
struct IrBlock
{
    u32 line;
    u32 column;
};

typedef struct IrStatement IrStatement;
struct IrStatement
{
    u32 line;
    u32 column;
    IrBlockRef block;
};

typedef struct IrBuilder IrBuilder;
struct IrBuilder
{
    u32 line;
    u32 column;
    IrBlockRef block;
};

typedef struct IrFunctionCreate IrFunctionCreate;
struct IrFunctionCreate
{
    IrTypeRef ir_type;
    IrDebugTypeRef debug_type;
    IrInternSlotRef name;
    IrLinkage linkage;
    u8 reserved[3];
    IrInternSlotRef* argument_names;
};

typedef struct IrType IrType;
struct IrType
{
    union
    {
        struct
        {
            struct
            {
                IrTypeRef* argument_types;
                IrTypeRef return_type;
                IrDebugTypeRef debug_type_ref;
                u16 argument_count;
                u8 reserved[6];
            } semantic;
            struct
            {
                u16 argument_count;
            } abi;
            IrFunctionAttributes attributes;
            u8 reserved[5];
            Target* target;
        } function;
    };
    IrInternSlotRef name;
    IrTypeRef kind_next;
    IrTypeId id;
    u8 reserved[7];
};

typedef struct IrDebugType IrDebugType;
struct IrDebugType
{
    union
    {
        struct
        {
            IrDebugTypeRef* argument_types;
            IrDebugTypeRef return_type;
            u16 argument_count;
            IrFunctionAttributes attributes;
            u8 reserved[1];
        } function;
        struct
        {
            IrDebugTypeRef element_type;
        } pointer;
    };
    IrInternSlotRef name;
    IrDebugTypeRef kind_next;
    IrDebugTypeId id;
    u8 reserved[7];
};

BUSTER_F_DECL SliceIrFunction ir_module_get_functions(IrModule* module);
BUSTER_F_DECL IrModule* ir_create_mock_module(Arena* arena);
BUSTER_F_DECL IrModule* ir_module_create(Arena* arena, Target* target, String8 name);

BUSTER_F_DECL IrTypeRef ir_type_get_builtin(IrModule* module, IrTypeId id);

BUSTER_F_DECL IrTypeRef ir_type_get_function(IrModule* module, IrTypeFunctionCreationArguments arguments);

BUSTER_F_DECL IrDebugTypeRef ir_debug_type_get_builtin(IrModule* module, IrDebugTypeId id);
BUSTER_F_DECL IrDebugTypeRef ir_debug_type_get_pointer(IrModule* module, IrDebugTypeRef element_type);
BUSTER_F_DECL IrDebugTypeRef ir_debug_type_get_function(IrModule* module, IrDebugTypeFunctionCreationArguments arguments);

BUSTER_F_DECL IrFunctionRef ir_function_create(IrModule* module, IrFunctionCreate create);
BUSTER_F_DECL IrFunction* ir_function_get(IrModule* module, IrFunctionRef function_ref);

BUSTER_F_DECL IrType* ir_type_get(IrModule* module, IrTypeRef reference);
BUSTER_F_DECL IrDebugType* ir_debug_type_get(IrModule* module, IrDebugTypeRef reference);

BUSTER_F_DECL IrInternSlotRef ir_module_intern(IrModule* module, String8 string);
BUSTER_F_DECL SliceIrInternSlotRef ir_module_allocate_name_array(IrModule* module, u64 count);
BUSTER_F_DECL SliceIrDebugTypeRef ir_module_allocate_debug_type_array(IrModule* module, u64 count);
BUSTER_F_DECL SliceIrTypeRef ir_module_allocate_type_array(IrModule* module, u64 count);
BUSTER_F_DECL IrValueRef ir_get_constant_integer(IrModule* module, IrTypeRef type_ref, u64 value);

BUSTER_F_DECL IrBlockRef ir_create_block(IrModule* module);
BUSTER_F_DECL IrBasicBlockRef ir_create_basic_block(IrModule* module);
IrBasicBlock* ir_basic_block_get(IrModule* module, IrBasicBlockRef reference);
#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL bool ir_tests(UnitTestArguments* arguments);
#endif
