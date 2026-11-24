static int calls;static unsigned next(unsigned v){calls++;return v;}
int tested(int n){unsigned u=(unsigned)n|1u;return (int)((__builtin_popcount)(next(u))+((__builtin_clz))(u)*100+(__builtin_bswap32)(u)%1000*10000)+calls*7;}
