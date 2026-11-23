typedef int V __attribute__((vector_size(64)));
int f(V x){return _Generic(__builtin_ia32_pshufd512(x,255),V:1,default:0)+_Generic(__builtin_ia32_pause(),int:0,default:1);}
