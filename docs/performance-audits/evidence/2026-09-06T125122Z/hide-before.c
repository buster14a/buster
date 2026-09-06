extern int foo(void);
void hide(void) { __asm__(".hidden foo"); }
int later(void) { return foo(); }
