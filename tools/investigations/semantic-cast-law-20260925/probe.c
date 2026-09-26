/* Diagnostic research only. Z is a predeclared integer constant expression.
 * Positive rows have independently established mathematical value zero.
 * Only scalar values/control flow are observed, never object padding. */
#if CASE_KIND == 0
int check(void) { return (Z) != 0; }
#elif CASE_KIND == 1
int check(void) { int *p = 0; return !(p == (Z)); }
#elif CASE_KIND == 2
int check(void) { int object = 7; int *p = &object; return !(p != (Z)); }
#elif CASE_KIND == 3
typedef union __attribute__((transparent_union)) { int *i; void *v; } pointer_argument;
int accept(pointer_argument p) { return p.i != 0; }
int check(void) { return accept(Z); }
#elif CASE_KIND == 4
int check(void)
{
    int result;
    void *table[2] = {&&good, &&bad};
    goto *table[Z];
bad: result = 1; goto done;
good: result = 0;
done: return result;
}
#elif CASE_KIND == 5
int check(void)
{
    int result;
    void *table[2] = {&&bad, &&bad};
    table[Z] = &&good;
    goto *table[0];
bad: result = 1; goto done;
good: result = 0;
done: return result;
}
#elif CASE_KIND == 6
int check(void) { int table[2] = {11, 23}; return table[Z] != 11; }
#elif CASE_KIND == 7
int check(void) { int *p = Z; return p != 0; }
#elif CASE_KIND == 8
int check(void) { int *p = 0; return (1 ? p : (Z)) != 0; }
#endif
