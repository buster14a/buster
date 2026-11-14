const signed char* const p = (const signed char*)"abc";
enum { E = p[0] };
_Static_assert(E == 97, "a");
