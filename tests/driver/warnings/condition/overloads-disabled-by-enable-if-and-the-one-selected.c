int f(int a) __attribute__((overloadable, enable_if(a > 0, "positive")));
char f(long a) __attribute__((overloadable));
_Static_assert(sizeof(f(1)) == 4, "enabled");
_Static_assert(sizeof(f(-1)) == 1, "disabled");
void t(int x) { (void)f(x); }
