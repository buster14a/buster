// #2219: the spellings around an imaginary bit-precise constant that clang refuses, each its invalid suffix.
void f(void)
{
    (void)5wbif16;
    (void)5wbf16i;
    (void)5iwbi;
    (void)5wbwbi;
    (void)5iwbwb;
    (void)5.0wbi;
    (void)5lwbi;
    (void)5wbli;
    (void)5wBi;
    (void)5iwB;
    (void)5uwbui;
    (void)0x1p3wbi;
}
