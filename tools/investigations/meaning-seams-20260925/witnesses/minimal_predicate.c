static volatile int value;
int main(void) { return __builtin_constant_p(value && 0); }
