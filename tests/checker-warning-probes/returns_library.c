#ifndef BEFORE
#define BEFORE 0
#endif
#if BEFORE
void longjmp(void *, int);
int old(void *p) { longjmp(p, 1); }
#endif
typedef int jmp_buf[8];
void longjmp(jmp_buf, int);
int known(jmp_buf p) { longjmp(p, 1); }
int branch(jmp_buf p, int n) { if (n) longjmp(p, 1); }
void plain(void);
void mark(void) __attribute__((analyzer_noreturn));
void mark(void);
int inherited(void) { mark(); }
int inherited_branch(int n) { if (n) mark(); }
int sibling_one(void) { void plain(void) __attribute__((analyzer_noreturn)); plain(); }
int sibling_two(void) { void plain(void); plain(); }
int outer(void) { plain(); }
int first_block(void) { extern void newname(void) __attribute__((analyzer_noreturn)); newname(); }
int second_block(void) { extern void newname(void); newname(); }
void newname(void);
int later_file(void) { newname(); }
_Noreturn void noreturn_body(void (*p)(void)) { p(); }
