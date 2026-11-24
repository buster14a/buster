enum Small : signed char { X };
enum Small e = 300;
enum Small rs(void) { return 300; }
void ca(enum Small);
void cc(void) { ca(300); }
void as(void) { enum Small s; s = 300; (void)s; }
enum FB : _Bool { FA };
enum FB fb = 2;
enum E { A, B };
enum E ee = 5000000000LL;
enum Small lit = 2.5;
