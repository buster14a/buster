#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/file.h>
#include <buster/lib/target.h>

typedef u32 AnalysisIdUnderlying;

typedef struct AnalysisModuleId AnalysisModuleId;
struct AnalysisModuleId
{
    AnalysisIdUnderlying value;
};

typedef struct AnalysisSourceId AnalysisSourceId;
struct AnalysisSourceId
{
    AnalysisIdUnderlying value;
};

typedef struct AnalysisTypeId AnalysisTypeId;
struct AnalysisTypeId
{
    AnalysisIdUnderlying value;
};

typedef struct AnalysisEntityIndex AnalysisEntityIndex;
struct AnalysisEntityIndex
{
    AnalysisIdUnderlying value;
};

typedef struct AnalysisLocalId AnalysisLocalId;
struct AnalysisLocalId
{
    AnalysisIdUnderlying value;
};

typedef struct AnalysisJobId AnalysisJobId;
struct AnalysisJobId
{
    AnalysisIdUnderlying value;
};

typedef struct AnalysisInstantiationId AnalysisInstantiationId;
struct AnalysisInstantiationId
{
    AnalysisIdUnderlying value;
};

#define ANALYSIS_ID_UNDERLYING_INVALID UINT32_MAX
#define ANALYSIS_MODULE_ID_INVALID ((AnalysisModuleId){.value = ANALYSIS_ID_UNDERLYING_INVALID})
#define ANALYSIS_SOURCE_ID_INVALID ((AnalysisSourceId){.value = ANALYSIS_ID_UNDERLYING_INVALID})
#define ANALYSIS_TYPE_ID_INVALID ((AnalysisTypeId){.value = ANALYSIS_ID_UNDERLYING_INVALID})
#define ANALYSIS_ENTITY_INDEX_INVALID ((AnalysisEntityIndex){.value = ANALYSIS_ID_UNDERLYING_INVALID})
#define ANALYSIS_LOCAL_ID_INVALID ((AnalysisLocalId){.value = ANALYSIS_ID_UNDERLYING_INVALID})
#define ANALYSIS_JOB_ID_INVALID ((AnalysisJobId){.value = ANALYSIS_ID_UNDERLYING_INVALID})
#define ANALYSIS_INSTANTIATION_ID_INVALID ((AnalysisInstantiationId){.value = ANALYSIS_ID_UNDERLYING_INVALID})
#define ANALYSIS_ENTITY_ID_INVALID                                                                                                                             \
    ((AnalysisEntityId){                                                                                                                                       \
        .module = ANALYSIS_MODULE_ID_INVALID,                                                                                                                  \
        .index = ANALYSIS_ENTITY_INDEX_INVALID,                                                                                                                \
    })

BUSTER_CT_CHECK(sizeof(AnalysisModuleId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisSourceId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisTypeId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisEntityIndex) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisLocalId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisJobId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisInstantiationId) == sizeof(AnalysisIdUnderlying));

typedef struct AnalysisEntityId AnalysisEntityId;
struct AnalysisEntityId
{
    AnalysisModuleId module;
    AnalysisEntityIndex index;
};

typedef enum AnalysisEntityKind
{
    ANALYSIS_ENTITY_TYPE,
    ANALYSIS_ENTITY_CODE,
    ANALYSIS_ENTITY_DATA,
    ANALYSIS_ENTITY_COUNT,
} AnalysisEntityKind;

typedef enum AnalysisNamespace
{
    ANALYSIS_NAMESPACE_TYPE,
    ANALYSIS_NAMESPACE_VALUE,
    ANALYSIS_NAMESPACE_COUNT,
} AnalysisNamespace;

typedef enum AnalysisResolutionState
{
    ANALYSIS_RESOLUTION_UNRESOLVED,
    ANALYSIS_RESOLUTION_RESOLVING,
    ANALYSIS_RESOLUTION_RESOLVED,
    ANALYSIS_RESOLUTION_ERROR,
    ANALYSIS_RESOLUTION_COUNT,
} AnalysisResolutionState;

typedef enum AnalysisTypeKind
{
    // Internal recovery sentinel. This is not a user-visible language type.
    ANALYSIS_TYPE_POISON,
    ANALYSIS_TYPE_VOID,
    ANALYSIS_TYPE_BOOL,
    ANALYSIS_TYPE_INTEGER,
    ANALYSIS_TYPE_FLOAT,
    ANALYSIS_TYPE_VA_LIST,
    ANALYSIS_TYPE_COMPILE_TIME_PARAMETER,
    ANALYSIS_TYPE_POINTER,
    ANALYSIS_TYPE_SLICE,
    ANALYSIS_TYPE_INFERRED_ARRAY,
    ANALYSIS_TYPE_ARRAY,
    ANALYSIS_TYPE_VECTOR,
    ANALYSIS_TYPE_FUNCTION,
    // Internal iterable type produced by the range operator.
    ANALYSIS_TYPE_RANGE,
    ANALYSIS_TYPE_STRUCT,
    ANALYSIS_TYPE_UNION,
    ANALYSIS_TYPE_ENUM,
    ANALYSIS_TYPE_COUNT,
} AnalysisTypeKind;

typedef struct AnalysisType AnalysisType;
typedef enum AnalysisAbiClass
{
    ANALYSIS_ABI_CLASS_NONE,
    ANALYSIS_ABI_CLASS_INTEGER,
    ANALYSIS_ABI_CLASS_FLOAT,
    ANALYSIS_ABI_CLASS_VECTOR,
    ANALYSIS_ABI_CLASS_POINTER,
    ANALYSIS_ABI_CLASS_AGGREGATE,
    ANALYSIS_ABI_CLASS_MEMORY,
    ANALYSIS_ABI_CLASS_COUNT,
} AnalysisAbiClass;

typedef enum AnalysisLayoutState
{
    ANALYSIS_LAYOUT_UNRESOLVED,
    ANALYSIS_LAYOUT_RESOLVING,
    ANALYSIS_LAYOUT_RESOLVED,
    ANALYSIS_LAYOUT_ERROR,
    ANALYSIS_LAYOUT_COUNT,
} AnalysisLayoutState;

typedef struct AnalysisTypeLayout AnalysisTypeLayout;
struct AnalysisTypeLayout
{
    u64 size;
    u32 alignment;
    AnalysisAbiClass abi_class;
    AnalysisLayoutState state;
};

struct AnalysisType
{
    String8 name;
    AnalysisTypeId id;
    AnalysisTypeKind kind;
    AnalysisTypeLayout layout;
    union
    {
        struct
        {
            u32 bit_width;
            bool is_signed;
            u8 reserved[3];
        } integer;
        u32 float_bit_width;
        AnalysisTypeId element_type;
        struct
        {
            AnalysisTypeId element_type;
            u64 count;
        } array;
        struct
        {
            AnalysisTypeId element_type;
            u64 count;
        } vector;
        struct
        {
            AnalysisTypeId* argument_types;
            AnalysisTypeId return_type;
            AstCallingConvention calling_convention;
            u32 argument_count;
            bool is_variadic;
            u8 reserved[3];
        } function;
        AnalysisEntityId declaration;
    } as;
};

typedef struct AnalysisBuiltinTypes AnalysisBuiltinTypes;
struct AnalysisBuiltinTypes
{
    AnalysisTypeId poison;
    AnalysisTypeId void_type;
    AnalysisTypeId bool_type;
    AnalysisTypeId u8_type;
    AnalysisTypeId u16_type;
    AnalysisTypeId u32_type;
    AnalysisTypeId u64_type;
    AnalysisTypeId s8_type;
    AnalysisTypeId s16_type;
    AnalysisTypeId s32_type;
    AnalysisTypeId s64_type;
    AnalysisTypeId f32_type;
    AnalysisTypeId f64_type;
    AnalysisTypeId va_list_type;
};

typedef struct AnalysisTypeTable AnalysisTypeTable;
struct AnalysisTypeTable
{
    AnalysisType* types;
    AnalysisBuiltinTypes builtin;
    u32 count;
    u32 capacity;
};

typedef struct AnalysisField AnalysisField;
struct AnalysisField
{
    String8 name;
    ParserSourceRange range;
    AnalysisTypeId type;
    u64 offset;
};

typedef struct AnalysisEnumMember AnalysisEnumMember;
struct AnalysisEnumMember
{
    String8 name;
    ParserSourceRange range;
    u64 value;
};

typedef struct AnalysisEntitySemantic AnalysisEntitySemantic;
typedef struct AnalysisConstant AnalysisConstant;
struct AnalysisEntitySemantic
{
    AnalysisField* fields;
    AnalysisEnumMember* enum_members;
    AnalysisTypeId type;
    AnalysisConstant* constant;
    AnalysisResolutionState state;
    u32 field_count;
    u32 enum_member_count;
};

typedef enum AnalysisValueCategory
{
    ANALYSIS_VALUE_CATEGORY_VALUE,
    ANALYSIS_VALUE_CATEGORY_TEMPORARY,
    ANALYSIS_VALUE_CATEGORY_IMMUTABLE_PLACE,
    ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE,
    ANALYSIS_VALUE_CATEGORY_COUNT,
} AnalysisValueCategory;

typedef enum AnalysisConversionKind
{
    ANALYSIS_CONVERSION_NONE,
    ANALYSIS_CONVERSION_LITERAL,
    ANALYSIS_CONVERSION_INTEGER_WIDEN,
    ANALYSIS_CONVERSION_INTEGER_NARROW,
    ANALYSIS_CONVERSION_FLOAT_WIDEN,
    ANALYSIS_CONVERSION_FLOAT_NARROW,
    ANALYSIS_CONVERSION_POINTER,
    ANALYSIS_CONVERSION_EXPLICIT,
    ANALYSIS_CONVERSION_UNDEFINED,
    ANALYSIS_CONVERSION_COUNT,
} AnalysisConversionKind;

typedef enum AnalysisLocalKind
{
    ANALYSIS_LOCAL_ARGUMENT,
    ANALYSIS_LOCAL_DATA,
    ANALYSIS_LOCAL_FOR,
    ANALYSIS_LOCAL_COUNT,
} AnalysisLocalKind;

typedef struct AnalysisTypedNode AnalysisTypedNode;
typedef enum AnalysisConstantKind
{
    ANALYSIS_CONSTANT_NONE,
    ANALYSIS_CONSTANT_INTEGER,
    ANALYSIS_CONSTANT_BOOLEAN,
    ANALYSIS_CONSTANT_ENUM,
    ANALYSIS_CONSTANT_FLOAT,
    ANALYSIS_CONSTANT_ARRAY,
    ANALYSIS_CONSTANT_AGGREGATE,
    ANALYSIS_CONSTANT_COUNT,
} AnalysisConstantKind;

struct AnalysisConstant
{
    union
    {
        u64 integer;
        f64 floating;
        struct
        {
            AnalysisConstant* elements;
            u32 element_count;
        } aggregate;
    };
    AnalysisConstantKind kind;
    bool is_negative;
    u8 reserved[3];
};

typedef struct AnalysisLocal AnalysisLocal;
struct AnalysisLocal
{
    String8 name;
    ParserSourceRange range;
    AnalysisTypeId type;
    AnalysisLocalId id;
    AnalysisConstant constant;
    AnalysisLocalKind kind;
    u32 scope_depth;
    bool is_mutable;
    bool is_initialized;
    bool address_taken;
    bool requires_storage;
    bool is_compile_time;
    u8 reserved[3];
};

struct AnalysisTypedNode
{
    AnalysisTypeId type;
    AnalysisValueCategory category;
    AnalysisLocalId local;
    AnalysisEntityId entity;
    AnalysisModuleId namespace_module;
    AnalysisInstantiationId instantiation;
    AnalysisConstant constant;
    // Inclusive node index where this node's flattened postorder subtree starts.
    u32 subtree_start;
    AnalysisConversionKind conversion;
    bool is_addressable;
    bool is_namespace;
    u8 reserved[2];
};

typedef struct AnalysisTypedExpression AnalysisTypedExpression;
struct AnalysisTypedExpression
{
    AnalysisTypedExpression* next;
    AstExpression ast;
    AnalysisTypedNode* nodes;
    AnalysisTypeId expected_type;
    AnalysisTypeId type;
};

typedef struct AnalysisBody AnalysisBody;
struct AnalysisBody
{
    AnalysisLocal* locals;
    AnalysisTypedExpression* first_expression;
    AnalysisTypedExpression* last_expression;
    u32 local_count;
    u32 local_capacity;
    u32 expression_count;
    AnalysisEntityId* dependencies;
    u32 dependency_count;
    u32 dependency_capacity;
    bool analyzed;
    bool can_fall_through;
    bool has_unreachable;
    u8 reserved;
};

typedef struct AnalysisGenericTypeBinding AnalysisGenericTypeBinding;
struct AnalysisGenericTypeBinding
{
    String8 name;
    AnalysisTypeId type;
};

typedef struct AnalysisCompileTimeArgument AnalysisCompileTimeArgument;
struct AnalysisCompileTimeArgument
{
    AnalysisConstant constant;
    AnalysisTypeId type;
    u32 source_argument_index;
};

typedef struct AnalysisInstantiationRequester AnalysisInstantiationRequester;
struct AnalysisInstantiationRequester
{
    AnalysisInstantiationRequester* next;
    AnalysisModuleId module;
};

typedef struct AnalysisInstantiation AnalysisInstantiation;
struct AnalysisInstantiation
{
    AnalysisInstantiation* next;
    AnalysisInstantiationRequester* first_requester;
    AnalysisInstantiationRequester* last_requester;
    AnalysisGenericTypeBinding* type_bindings;
    AnalysisCompileTimeArgument* compile_time_arguments;
    String8 canonical_key;
    String8 symbol_name;
    AnalysisBody body;
    AnalysisEntityId generic_entity;
    AnalysisModuleId codegen_owner;
    AnalysisTypeId function_type;
    AnalysisInstantiationId id;
    u64 canonical_hash;
    u32 type_binding_count;
    u32 compile_time_argument_count;
    u32 requester_count;
    bool analyzed;
    u8 reserved[3];
};

// Parser results and their source storage are borrowed. They must outlive the
// analysis result. Paths must uniquely identify sources in a module. Paths and
// module names are copied into the result arena.
typedef struct AnalysisSourceInput AnalysisSourceInput;
struct AnalysisSourceInput
{
    String8 path;
    ParserResult* parser;
};

typedef struct AnalysisSource AnalysisSource;
struct AnalysisSource
{
    String8 path;
    ParserResult* parser;
    AnalysisSourceId id;
    u32 original_input_index;
};

typedef struct AnalysisResult AnalysisResult;
typedef enum AnalysisImportResolutionState
{
    ANALYSIS_IMPORT_UNRESOLVED,
    ANALYSIS_IMPORT_RESOLVED,
    ANALYSIS_IMPORT_MISSING,
    ANALYSIS_IMPORT_AMBIGUOUS,
    ANALYSIS_IMPORT_CYCLE,
    ANALYSIS_IMPORT_COUNT,
} AnalysisImportResolutionState;

typedef struct AnalysisImport AnalysisImport;
struct AnalysisImport
{
    String8 name_space;
    String8 path;
    ParserSourceRange range;
    ParserSourceRange path_range;
    AnalysisSourceId source;
    AnalysisModuleId target_id;
    AnalysisResult* target;
    AnalysisImportResolutionState state;
};

typedef struct AnalysisEntity AnalysisEntity;
struct AnalysisEntity
{
    String8 name;
    ParserSourceRange range;
    AnalysisEntityId id;
    AnalysisSourceId source;
    AnalysisEntityKind kind;
    AnalysisNamespace name_space;
    union
    {
        AstTypeDeclaration* type_declaration;
        AstCode* code;
        AstDataDeclaration* data;
    } ast;
};

typedef struct AnalysisModuleInterface AnalysisModuleInterface;
struct AnalysisModuleInterface
{
    String8 name;
    AnalysisSource* sources;
    AnalysisImport* imports;
    AnalysisEntity* entities;
    AnalysisEntitySemantic* semantics;
    AnalysisBody* bodies;
    AnalysisModuleId id;
    u32 source_count;
    u32 import_count;
    u32 entity_count;
    u32 type_count;
    u32 code_count;
    u32 data_count;
};

typedef enum AnalysisDiagnosticKind
{
    ANALYSIS_DIAGNOSTIC_DUPLICATE_DECLARATION,
    ANALYSIS_DIAGNOSTIC_DUPLICATE_IMPORT_NAMESPACE,
    ANALYSIS_DIAGNOSTIC_MISSING_IMPORTED_MODULE,
    ANALYSIS_DIAGNOSTIC_AMBIGUOUS_IMPORTED_MODULE,
    ANALYSIS_DIAGNOSTIC_IMPORT_CYCLE,
    ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE,
    ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE,
    ANALYSIS_DIAGNOSTIC_INVALID_VECTOR_TYPE,
    ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER,
    ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION,
    ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL,
    ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH,
    ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE,
    ANALYSIS_DIAGNOSTIC_NOT_CALLABLE,
    ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT,
    ANALYSIS_DIAGNOSTIC_COMPILE_TIME_ARGUMENT_REQUIRED,
    ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER,
    ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
    ANALYSIS_DIAGNOSTIC_EXPECTED_CONTEXTUAL_TYPE,
    ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW,
    ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD,
    ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER,
    ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD,
    ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD,
    ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT,
    ANALYSIS_DIAGNOSTIC_MISSING_RETURN,
    ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT,
    ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE,
    ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH,
    ANALYSIS_DIAGNOSTIC_EXPECTED_FUNCTION_TYPE,
    ANALYSIS_DIAGNOSTIC_COUNT,
} AnalysisDiagnosticKind;

typedef struct AnalysisDiagnostic AnalysisDiagnostic;
typedef struct AnalysisDiagnosticNote AnalysisDiagnosticNote;
struct AnalysisDiagnosticNote
{
    AnalysisDiagnosticNote* next;
    String8 message;
    ParserSourceRange range;
    AnalysisEntityId entity;
    AnalysisSourceId source;
};

struct AnalysisDiagnostic
{
    AnalysisDiagnostic* next;
    AnalysisDiagnosticNote* first_note;
    AnalysisDiagnosticNote* last_note;
    String8 message;
    String8 expected_type_name;
    String8 actual_type_name;
    String8 explanation;
    ParserSourceRange range;
    AnalysisEntityId entity;
    AnalysisEntityId previous_entity;
    AnalysisSourceId source;
    AnalysisDiagnosticKind kind;
    String8 subject;
    AnalysisTypeId expected_type;
    AnalysisTypeId actual_type;
    u32 argument_index;
    bool has_argument_index;
    u8 reserved[3];
};

typedef enum AnalysisJobKind
{
    ANALYSIS_JOB_INTERFACE,
    ANALYSIS_JOB_BODY,
    ANALYSIS_JOB_LAYOUT,
    ANALYSIS_JOB_COUNT,
} AnalysisJobKind;

typedef enum AnalysisDependencyKind
{
    ANALYSIS_DEPENDENCY_INTERFACE,
    ANALYSIS_DEPENDENCY_CONSTANT,
    ANALYSIS_DEPENDENCY_LAYOUT,
    ANALYSIS_DEPENDENCY_BODY,
    ANALYSIS_DEPENDENCY_COUNT,
} AnalysisDependencyKind;

typedef struct AnalysisJob AnalysisJob;
struct AnalysisJob
{
    AnalysisJobId* dependencies;
    AnalysisDependencyKind* dependency_kinds;
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    AnalysisJobId id;
    AnalysisJobKind kind;
    u32 dependency_count;
};

typedef struct AnalysisScheduleResult AnalysisScheduleResult;
struct AnalysisScheduleResult
{
    AnalysisJobId* execution_order;
    u32 execution_count;
    u32 wave_count;
    bool has_cycle;
    u8 reserved[3];
};

typedef struct AnalysisInterfaceSummary AnalysisInterfaceSummary;
#define ANALYSIS_INTERFACE_SCHEMA_VERSION 1u
struct AnalysisInterfaceSummary
{
    String8 bytes;
    u64 hash;
    u32 schema_version;
    u32 reserved;
};

typedef struct AnalysisInterfaceCacheEntry AnalysisInterfaceCacheEntry;
struct AnalysisInterfaceCacheEntry
{
    AnalysisInterfaceCacheEntry* next;
    String8 module_name;
    AnalysisInterfaceSummary summary;
};

typedef struct AnalysisInterfaceCache AnalysisInterfaceCache;
struct AnalysisInterfaceCache
{
    AnalysisInterfaceCacheEntry* first;
    AnalysisInterfaceCacheEntry* last;
    u32 count;
};

typedef struct AnalysisProgramModule AnalysisProgramModule;
struct AnalysisProgramModule
{
    String8 name;
    String8 path;
    String8 source;
    FileMapRead source_map;
    ParserResult parser;
    AnalysisResult* analysis;
};

typedef struct AnalysisProgram AnalysisProgram;
struct AnalysisProgram
{
    AnalysisProgramModule* modules;
    AnalysisResult** module_results;
    AnalysisProgramModule* root;
    u32 module_count;
    u32 parser_diagnostic_count;
    u32 analysis_diagnostic_count;
    bool load_failed;
    u8 reserved[3];
};

typedef struct AnalysisProgramJob AnalysisProgramJob;
struct AnalysisProgramJob
{
    AnalysisModuleId module;
    AnalysisJobId job;
};

typedef struct AnalysisProgramScheduleResult AnalysisProgramScheduleResult;
struct AnalysisProgramScheduleResult
{
    AnalysisProgramJob* execution_order;
    u32 execution_count;
    u32 wave_count;
    bool has_cycle;
    u8 reserved[3];
};

typedef struct AnalysisProgramOptions AnalysisProgramOptions;
struct AnalysisProgramOptions
{
    String8 root_path;
    String8 root_module_name;
    String8 module_root;
    TargetDataLayout data_layout;
    u32 pointer_size;
    u32 pointer_alignment;
};

typedef struct AnalysisLayoutOptions AnalysisLayoutOptions;
struct AnalysisLayoutOptions
{
    TargetDataLayout data_layout;
    u32 pointer_size;
    u32 pointer_alignment;
};

typedef enum AnalysisAbiConvention
{
    ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64,
    ANALYSIS_ABI_CONVENTION_WIN64_X86_64,
    ANALYSIS_ABI_CONVENTION_AAPCS64,
    ANALYSIS_ABI_CONVENTION_APPLE_AARCH64,
    ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64,
    ANALYSIS_ABI_CONVENTION_COUNT,
} AnalysisAbiConvention;

typedef enum AnalysisAbiLocationKind
{
    ANALYSIS_ABI_LOCATION_NONE,
    ANALYSIS_ABI_LOCATION_REGISTER,
    ANALYSIS_ABI_LOCATION_STACK,
    ANALYSIS_ABI_LOCATION_INDIRECT,
    ANALYSIS_ABI_LOCATION_COUNT,
} AnalysisAbiLocationKind;

typedef struct AnalysisAbiPart AnalysisAbiPart;
struct AnalysisAbiPart
{
    AnalysisAbiClass abi_class;
    AnalysisAbiLocationKind location;
    u32 register_index;
    u32 stack_offset;
    u32 value_offset;
    u32 size;
};

enum
{
    ANALYSIS_ABI_MAX_PARTS = 4,
};

typedef struct AnalysisAbiValue AnalysisAbiValue;
struct AnalysisAbiValue
{
    AnalysisAbiPart parts[ANALYSIS_ABI_MAX_PARTS];
    u32 part_count;
    u32 indirect_copy_offset;
    bool indirect;
    u8 reserved[3];
};

typedef struct AnalysisFunctionAbi AnalysisFunctionAbi;
struct AnalysisFunctionAbi
{
    AnalysisAbiValue* arguments;
    AnalysisAbiValue result;
    AnalysisAbiConvention convention;
    u32 argument_count;
    u32 stack_size;
    u32 indirect_result_register;
    u32 fixed_argument_count;
    bool is_variadic;
    u8 reserved[3];
};

struct AnalysisResult
{
    AnalysisModuleInterface module;
    AnalysisTypeTable types;
    TargetDataLayout data_layout;
    AnalysisDiagnostic* first_diagnostic;
    AnalysisDiagnostic* last_diagnostic;
    AnalysisJob* jobs;
    AnalysisResult** program_modules;
    AnalysisInstantiation* first_instantiation;
    AnalysisInstantiation* last_instantiation;
    u32 diagnostic_count;
    u32 job_count;
    u32 program_module_count;
    u32 instantiation_count;
};

// Builds the immutable declaration interface which later type-resolution and
// body-analysis jobs will consume. Source IDs and entity IDs are deterministic:
// sources are ordered by path and entities by source, offset, and kind.
BUSTER_F_DECL AnalysisResult analysis_index_module(Arena* result_arena, AnalysisModuleId module_id, String8 module_name, AnalysisSourceInput* inputs,
                                                   u32 input_count);
BUSTER_F_DECL void analysis_resolve_imports(Arena* result_arena, AnalysisResult** modules, u32 module_count);
BUSTER_F_DECL void analysis_resolve_program_interfaces(Arena* result_arena, AnalysisResult** modules, u32 module_count);
BUSTER_F_DECL AnalysisEntity* analysis_find_qualified_entity(AnalysisResult* module, String8 import_name_space, String8 entity_name,
                                                             AnalysisNamespace name_space);
BUSTER_F_DECL String8 analysis_serialize_module_interface(Arena* arena, AnalysisResult* result);
BUSTER_F_DECL AnalysisInterfaceSummary analysis_module_interface_summary(Arena* arena, AnalysisResult* result);
BUSTER_F_DECL bool analysis_interface_summary_is_valid(AnalysisInterfaceSummary summary);
BUSTER_F_DECL AnalysisInterfaceCacheEntry* analysis_interface_cache_find(AnalysisInterfaceCache* cache, String8 module_name);
BUSTER_F_DECL bool analysis_interface_cache_store(Arena* arena, AnalysisInterfaceCache* cache, String8 module_name, AnalysisInterfaceSummary summary);
BUSTER_F_DECL String8 analysis_source_path(AnalysisResult* result, AnalysisModuleId module, AnalysisSourceId source);
BUSTER_F_DECL String8 analysis_format_diagnostic(Arena* arena, AnalysisResult* result, AnalysisDiagnostic* diagnostic);

// Resolves all top-level declared types, aggregate fields, and code signatures.
// This stage is deliberately separate from indexing so module indexes can be
// published before dependency-aware resolution jobs are scheduled.
BUSTER_F_DECL void analysis_resolve_module_interfaces(Arena* result_arena, AnalysisResult* result);
BUSTER_F_DECL AnalysisType* analysis_type_from_id(AnalysisResult* result, AnalysisTypeId id);
BUSTER_F_DECL bool analysis_entity_is_generic(Arena* scratch_arena, AnalysisResult* result, AnalysisEntity* entity);
// Resolves lexical bindings and annotates every node in each flattened body
// expression. Interfaces must have been resolved first.
BUSTER_F_DECL void analysis_analyze_bodies(Arena* result_arena, AnalysisResult* result);
BUSTER_F_DECL void analysis_compute_layouts(AnalysisResult* result, AnalysisLayoutOptions options);
BUSTER_F_DECL AnalysisAbiValue analysis_abi_value_classify(Arena* scratch_arena, AnalysisResult* result, AnalysisTypeId type, AnalysisAbiConvention convention,
                                                           bool is_result);
BUSTER_F_DECL AnalysisAbiValue analysis_abi_value_classify_variadic_argument(Arena* scratch_arena, AnalysisResult* result, AnalysisTypeId type,
                                                                             AnalysisAbiConvention convention);
BUSTER_F_DECL AnalysisFunctionAbi analysis_classify_function_abi(Arena* result_arena, AnalysisResult* result, AnalysisTypeId function_type, Target target);
BUSTER_F_DECL AnalysisFunctionAbi analysis_classify_call_abi(Arena* result_arena, AnalysisResult* result, AnalysisTypeId function_type,
                                                             AnalysisTypeId* argument_types, u32 argument_count, Target target);
BUSTER_F_DECL void analysis_build_jobs(Arena* result_arena, AnalysisResult* result);
BUSTER_F_DECL AnalysisScheduleResult analysis_schedule_jobs(Arena* result_arena, AnalysisResult* result);
BUSTER_F_DECL AnalysisProgram analysis_program_load(Arena* result_arena, Arena* expression_arena, AnalysisProgramOptions options);
BUSTER_F_DECL AnalysisProgram analysis_program_load_memory(Arena* result_arena, Arena* expression_arena, String8 source);
BUSTER_F_DECL void analysis_program_unmap_sources(AnalysisProgram* program);
BUSTER_F_DECL AnalysisProgramScheduleResult analysis_schedule_program_jobs(Arena* result_arena, AnalysisProgram* program);
