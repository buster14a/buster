enum A { A1, A2 };
enum B { B1, B2 };
enum { C1 };
enum B vb;
void take(enum A);
enum A ret(void) { return _Generic(0, int: B2); }
void f(enum A a, int c)
{
    a = _Generic(1, int: A1);
    a = _Generic(1, int: C1);
    a = _Generic(1, int: 5);
    a = _Generic(1, long: A1, default: B2);
    a = _Generic(1, int: _Generic(2, int: B1));
    a = _Generic(1, int: vb);
    a = c ? _Generic(1, int: B1) : A1;
    take(_Generic(1, int: B1));
    enum A x = _Generic(1, int: (B1));
    enum A y[2] = { _Generic(1, int: B1), A2 };
    a = (_Generic(1, int: B1));
    a = _Generic(1, int: B1, char: A1);
}
