struct DirectFlexible { int member[]; };
union DirectFlexibleUnion { int member[]; };
struct NestedFlexible { struct DirectFlexible member; };
union NestedFlexibleUnion { struct DirectFlexible member; };
struct NamedBit { int bit : 1; };
struct UnnamedBit { int : 1; };
struct PositiveArray { int member[1]; };
struct Forward;
typedef struct Forward Alias;
