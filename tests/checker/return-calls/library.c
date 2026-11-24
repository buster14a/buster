// Resolve library contracts at the declaration, before a later typedef or redeclaration.
void abort(void);
void longjmp(void *, int);
int ordinary_jump(void *p) { longjmp(p, 1); }
typedef void *jmp_buf;
int still_ordinary(void *p) { longjmp(p, 1); }
void longjmp(void *, int);
int still_returns(void *p) { longjmp(p, 1); }
typedef int sigjmp_buf[16];
void siglongjmp(sigjmp_buf, int);
int stops(sigjmp_buf p) { siglongjmp(p, 1); }
int split(int x) { if (x) abort(); }
int through_type(int x) { if (x) return 1; (x ? abort : abort)(); }
void quit(void) __attribute__((analyzer_noreturn));
int analyzer_split(int x) { if (x) quit(); }
void local_annotation(void);
int scoped(int x) {
    if (x) {
        extern void local_annotation(void) __attribute__((analyzer_noreturn));
        local_annotation();
    }
    local_annotation();
}
int outside(void) { local_annotation(); }
void handle(int) __attribute__((overloadable));
_Noreturn void handle(double) __attribute__((overloadable));
int selected_returns(void) { handle(1); }
int selected_stops(void) { handle(1.0); }
