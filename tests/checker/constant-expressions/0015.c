int a[2]; enum { N=sizeof(__builtin_bit_cast(int[2],a)) }; _Static_assert(N==8,"array representation");
