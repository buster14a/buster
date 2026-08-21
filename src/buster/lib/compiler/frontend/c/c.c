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
    }
    else
    {
        result = c_lower_to_ir(arena, source_path, preprocess, analysis, target);
    }

    return result;
}

String8 c_token_kind_name(CTokenKind kind)
{
    String8 result;
    switch (kind)
    {
    case C_TOKEN_INVALID:
        result = S8("invalid");
        break;
    case C_TOKEN_END_OF_FILE:
        result = S8("end of file");
        break;
    case C_TOKEN_IDENTIFIER:
        result = S8("identifier");
        break;
    case C_TOKEN_PREPROCESSING_NUMBER:
        result = S8("preprocessing number");
        break;
    case C_TOKEN_CHARACTER_LITERAL:
        result = S8("character literal");
        break;
    case C_TOKEN_STRING_LITERAL:
        result = S8("string literal");
        break;
    case C_TOKEN_PUNCTUATOR:
        result = S8("punctuator");
        break;
    case C_TOKEN_NEWLINE:
        result = S8("newline");
        break;
    case C_TOKEN_PRAGMA:
        result = S8("pragma");
        break;
    case C_TOKEN_KIND_COUNT:
    default:
        result = S8("invalid token kind");
        break;
    }

    return result;
}
