struct S { int a, b; };
int f(__seg_gs int *p) { return *p + p[1]; }
void g(__seg_fs struct S *s, int v) { s->b = v; }
