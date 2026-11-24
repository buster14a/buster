// Moves its lines and its file's name, and warns after it.
#include "probe.h"
#line 9000 "elsewhere.c"
int lines_shift = 1 << 42;
# 77 "marker.h" 1
int lines_marker = 1 << 43;
# 9002 "elsewhere.c" 2
int lines_back = __LINE__;
const char* lines_file = __FILE__;
