const unsigned char a[] = "abc";
enum { E = a[0] };
_Static_assert(E == 97, "a");
