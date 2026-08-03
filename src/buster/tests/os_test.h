#pragma once

#include <buster/tests/test.h>
#include <buster/lib/os.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/string.h>

#if defined(__linux__) || defined(__APPLE__)
BUSTER_TEST_F_DECL int generic_fd_to_posix(OsFileDescriptor* fd);
#endif
#if defined(__APPLE__)
BUSTER_TEST_F_DECL bool os_apple_process_is_traced(u32 process_flags);
#elif defined(_WIN32)
BUSTER_TEST_F_DECL bool os_windows_pipe_disable_inheritance(OsFileDescriptor* pipe);
#endif

BUSTER_TEST_F_DECL UnitTestResult os_tests(UnitTestArguments* arguments);
