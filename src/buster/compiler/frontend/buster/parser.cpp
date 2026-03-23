#pragma once
#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/assertion.h>

STRUCT(ParserResult)
{
};

BUSTER_F_DECL void parser_experiments()
{
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_IMPL BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    BUSTER_UNUSED(arguments);
    return result;
}
#endif
