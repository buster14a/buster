struct S{int x;struct T{int x;struct U{int x;} u;} t;};_Static_assert(sizeof(struct S)==12,"layout");
