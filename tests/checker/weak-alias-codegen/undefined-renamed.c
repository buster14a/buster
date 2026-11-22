int target(void) __asm__("renamed_target");
int target(void) { return 7; }
#pragma weak alias = target
