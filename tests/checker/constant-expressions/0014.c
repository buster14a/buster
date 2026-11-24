__fp16 x; enum { N=sizeof(__builtin_bit_cast(unsigned short,x)), M=sizeof(__builtin_bit_cast(__fp16,(unsigned short)0x3c00)) }; _Static_assert(N==2 && M==2,"half representation");
