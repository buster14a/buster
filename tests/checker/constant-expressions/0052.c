enum { N=sizeof(int[2+sizeof(char[3])]) }; _Static_assert(N==20, "nested");
