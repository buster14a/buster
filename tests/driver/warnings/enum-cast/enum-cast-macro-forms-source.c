enum A { A1 }; enum B { B1, B2 };
#define ID(v) v
#define B_VAL B1
#define PICK(c, x, y) ((c) ? (x) : (y))
#define INIT(v) { v }
#define ASSIGN(l, r) l = r
void take(enum A);
enum A g(int c) {
    enum A a = ID(B_VAL);
    a = PICK(c, B1, A1);
    a = c ? ID(B1) : ID(B2);
    enum A arr[2] = INIT(B1);
    ASSIGN(a, B1);
    ASSIGN(a, ID(B1));
    ID(enum A arr2[1] = { B1 });
    take(PICK(c, B1, B2));
    (void)arr; (void)arr2;
    return ID(c) ? B1 : A1;
}
