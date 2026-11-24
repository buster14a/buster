int (*callbacks[2])(int); int f(void) { return callbacks[1](3); }
