enum E { A }; enum F { B }; enum G { C };
void f(enum E e, enum F g, enum G h, int c, signed char sc, signed char t, _Bool b) {
    e = c ? (g = h) : g;
    e = c ? g : (g = h);
    e = c ? (c ? (g = h) : g) : (g = h);
    enum E e2 = c ? (g = h) : (enum F)(h = B);
    sc = c ? (t = 300) : 400;
    sc = c ? (c ? (t = 301) : 302) : (t = 303, 304);
    sc = 1 ? 305 : (t = 306);
    sc = 0 ? (t = 307) : 308;
    e = c ? (g = h, (sc = 309, g)) : _Generic(c, int: (g = h));
    b = c ? (t = 310) : (t = c ? 2 : 3);
    if (0) { e = c ? (g = h) : g; sc = c ? (t = 311) : 312; }
    (void)e2;
}
enum E r(int c, enum F g, enum G h, signed char t) { return c ? (g = h) : (t = 313, g); }
