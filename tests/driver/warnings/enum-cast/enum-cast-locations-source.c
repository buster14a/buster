enum E { A }; enum F { B };
void f(enum E e, enum F g, int c) {
    e = _Generic(c, int: g, default: B);
    e = (_Generic(c, int: (g), default: B));
    e = (e, g);
    e = ((e, g));
    e = (((e, g)));
    e = ((0, (g)));
    e = (c, (e, g));
    e = (g);
    e = ((g));
}
