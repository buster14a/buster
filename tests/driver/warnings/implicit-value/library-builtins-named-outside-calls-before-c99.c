int g(void) { return abs; }
void h(void) { void (*fp)(void) = free; (void)fp; }
