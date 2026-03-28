#pragma once

#include <buster/base.h>

#include <buster/compiler/frontend/buster/parser.h>

STRUCT(AnalysisResult)
{
};

BUSTER_F_DECL AnalysisResult analyze(ParserResult parser);
BUSTER_F_DECL void analysis_experiments();
