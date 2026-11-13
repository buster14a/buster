void g(void) __attribute__((noreturn));
void g(void);
typeof(&g) a;
int first(void) { a(); }
typedef void F(void) __attribute__((noreturn));
F *b;
F direct;
int second(void) { b(); }
int third(void) { direct(); }
void (*plain)(void);
int raw_cast(void) { ((void (*)(void) __attribute__((noreturn)))plain)(); }
typedef void (*NR)(void) __attribute__((noreturn));
NR (*provide)(void);
int fourth(void) { provide()(); }
NR p;
int fifth(void) { ((typeof(p))plain)(); }
typedef NR Alias;
Alias alias;
int sixth(void) { alias(); }
int seventh(void) { (p=alias)(); }
int eighth(void) { (plain=(void(*)(void))alias)(); }
