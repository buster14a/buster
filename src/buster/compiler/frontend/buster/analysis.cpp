#pragma once

#include <buster/compiler/frontend/buster/analysis.h>
#include <buster/file.h>
#include <buster/arena.h>

BUSTER_F_IMPL AnalysisResult analyze(ParserResult parser)
{
    AnalysisResult result = {};
    BUSTER_UNUSED(parser);
    bool a = true;
    if (a) BUSTER_TRAP();
    return result;
}

BUSTER_F_IMPL void analysis_experiments()
{
    Arena* arena = arena_create({});
    let source = BYTE_SLICE_TO_STRING(8, file_read(arena, SOs("tests/basic.bbb"), {}));
    analyze(parse(source.pointer, tokenize(arena, source.pointer, source.length)));
}
