struct S{char c;
#pragma pack(1)
int x;};
struct T{char c;int x;};
_Static_assert(sizeof(struct S)==8,"");
_Static_assert(sizeof(struct T)==5,"");
