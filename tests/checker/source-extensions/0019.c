typedef float V __attribute__((vector_size(64)));int f(V x){return _Generic(__builtin_ia32_rndscaleps_mask(x,0,x,65535,5),V:1,default:0);}
