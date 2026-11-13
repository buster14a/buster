void ordinary(void);
int ordinary_int(void);
void (*plain)(void);
void (*never)(void) __attribute__((noreturn));
typedef void (*NR)(void) __attribute__((noreturn));
NR alias;
int plain_call(void) { plain(); }
int never_call(void) { never(); }
int alias_call(void) { alias(); }
int never_branch(int n) { if (n) never(); }
int alias_branch(int n) { if (n) alias(); }
int plain_after_never_branch(int n) { if (n) never(); else plain(); }
int query_literal(void) { (void)__builtin_constant_p(1); }
int query_ordinary(void) { (void)__builtin_constant_p(ordinary_int()); }
_Noreturn int noreturn_int(void);
int query_never(void) { (void)__builtin_constant_p(noreturn_int()); }
int query_branch(int n) { if (n) (void)__builtin_constant_p(noreturn_int()); }
int object_size(void *p) { (void)__builtin_object_size(p, 0); }
int classify_ordinary(double x) { (void)__builtin_fpclassify(ordinary_int(), 1, 2, 3, 4, x); }
int classify_never(double x) { (void)__builtin_fpclassify(noreturn_int(), 1, 2, 3, 4, x); }
void analyzer(void) __attribute__((analyzer_noreturn));
int analyzer_branch(int n) { if (n) analyzer(); }
int analyzer_both(int n) { if (n) analyzer(); else analyzer(); }
_Noreturn void keyword(void);
void gnu(void) __attribute__((noreturn));
typeof(keyword) *keyword_pointer;
typeof(&keyword) keyword_address;
typeof(gnu) *gnu_pointer;
typeof(&gnu) gnu_address;
int keyword_typeof(void) { keyword_pointer(); }
int keyword_address_typeof(void) { keyword_address(); }
int gnu_typeof(void) { gnu_pointer(); }
int gnu_address_typeof(void) { gnu_address(); }
int cast_plain(void) { ((void (*)(void))never)(); }
int cast_never(void) { ((NR)plain)(); }
int conditional_plain(int n) { (n ? never : plain)(); }
int conditional_null(int n) { (n ? never : 0)(); }
int conditional_never(int n) { (n ? never : alias)(); }
int comma_plain(void) { (never, plain)(); }
int comma_never(void) { (plain, never)(); }
struct Callbacks { NR stop; void (*work)(void); };
int member_never(struct Callbacks *s) { s->stop(); }
int member_plain(struct Callbacks *s) { s->work(); }
NR callbacks[2];
int array_never(int n) { callbacks[n & 1](); }
NR provide(void);
int returned_never(void) { provide()(); }
void abort(void);
void exit(int);
void quick_exit(int);
int library_abort(void) { abort(); }
int library_exit(void) { exit(1); }
int library_quick_exit(void) { quick_exit(1); }
void longjmp(void *, int);
int library_longjmp_missing(void *p) { longjmp(p, 1); }
typedef void *jmp_buf;
void longjmp(void *, int);
int library_longjmp_known(void *p) { longjmp(p, 1); }
void overload(int) __attribute__((overloadable));
void overload(double) __attribute__((overloadable, noreturn));
int overload_plain(void) { overload(1); }
int overload_never(void) { overload(1.0); }
int object_size_never(void) { (void)__builtin_object_size((void *)(long)noreturn_int(), 0); }
int dynamic_object_size_never(void) { (void)__builtin_dynamic_object_size((void *)(long)noreturn_int(), 0); }
