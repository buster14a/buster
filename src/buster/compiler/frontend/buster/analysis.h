#pragma once

#include <buster/arena.h>
#include <buster/compiler/frontend/buster/parser.h>

typedef u32 AnalysisModuleId;
typedef u32 AnalysisSourceId;
typedef u32 AnalysisTypeId;

#define ANALYSIS_INVALID_ID UINT32_MAX

typedef struct AnalysisEntityId AnalysisEntityId;
struct AnalysisEntityId
{
    AnalysisModuleId module;
    u32 index;
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
    AnalysisModuleId id;
    u32 source_count;
    u32 entity_count;
    u32 type_count;
    u32 code_count;
};

typedef enum AnalysisDiagnosticKind
{
    ANALYSIS_DIAGNOSTIC_DUPLICATE_DECLARATION,
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
};

typedef struct AnalysisResult AnalysisResult;
struct AnalysisResult
{
    AnalysisModuleInterface module;
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

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult analysis_tests(UnitTestArguments* arguments);
#endif
