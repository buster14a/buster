int printf(const char *, ...);
typedef long L;
typedef unsigned U;
typedef struct P { int x; } P;
typedef int A2[2];
void f(int n)
{
    char *a = &(L){ 1 };
    char *b = (A2){ 1, 2 };
    int c = (P){ 1 };
    char *d = &(const L){ 2 };
    printf("%s %s\n", (L){ 3 }, (A2){ 0 });
    if (n < (U){ 4 }) {}
    char *e = (L[]){ 1, 2, 3 };
    char *g = &(P){ 0 }.x;
}
