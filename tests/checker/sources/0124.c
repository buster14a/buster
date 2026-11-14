struct B { int b : 3; };
void f(struct B s) {
    __auto_type x =
        s.b;
}
