typedef int V __attribute__((vector_size(64)));int f(V x,int n){int a[_Generic(__builtin_ia32_pshufd512(x,n),V:2,default:1)];return sizeof(a);}
