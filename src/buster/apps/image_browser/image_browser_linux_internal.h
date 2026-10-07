#pragma once

#include <buster/apps/image_browser/image_browser_linux.h>

#if BUSTER_INCLUDE_TESTS
// Runs the catalog path sort on `count` descriptors with `scratch` as its
// count-descriptor merge buffer and returns the path comparisons it made.
BUSTER_F_DECL u64 image_browser_linux_test_sort(String8* paths, String8* scratch, u64 count);
#endif
