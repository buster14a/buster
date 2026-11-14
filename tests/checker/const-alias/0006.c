const unsigned char* const p = (const unsigned char*)"abc";
enum { E = p[0] };
_Static_assert(E == 97, "a");
