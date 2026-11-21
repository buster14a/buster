int first(void) { return 1; }
int first(void) __asm__("first_label") __attribute__((used));
int leading(void) { return 2; }
__attribute__((used)) int leading(void) __asm__("leading_label");
int between(void) { return 3; }
int between(void) __attribute__((used)) __asm__("between_label");
int unknown(void) { return 4; }
int unknown(void) __asm__("unknown_label") __attribute__((weak, not_an_attribute));
