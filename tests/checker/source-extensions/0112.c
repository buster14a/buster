struct S{union{int x;long y;}; int z;}; int f(struct S *p){return p->x+p->z;}
