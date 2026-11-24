const _Atomic(int) az=0;
enum { E=az };
_Static_assert(E==0,"const atomic value");
