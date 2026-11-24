enum A { A1, A2 } a;
enum B { B1 = 1, B2 = 4, B3 = 8 } b;
enum __attribute__((flag_enum)) F { F1 = 1, F2 = 4 } fl;
typedef enum A TA;
void g(enum A);
enum A h(void) { return 7; }
void f(int x)
{
    a = 5; a = 1; a = A2; a = x; a = -1; a = 1 ? 3 : 0;
    enum A c = 7; TA d = 9; b = 2; b = 4; b = 3; fl = 5; fl = 2;
    g(4); a += 9; a = (enum A)9; a = 'x';
    enum A arr[2] = { 1, 6 };
    struct { enum A m; } s = { 9 };
    (void)c; (void)d; (void)arr; (void)s;
}
enum A k = 9;
