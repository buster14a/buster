long w(long a) __attribute__((overloadable));
long w(unsigned long a) __attribute__((overloadable));
long w(long long a) __attribute__((overloadable));
void t(void) { w(1); }
