enum { A=__builtin_popcount(__builtin_bswap32(0x10203040)), B=__builtin_bswap16(0x1234) }; _Static_assert(A==5 && B==0x3412, "nested builtin");
