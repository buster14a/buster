#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL AnalysisEntity* analysis_value_entity_find(AnalysisResult* result, String8 name);
BUSTER_F_DECL UnitTestResult analysis_tests(UnitTestArguments* arguments);
#endif
