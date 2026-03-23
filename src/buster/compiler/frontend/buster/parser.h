#pragma once
#include <buster/base.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL BatchTestResult parser_tests(UnitTestArguments* arguments);
#endif

BUSTER_F_DECL void parser_experiments();
