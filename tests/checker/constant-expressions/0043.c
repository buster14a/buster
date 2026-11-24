typedef int V __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_shufflevector(a,a,0,1,2,8)) };
