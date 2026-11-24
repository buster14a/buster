__attribute__((naked)) void n(void) { __asm__("ret"); }
int main(void) { n(); return 0; }
