extern int target __asm__("actual_target") __attribute__((weak,visibility("hidden"))); int f(void){return target;}
