#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL AnalysisInstantiation* ir_instantiation_from_id(AnalysisResult* analysis, AnalysisInstantiationId id);
BUSTER_F_DECL bool ir_entity_has_diagnostic(AnalysisResult* analysis, AnalysisEntityId entity);
BUSTER_F_DECL UnitTestResult ir_tests(UnitTestArguments* arguments);
#endif
