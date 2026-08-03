#pragma once

#include <buster/tests/test.h>
#include <buster/lib/window.h>

BUSTER_TEST_F_DECL UnitTestResult window_tests(UnitTestArguments* arguments);

#if defined(_WIN32)
BUSTER_TEST_F_DECL WmKey wm_win32_keyboard_key_for_test(u32 virtual_key);
BUSTER_TEST_F_DECL WmEventList wm_win32_text_events_for_test(Arena* arena, String16 units, bool flush);
#endif
