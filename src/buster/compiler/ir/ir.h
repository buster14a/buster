#pragma once
#include <buster/base.h>
#include <buster/target.h>

STRUCT(InternTable)
{
    Arena* string_arena;
    Arena* slot_arena;
    Arena* hash_table_arena;
    u32 hash_table_item_count;
    u32 reserved;
};

ENUM(IrValueId,
    ConstantInteger);

struct IrRef
{
    u32 v;

    BUSTER_INLINE bool is_valid() { return v != 0; }
    BUSTER_INLINE u32 get() { BUSTER_CHECK(is_valid()); return v - 1; }

    BUSTER_INLINE bool operator==(IrRef other)
    {
        return get() == other.get();
    }
};
static_assert(sizeof(IrRef) == 4);

STRUCT(InternSlot)
{
    String8 name;
    u64 hash;
};

template<typename T>
struct TypedIrRef : public IrRef{};

typedef TypedIrRef<struct IrType> IrTypeRef;
typedef TypedIrRef<struct IrDebugType> IrDebugTypeRef;
typedef TypedIrRef<struct IrInternSlot> IrInternRef;
typedef TypedIrRef<struct IrBasicBlock> IrBasicBlockRef;
typedef TypedIrRef<struct IrInstruction> IrInstructionRef;
typedef TypedIrRef<struct IrFunction> IrFunctionRef;
typedef TypedIrRef<struct IrFunctionDeclaration> IrFunctionDeclarationRef;
typedef TypedIrRef<struct IrValue> IrValueRef;
typedef TypedIrRef<struct IrBlock> IrBlockRef;
typedef TypedIrRef<struct IrDebugType> IrDebugTypeRef;
typedef TypedIrRef<struct IrDebugType> IrDebugTypeRef;
typedef TypedIrRef<struct IrDebugType> IrDebugTypeRef;
typedef TypedIrRef<struct IrDebugType> IrDebugTypeRef;

typedef TypedIrRef<u8> IrConstant1Ref;
typedef TypedIrRef<u16> IrConstant2Ref;
typedef TypedIrRef<u32> IrConstant4Ref;
typedef TypedIrRef<u64> IrConstant8Ref;

ENUM_T(IrConstantDataId, u8,
        C8,
        C16,
        C32,
        C64,
        C128,
        C256,
        C512);

STRUCT(IrValue)
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
};

ENUM_T(IrStatementId, u8,
    Return);

ENUM_T(IrCallingConvention, u8,
    C,
    SystemV,
    Win64);

ENUM(IrInstructionId,
    Return);

STRUCT(IrInstruction)
{
    IrValue* value;
    IrInstructionRef previous;
    IrInstructionRef next;
    IrInstructionId id;
    u8 reserved[4];
};

STRUCT(IrBasicBlock)
{
    IrInstructionRef first;
    IrInstructionRef last;
};

ENUM(IrFunctionAttribute,
    CallingConvention);

STRUCT(IrFunctionAttributes)
{
    IrCallingConvention calling_convention;
};

ENUM_T(IrLinkage, u8,
    Internal,
    External);

ENUM_T(IrSymbolAttribute, u8,
      Export);

STRUCT(IrSymbolAttributes)
{
    IrLinkage linkage;
    bool exported;
};

// We need to hold some type of debug type reference in order to maintain semantics, since semantics are not fully explained by semantic/ABI IR types
STRUCT(IrTypeFunctionCreationArguments)
{
    Slice<IrTypeRef> argument_types;
    Target* target;
    IrDebugTypeRef debug_type;
    IrTypeRef return_type;
    IrFunctionAttributes attributes;
    u8 reserved[7];
};

STRUCT(IrDebugTypeFunctionCreationArguments)
{
    Slice<IrDebugTypeRef> argument_types;
    IrDebugTypeRef return_type;
    IrFunctionAttributes attributes;
    u8 reserved[3];
};

STRUCT(IrFunctionType)
{
    // IrType* return_type;
    // IrType** argument_types;
    u64 argument_count;
    Target* target;
    // IrType type;
    IrFunctionAttributes attributes;
    u8 reserved[7];
};

ENUM_T(IrGlobalSymbolId, u8,
    Function,
    Variable);

STRUCT(IrGlobalSymbol)
{
    IrInternRef name;
    IrTypeRef type;
    IrDebugTypeRef debug_type;
    IrGlobalSymbolId id;
    IrLinkage linkage;
    u8 reserved[2];
};

STRUCT(IrGlobalVariable)
{
    IrGlobalSymbol symbol;
};

STRUCT(IrFunctionDeclaration)
{
    IrGlobalSymbol symbol;
};

STRUCT(IrFunction)
{
    IrInternRef* argument_names;
    IrFunctionDeclaration declaration;
    IrBasicBlockRef entry_block;
    IrBasicBlockRef current_basic_block;
};

ENUM_T(IrTypeId, u8,
      i1,
      i2,
      i3,
      i4,
      i5,
      i6,
      i7,
      i8,
      i9,
      i10,
      i11,
      i12,
      i13,
      i14,
      i15,
      i16,
      i17,
      i18,
      i19,
      i20,
      i21,
      i22,
      i23,
      i24,
      i25,
      i26,
      i27,
      i28,
      i29,
      i30,
      i31,
      i32,
      i33,
      i34,
      i35,
      i36,
      i37,
      i38,
      i39,
      i40,
      i41,
      i42,
      i43,
      i44,
      i45,
      i46,
      i47,
      i48,
      i49,
      i50,
      i51,
      i52,
      i53,
      i54,
      i55,
      i56,
      i57,
      i58,
      i59,
      i60,
      i61,
      i62,
      i63,
      i64,
      i128,
      Void,
      pointer,
      hf16,
      bf16,
      f32,
      f64,
      f128,
      v64,
      v128,
      v256,
      v512,
      vector, // Vector is not builtin
      aggregate,
      function);

BUSTER_GLOBAL_LOCAL constexpr let ir_type_builtin_count = (BUSTER_UNDERLYING_TYPE(IrTypeId))IrTypeId::vector;

ENUM_T(IrDebugTypeId, u8,
      u1,
      u2,
      u3,
      u4,
      u5,
      u6,
      u7,
      u8,
      u9,
      u10,
      u11,
      u12,
      u13,
      u14,
      u15,
      u16,
      u17,
      u18,
      u19,
      u20,
      u21,
      u22,
      u23,
      u24,
      u25,
      u26,
      u27,
      u28,
      u29,
      u30,
      u31,
      u32,
      u33,
      u34,
      u35,
      u36,
      u37,
      u38,
      u39,
      u40,
      u41,
      u42,
      u43,
      u44,
      u45,
      u46,
      u47,
      u48,
      u49,
      u50,
      u51,
      u52,
      u53,
      u54,
      u55,
      u56,
      u57,
      u58,
      u59,
      u60,
      u61,
      u62,
      u63,
      u64,
      s1,
      s2,
      s3,
      s4,
      s5,
      s6,
      s7,
      s8,
      s9,
      s10,
      s11,
      s12,
      s13,
      s14,
      s15,
      s16,
      s17,
      s18,
      s19,
      s20,
      s21,
      s22,
      s23,
      s24,
      s25,
      s26,
      s27,
      s28,
      s29,
      s30,
      s31,
      s32,
      s33,
      s34,
      s35,
      s36,
      s37,
      s38,
      s39,
      s40,
      s41,
      s42,
      s43,
      s44,
      s45,
      s46,
      s47,
      s48,
      s49,
      s50,
      s51,
      s52,
      s53,
      s54,
      s55,
      s56,
      s57,
      s58,
      s59,
      s60,
      s61,
      s62,
      s63,
      s64,
      u128,
      s128,
      Void,
      NoReturn,
      hf16, // 16-bit “brain” floating-point value (7-bit significand). Provides the same number of exponent bits as float, so that it matches its dynamic range, but with greatly reduced precision. Used in Intel’s AVX-512 BF16 extensions and Arm’s ARMv8.6-A extensions, among others.
      f16, // IEEE 754 binary16 (_Float16)
      f32, // IEEE 754 binary32 (float)
      f64, // IEEE 754 binary64 (double)
      f128, // IEEE 754 binary128 (__float128)
      pointer, // Pointer is not builtin
      vector,
      aggregate,
      function);

BUSTER_GLOBAL_LOCAL constexpr let ir_debug_type_builtin_count = (BUSTER_UNDERLYING_TYPE(IrDebugTypeId))IrDebugTypeId::pointer;

STRUCT(IrModule)
{
    Arena* untyped_arena;
    Arena* function_arena;
    Arena* value_arena;
    Arena* global_variable_arena;
    Arena* block_arena;
    Arena* statement_arena;
    Arena* constant_arenas[(u64)IrConstantDataId::Count];
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
            IrTypeRef ir[ir_type_builtin_count];
            IrDebugTypeRef debug[ir_debug_type_builtin_count];
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

STRUCT(IrDebugScope)
{
};

STRUCT(IrBlock)
{
    u32 line;
    u32 column;
};

STRUCT(IrStatement)
{
    u32 line;
    u32 column;
    IrBlockRef block;
};

STRUCT(IrBuilder)
{
    u32 line;
    u32 column;
    IrBlockRef block;
};

STRUCT(IrFunctionCreate)
{
    IrTypeRef ir_type;
    IrDebugTypeRef debug_type;
    IrInternRef name;
    IrLinkage linkage;
    u8 reserved[3];
    IrInternRef* argument_names;
};

STRUCT(IrType)
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
    IrInternRef name;
    IrTypeRef kind_next;
    IrTypeId id;
    u8 reserved[7];
};

STRUCT(IrDebugType)
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
    IrInternRef name;
    IrDebugTypeRef kind_next;
    IrDebugTypeId id;
    u8 reserved[7];
};

BUSTER_F_DECL Slice<IrFunction> ir_module_get_functions(IrModule* module);
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

BUSTER_F_DECL IrInternRef ir_module_intern(IrModule* module, String8 string);
BUSTER_F_DECL Slice<IrInternRef> ir_module_allocate_name_array(IrModule* module, u64 count);
BUSTER_F_DECL Slice<IrDebugTypeRef> ir_module_allocate_debug_type_array(IrModule* module, u64 count);
BUSTER_F_DECL Slice<IrTypeRef> ir_module_allocate_type_array(IrModule* module, u64 count);
BUSTER_F_DECL IrValueRef ir_get_constant_integer(IrModule* module, IrTypeRef type_ref, u64 value);

BUSTER_F_DECL IrBlockRef ir_create_block(IrModule* module);
#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL bool ir_tests(UnitTestArguments* arguments);
#endif
