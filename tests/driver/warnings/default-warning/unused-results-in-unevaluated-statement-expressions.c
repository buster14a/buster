int *q;
struct S { int m; } *ps, vs;
unsigned long f(int x) {
    return sizeof(({ x == 1; x != 1; x < 1; 1; })) + sizeof(({ (0, x); (void *)q; ps->m; vs.m; 1; })) +
           sizeof(({ x ? 1 : 2; ({ 3; 4; }); 5; })) + sizeof(x ? ({ 1; 2; }) : 3) + sizeof (({ *q; 1; })) +
           __alignof__(({ *q; x == 1; (void *)q; 1; })) + _Alignof(({ 1; 2; }));
}
