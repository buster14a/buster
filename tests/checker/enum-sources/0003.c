enum { P=4, N=P+sizeof(struct S { char a[P]; })+sizeof(struct S) }; _Static_assert(N==12 && sizeof(struct S)==4,"tag completion identity");
