enum { A=3, B=sizeof(int[A]), C=(unsigned char)B }; _Static_assert(C==12, "enumerator");
