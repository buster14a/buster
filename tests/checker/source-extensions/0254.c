#pragma pack(1)
struct S { char c; int x; };
_Static_assert(sizeof(struct S)==5,"");
