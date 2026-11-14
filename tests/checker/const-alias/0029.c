const int k = 0;
enum { E = *(const unsigned char*)&k };
_Static_assert(E == 0, "zero");
