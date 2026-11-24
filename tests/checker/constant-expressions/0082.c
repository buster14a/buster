struct S { long x; };
enum { N=sizeof(__typeof__(((struct S){0}).x)) }; _Static_assert(N==sizeof(long), "compound member");
