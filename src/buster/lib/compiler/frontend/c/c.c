#include "c_internal.h"

// Keep the frontend historical declaration order and one-translation-unit
// linkage while making the large implementation mechanically navigable. The
// same .c files are included here in historical order for unity builds and are
// compiled as independent sources in non-unity builds. Each implementation
// owns its physical source path and line numbers in both modes so debuggers,
// profilers, and diagnostics point at the split file that contains the code.
#include "c_source.c"
#include "c_parse.c"
#include "c_gen.c"
CIRLowerResult c_analyze(Arena* arena, String8 source_path, CPreprocessResult preprocess, CParserResult syntax, Target target)
{
    CIRLowerResult result = {0};
    CAnalysisResult analysis = c_analyze_semantics(arena, preprocess, syntax);
    if (analysis.diagnostic_count)
    {
        result.diagnostics = analysis.diagnostics;
        result.diagnostic_count = analysis.diagnostic_count;
        return result;
    }
    return c_lower_to_ir(arena, source_path, preprocess, analysis, target);
}

String8 c_token_kind_name(CTokenKind kind)
{
    switch (kind)
    {
    case C_TOKEN_INVALID:
    {
        return S8("invalid");
    }
    case C_TOKEN_END_OF_FILE:
    {
        return S8("end of file");
    }
    case C_TOKEN_IDENTIFIER:
    {
        return S8("identifier");
    }
    case C_TOKEN_PREPROCESSING_NUMBER:
    {
        return S8("preprocessing number");
    }
    case C_TOKEN_CHARACTER_LITERAL:
    {
        return S8("character literal");
    }
    case C_TOKEN_STRING_LITERAL:
    {
        return S8("string literal");
    }
    case C_TOKEN_PUNCTUATOR:
    {
        return S8("punctuator");
    }
    case C_TOKEN_NEWLINE:
    {
        return S8("newline");
    }
    case C_TOKEN_PRAGMA:
    {
        return S8("pragma");
    }
    case C_TOKEN_KIND_COUNT:
    {
        return S8("invalid token kind");
    }
    }
    return S8("invalid token kind");
}
