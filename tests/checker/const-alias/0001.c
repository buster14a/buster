const char* const p = "abc";
enum { E = p[0] };
_Static_assert(E == 97, "a");
