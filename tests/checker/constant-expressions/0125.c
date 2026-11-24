typedef short Element; enum { N=3 }; _Static_assert(__builtin_offsetof(struct {char c; struct {Element a[N]; long x;} inner;},inner.x)==16, "nested offset");
