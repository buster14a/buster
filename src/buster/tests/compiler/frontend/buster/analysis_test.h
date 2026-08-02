#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/string.h>

BUSTER_TEST_F_DECL AnalysisEntity* analysis_value_entity_find(AnalysisResult* result, String8 name);
BUSTER_TEST_F_DECL UnitTestResult analysis_tests(UnitTestArguments* arguments);
