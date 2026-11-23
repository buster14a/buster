struct S{int x;}; _Static_assert(__builtin_expect(1,(struct S){0}),"type");
