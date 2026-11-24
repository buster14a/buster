typedef float V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_rndscaleps_mask(x,0,x,65535,5);}
