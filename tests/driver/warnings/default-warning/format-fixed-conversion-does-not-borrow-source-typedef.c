typedef _BitInt(32) T;
void f(const char*, _BitInt(32)*) __attribute__((format(printf,1,2)));
void g(void) { f("%d", (T)0); }
