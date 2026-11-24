enum E { A }; enum F { B }; enum G { C };
void take(enum E);
void take_char(signed char);
void take2(enum E, signed char);
void takeb(_Bool);
void f(int c, enum F g, enum G h, enum E e, signed char t) {
    take(c ? (g = h) : g);
    take_char(c ? (t = 300) : 400);
    take2(c ? g : (g = h), c ? (t = 301) : 302);
    take(c ? (c ? (g = h) : g) : (g = h, g));
    take_char(1 ? 303 : (t = 304));
    takeb(c ? (t = 307) : (t = 308));
    take2(c ? g : (c ? (g = h) : g), c ? (t = 309) : 310);
    e = _Generic(c, int: c ? (g = h) : g);
    take(_Generic(c, int: c ? (g = h) : g));
    take(_Generic(c, int: c ? g : g));
    if (0) take(c ? (g = h) : g);
}
