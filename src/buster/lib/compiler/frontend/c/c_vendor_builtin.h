#pragma once

// Private typed admission for a finite LLVM x86 resource-header closure.
// Include after c_internal.h has established BUSTER_C_EXTERN. Admission does
// not promise lowering, alter target feature tests, or waive argument checks.
typedef struct CVendorBuiltinType CVendorBuiltinType;
struct CVendorBuiltinType
{
    CTypeKind kind;
    u16 lanes;
    u8 pointer_depth;
    // Qualifiers apply to the scalar or vector object beneath the pointer.
    bool is_const;
    bool is_volatile;
};

typedef struct CVendorBuiltin CVendorBuiltin;
struct CVendorBuiltin
{
    CVendorBuiltinType types[8]; // Return type, then at most seven parameters.
    u8 parameter_count;
    u8 constant_arguments; // Bit n requires parameter n to be an integer constant.
};

BUSTER_C_EXTERN bool c_vendor_builtin_spelling(String8 name);
BUSTER_C_EXTERN bool c_vendor_builtin_lookup(Target target, String8 name, CVendorBuiltin* signature);

// Custom generic builtins have grammar and type rules, not a void(...) C
// prototype. These operation descriptors share the exact spelling and arity
// facts; the parser still performs their individual operand/type checks.
typedef enum CVendorGenericOperation
{
    C_VENDOR_GENERIC_NONE,
    C_VENDOR_GENERIC_BIT_CAST,
    C_VENDOR_GENERIC_CONVERT_VECTOR,
    C_VENDOR_GENERIC_ELEMENTWISE_ABS,
    C_VENDOR_GENERIC_ELEMENTWISE_ADD_SAT,
    C_VENDOR_GENERIC_ELEMENTWISE_MAX,
    C_VENDOR_GENERIC_ELEMENTWISE_MIN,
    C_VENDOR_GENERIC_ELEMENTWISE_POPCOUNT,
    C_VENDOR_GENERIC_ELEMENTWISE_SUB_SAT,
    C_VENDOR_GENERIC_NONDETERMINISTIC_VALUE,
    C_VENDOR_GENERIC_NONTEMPORAL_LOAD,
    C_VENDOR_GENERIC_NONTEMPORAL_STORE,
    C_VENDOR_GENERIC_REDUCE_ADD,
    C_VENDOR_GENERIC_REDUCE_AND,
    C_VENDOR_GENERIC_REDUCE_MAX,
    C_VENDOR_GENERIC_REDUCE_MIN,
    C_VENDOR_GENERIC_REDUCE_MUL,
    C_VENDOR_GENERIC_REDUCE_OR,
    C_VENDOR_GENERIC_SHUFFLE_VECTOR,
} CVendorGenericOperation;

typedef enum CVendorGenericCategory
{
    C_VENDOR_GENERIC_CATEGORY_REPRESENTATION,
    C_VENDOR_GENERIC_CATEGORY_INTEGER,
    C_VENDOR_GENERIC_CATEGORY_SIGNED_INTEGER_OR_FLOAT,
    C_VENDOR_GENERIC_CATEGORY_INTEGER_OR_FLOAT,
    C_VENDOR_GENERIC_CATEGORY_INTEGER_FLOAT_OR_POINTER,
} CVendorGenericCategory;

typedef enum CVendorGenericResult
{
    C_VENDOR_GENERIC_RESULT_FIRST_OPERAND,
    C_VENDOR_GENERIC_RESULT_FIRST_TYPE,
    C_VENDOR_GENERIC_RESULT_SECOND_TYPE,
    C_VENDOR_GENERIC_RESULT_POINTEE,
    C_VENDOR_GENERIC_RESULT_VOID,
    C_VENDOR_GENERIC_RESULT_VECTOR_ELEMENT,
    C_VENDOR_GENERIC_RESULT_SHUFFLE,
} CVendorGenericResult;

typedef struct CVendorGenericBuiltin CVendorGenericBuiltin;
struct CVendorGenericBuiltin
{
    CVendorGenericOperation operation;
    CVendorGenericCategory category;
    CVendorGenericResult result;
    u8 minimum_arguments;
    // UINT8_MAX denotes variable arity, not a limit of 255 selectors.
    u8 maximum_arguments;
    u8 type_arguments; // Bit n denotes a type-name argument at position n.
    bool requires_vector;
    bool same_type_operands;
};

BUSTER_C_EXTERN CVendorGenericBuiltin c_vendor_generic_builtin(String8 name);
