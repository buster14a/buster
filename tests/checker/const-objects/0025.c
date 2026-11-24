const unsigned k=3;
struct S{unsigned b:k;};
_Static_assert(sizeof(struct S)==sizeof(unsigned),"bit-field storage");
