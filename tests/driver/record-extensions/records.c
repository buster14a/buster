struct Empty {};
union EmptyUnion {};
typedef struct {} EmptyAlias;
struct OneZeroBits { int : 0; };
struct TwoZeroBits { int : 0; long : 0; };
struct OneBit { int : 1; };
struct OneEmpty { struct Empty member; };
struct TwoEmpty { struct Empty first, second; };
union TwoUnionEmpty { struct Empty first, second; };
struct Anonymous { struct {}; };
struct TwoAnonymous { struct {}; struct {}; };
struct OneZeroArray { int member[0]; };
struct TwoZeroArrays { int first[0], second[0]; };
union TwoUnionZeroArrays { int first[0], second[0]; };
struct Aligned {} __attribute__((aligned(16)));
struct Packed {} __attribute__((packed));
struct Nonempty { int member; };
void local(void)
{
    struct Local {} value;
    (void)value;
}
