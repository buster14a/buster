#include <buster/lib/compiler/frontend/c/c.h>

#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/simd.h>
#include <buster/lib/string.h>

// The lexer's compaction emitter wants exactly the feature set BUSTER_SIMD_512
// already decides -- AVX-512 plus VBMI for vpermi2b and VBMI2 for vpcompressb
// -- so it reuses that one condition rather than restating it and letting the
// two drift.
//
// Unlike the buster tokenizer, this emitter is still written in raw
// intrinsics rather than the target-fixed <buster/lib/simd.h> vocabulary, so
// it additionally excludes the self-hosted stages, which have no vendor
// headers. Moving it over is worth doing and is not mechanical: a 16-byte
// CToken row carries an offset, so building rows needs a 32-bit lane add and
// a masked byte broadcast that the vocabulary does not expose yet (a 4-byte
// Token, which is all the buster tokenizer stores, needs neither). Until then
// the scalar loop remains the byte-identical fallback here for the
// self-hosted stages, MSVC, AArch64 and pre-AVX-512 x86.
#if BUSTER_SIMD_512 && !defined(__BUSTER__)
#define BUSTER_C_LEX_COMPACT 1
#else
#define BUSTER_C_LEX_COMPACT 0
#endif

// Source translation needs only AVX-512F and AVX-512BW, so keep its less
// restrictive guard separate from the compact lexer's VBMI/VBMI2 requirement.
// MSVC, non-x86 hosts, and self-hosted stages retain the SWAR path without
// needing vendor headers.
#if BUSTER_CPU_ARCH_X86_64 && defined(__AVX512F__) && defined(__AVX512BW__) && !defined(__BUSTER__) && !BUSTER_COMPILER_MSVC
#define BUSTER_C_TRANSLATE_AVX512 1
#else
#define BUSTER_C_TRANSLATE_AVX512 0
#endif

#if BUSTER_C_LEX_COMPACT || BUSTER_C_TRANSLATE_AVX512
#include <immintrin.h>
#endif


// Keep the frontend historical declaration order and one-translation-unit
// linkage while making the large implementation mechanically navigable. These
// .c files are fragments, not CMake sources: each starts with a #line back to
// this file so __FILE__/__LINE__ and diagnostics retain the c.c contract.
#include "c_source.c"
#include "c_parse.c"
#include "c_sema.c"
#include "c_gen.c"
#line 48383 "src/buster/lib/compiler/frontend/c/c.c"
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
