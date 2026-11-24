typedef int V __attribute__((vector_size(64)));int side(void);V f(V x){return __builtin_ia32_pshufd512(x,__builtin_expect(1,side()));}
