int f(int n, void *p, int k) {
    void *t[2] = { &&even, &&odd };
    goto *(n & 1) ? t[1] : t[0];
even:
    goto *k ? p + 0 : &&odd;
odd:
    goto *n++, &&done;
done:
    goto *p = &&end;
end:
    return n;
}
