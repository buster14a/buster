int target(void) __asm__("renamed_target");
int target(void) { return 7; }
int alias(void) __attribute__((alias("target")));
