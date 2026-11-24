typedef char C __attribute__((aligned(8)));
C *p = 1;
C c = "x";
struct X;
typedef struct X TX __attribute__((aligned(8)));
TX a[3];
