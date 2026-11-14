#pragma pack(push, 8uwb)
struct A { char c; double d; };
#pragma pack(pop)
#pragma pack(push, 2wb)
struct B { char c; int i; };
#pragma pack(pop)
#pragma pack(4)
#pragma pack(0x10000000000000001uwb)
struct C { char c; double d; };
#pragma pack(0x1000000000000000000000000000000001uwb)
struct D { char c; double d; };
#pragma pack()
_Static_assert(sizeof(struct A) == 16 && sizeof(struct B) == 6, "small values apply");
_Static_assert(sizeof(struct C) == 12 && sizeof(struct D) == 12, "wide values leave pack(4)");
