#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/codegen/codegen_internal.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/string.h>

BUSTER_TEST_F_DECL UnitTestResult codegen_tests(UnitTestArguments* arguments);
