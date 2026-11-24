typedef struct { int x; } S, *SP;
typedef S *SP2;
typedef struct { int y; } *TP, T;
void h(SP a, SP2 b, TP c){ int *p = a; int *q = b; int *r = c; (void)p; (void)q; (void)r; }
