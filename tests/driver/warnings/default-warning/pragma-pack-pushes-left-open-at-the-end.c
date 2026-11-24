#pragma pack(push, 4)
#pragma pack(push, inner, 1)
struct S { char c; int i; };
#pragma pack()
int main(void) { return sizeof(struct S) != 5; }
