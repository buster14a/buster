enum { N=__builtin_bit_cast(int,0xffffffffu), P=__builtin_bit_cast(unsigned, -1) }; _Static_assert(N==-1 && P==0xffffffffu,"integer representation");
