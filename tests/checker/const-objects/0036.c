const char* const p="abc";
enum { E=p[1] };
_Static_assert(E=='b',"literal string element");
