#define ID(x) x
#define SZ(x) sizeof(x)
#define INC(x) x++
#define PLUS +
int g(int);
void f(int i, int *p) {
    (void)sizeof(ID(i++)); (void)sizeof(INC(i)); (void)SZ(i++); (void)sizeof(ID((i++)));
    (void)sizeof(i = ID(1)); (void)sizeof(ID(i) = 1); (void)sizeof(i ID(=) 1); (void)sizeof(ID(i)++); (void)sizeof(++ID(i));
    (void)sizeof(ID(++)i); (void)sizeof(i ID(++)); (void)sizeof(ID(g)(i++)); (void)sizeof(g(ID(i)++));
    (void)sizeof(i++ PLUS 1); (void)sizeof(1 PLUS i++); (void)sizeof(ID(1) + i++); (void)sizeof(i ? ID(1) : i++);
    (void)sizeof(ID(i) ? 1 : i++); (void)sizeof((int)ID(i++)); (void)sizeof(ID((int))i++);
    (void)_Generic(ID(i++), int: 1); (void)sizeof(({ ID(i++); 1; }));
}
