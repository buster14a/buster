#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/tests/compiler/dwarf/dwarf_test.h>
#include <buster/lib/string.h>

BUSTER_TEST_F_DECL UnitTestResult compiler_driver_tests(UnitTestArguments* arguments);
