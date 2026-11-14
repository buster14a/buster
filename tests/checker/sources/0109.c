void f(void) {
    goto inner;
    {
        __label__ inner;
    inner:;
    }
}
