typedef int V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_pshufd512(x,255);}
