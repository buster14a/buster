typedef int V __attribute__((vector_size(64)));int f(V x,int n){return _Generic(__builtin_ia32_pshufd512(x,n),V:1,default:0);}
