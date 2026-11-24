void f(int n) { enum { N=sizeof(({n++;n;})) }; _Static_assert(N==sizeof n, "unevaluated statement size"); }
