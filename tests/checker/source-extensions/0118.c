struct S{struct{int x;}; int values[];}; _Static_assert(sizeof(struct S)==4,"flexible");
