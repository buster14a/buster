int f(int n) {
    {
        __label__ odd;
        void *p = &&odd;
        if (n & 1) goto *p;
        return 0;
    odd:
        return 1;
    }
}
