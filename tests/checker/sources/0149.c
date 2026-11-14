struct S { int a; };
int f(void);
int main(void) {
    constexpr struct S s =
        { f() };
    return s.a;
}
