_Atomic int f(void) { return 3.5; }
_Atomic int gpar(_Atomic int);
void h(void) { gpar(3.5); }
_Atomic char g(void) { return 300; }
void gc(_Atomic char);
void kc(void) { gc(300); }
struct S { int i; _Atomic int m; };
void k(void) { struct S t; t.m = 3.5; t.m = 5000000000LL; (void)t; }
int fi(void) { return 2.5; }
void pi(int);
void hp(void) { pi(2.5); }
char fc(void) { return 300; }
