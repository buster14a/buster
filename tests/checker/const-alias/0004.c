const unsigned char a[] = "abc";
enum { E = *(const char*)a };
_Static_assert(E == 97, "a");
