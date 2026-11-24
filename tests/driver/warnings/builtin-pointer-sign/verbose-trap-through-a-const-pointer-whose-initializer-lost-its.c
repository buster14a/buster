static const char arr[] = "hello";
static const char* const q = (const char*)(const void*)arr;
static const char* const q2 = arr;
static const char* const q3 = (const char*)(const void*)"lit";
static const char* const q4 = (const char*)arr, *const q5 = (const char*)(void*)arr;
struct R { char m[8]; };
static const struct R r = { "member" };
static const char* const q6 = (const char*)(const void*)r.m;
void f1(void) { __builtin_verbose_trap("c", q); }
void f2(void) { __builtin_verbose_trap("c", q2); }
void f3(void) { __builtin_verbose_trap("c", q3); }
void f4(void) { __builtin_verbose_trap("c", q4); }
void f5(void) { __builtin_verbose_trap("c", q5); }
void f6(void) { __builtin_verbose_trap("c", q6); }
void f7(void) { __builtin_verbose_trap("c", (const char*)(const void*)q2); }
