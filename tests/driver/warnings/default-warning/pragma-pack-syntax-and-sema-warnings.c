#pragma pack(3)
#pragma pack(push, r, 4)
#pragma pack(pop, r, 2)
#pragma pack(pop, nosuch)
#pragma pack
#pragma pack(foo)
#pragma pack(push, 4) extra
#pragma pack(pop, 32)
#pragma pack(push, 4
#pragma pack(push, r, x)
#pragma pack(+)
#pragma pack(show)
#pragma pack(2)
#pragma pack(show)
#pragma options align=packed
#pragma pack(show)
#pragma pack(pop)
#pragma pack(pop)
struct S { char c; int i; };
int main(void) { return sizeof(struct S) != 6; }
