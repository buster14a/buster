#pragma once

#include <buster/base.h>

#include <buster/compiler/frontend/buster/parser.h>
#include <buster/compiler/ir/ir.h>

typedef struct AnalysisResult AnalysisResult;
struct AnalysisResult
{
    u8 foo;
};

// BUSTER_F_IMPL AnalysisResult analyze(ParserResult* restrict parsers, u64 parser_count);
BUSTER_F_DECL void analysis_experiments(void);
