void a(void) { auto x __asm__("x1") = 1; (void)x; constexpr int y __asm__("y1") = 2; (void)y; }
