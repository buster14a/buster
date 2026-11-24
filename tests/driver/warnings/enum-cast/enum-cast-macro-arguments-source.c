enum A { A1 }; enum B { B1 };
#define ID(v) v
#define TWO(x, y) x; y
void take(enum A);
enum A r(void) { return ID(B1); }
void f(enum A a) {
    ID(a = B1);
    ID(take(B1));
    take(ID(B1));
    TWO(a = B1, a = ID(B1));
    a = ID(ID(B1));
    a = ID((B1));
    a = (ID(B1));
    enum A arr[1] = { ID(B1) };
    ID(enum A c = B1);
    (void)c;
}
