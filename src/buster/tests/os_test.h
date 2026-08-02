#pragma once

#include <buster/tests/test.h>
#include <buster/lib/os.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/string.h>

#if defined(__linux__) || defined(__APPLE__)
BUSTER_TEST_F_DECL int generic_fd_to_posix(OsFileDescriptor* fd);
#endif

BUSTER_TEST_F_DECL UnitTestResult os_tests(UnitTestArguments* arguments);
