#pragma once

#include <buster/arena.h>
#include <buster/compiler/frontend/buster/parser.h>

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

#define ANALYSIS_ID_UNDERLYING_INVALID UINT32_MAX
#define ANALYSIS_MODULE_ID_INVALID ((AnalysisModuleId){ .value = ANALYSIS_ID_UNDERLYING_INVALID })
#define ANALYSIS_SOURCE_ID_INVALID ((AnalysisSourceId){ .value = ANALYSIS_ID_UNDERLYING_INVALID })
#define ANALYSIS_TYPE_ID_INVALID ((AnalysisTypeId){ .value = ANALYSIS_ID_UNDERLYING_INVALID })
#define ANALYSIS_ENTITY_INDEX_INVALID ((AnalysisEntityIndex){ .value = ANALYSIS_ID_UNDERLYING_INVALID })
#define ANALYSIS_ENTITY_ID_INVALID ((AnalysisEntityId){ \
    .module = ANALYSIS_MODULE_ID_INVALID, \
    .index = ANALYSIS_ENTITY_INDEX_INVALID, \
})

BUSTER_CT_CHECK(sizeof(AnalysisModuleId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisSourceId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisTypeId) == sizeof(AnalysisIdUnderlying));
BUSTER_CT_CHECK(sizeof(AnalysisEntityIndex) == sizeof(AnalysisIdUnderlying));

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
    ANALYSIS_TYPE_POINTER,
    ANALYSIS_TYPE_SLICE,
    ANALYSIS_TYPE_INFERRED_ARRAY,
    ANALYSIS_TYPE_ARRAY,
    ANALYSIS_TYPE_FUNCTION,
    ANALYSIS_TYPE_STRUCT,
    ANALYSIS_TYPE_UNION,
    ANALYSIS_TYPE_ENUM,
    ANALYSIS_TYPE_COUNT,
} AnalysisTypeKind;

typedef struct AnalysisType AnalysisType;
struct AnalysisType
{
    String8 name;
    AnalysisTypeId id;
    AnalysisTypeKind kind;
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
            AnalysisTypeId* argument_types;
            AnalysisTypeId return_type;
            AstCallingConvention calling_convention;
            u32 argument_count;
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
};

typedef struct AnalysisEntitySemantic AnalysisEntitySemantic;
struct AnalysisEntitySemantic
{
    AnalysisField* fields;
    AnalysisTypeId type;
    AnalysisResolutionState state;
    u32 field_count;
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
    } ast;
};

typedef struct AnalysisModuleInterface AnalysisModuleInterface;
struct AnalysisModuleInterface
{
    String8 name;
    AnalysisSource* sources;
    AnalysisEntity* entities;
    AnalysisEntitySemantic* semantics;
    AnalysisModuleId id;
    u32 source_count;
    u32 entity_count;
    u32 type_count;
    u32 code_count;
};

typedef enum AnalysisDiagnosticKind
{
    ANALYSIS_DIAGNOSTIC_DUPLICATE_DECLARATION,
    ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE,
    ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE,
    ANALYSIS_DIAGNOSTIC_COUNT,
} AnalysisDiagnosticKind;

typedef struct AnalysisDiagnostic AnalysisDiagnostic;
struct AnalysisDiagnostic
{
    AnalysisDiagnostic* next;
    String8 message;
    ParserSourceRange range;
    AnalysisEntityId entity;
    AnalysisEntityId previous_entity;
    AnalysisSourceId source;
    AnalysisDiagnosticKind kind;
    String8 subject;
};

typedef struct AnalysisResult AnalysisResult;
struct AnalysisResult
{
    AnalysisModuleInterface module;
    AnalysisTypeTable types;
    AnalysisDiagnostic* first_diagnostic;
    AnalysisDiagnostic* last_diagnostic;
    u32 diagnostic_count;
};

// Builds the immutable declaration interface which later type-resolution and
// body-analysis jobs will consume. Source IDs and entity IDs are deterministic:
// sources are ordered by path and entities by source, offset, and kind.
BUSTER_F_DECL AnalysisResult analysis_index_module(
    Arena* result_arena,
    AnalysisModuleId module_id,
    String8 module_name,
    AnalysisSourceInput* inputs,
    u32 input_count);

// Resolves all top-level declared types, aggregate fields, and code signatures.
// This stage is deliberately separate from indexing so module indexes can be
// published before dependency-aware resolution jobs are scheduled.
BUSTER_F_DECL void analysis_resolve_module_interfaces(Arena* result_arena, AnalysisResult* result);
BUSTER_F_DECL AnalysisType* analysis_type_from_id(AnalysisResult* result, AnalysisTypeId id);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult analysis_tests(UnitTestArguments* arguments);
#endif
