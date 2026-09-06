extern int foo(void);
int later(void) { return foo(); }
void hide(void) { __asm__(".hidden foo"); }
