int f(int n) {
    int s = 0;
    {
        __label__ again, done;
    again:
        if (n-- > 0) { s += 1; goto again; }
        {
            __label__ again;
        again:
            if (s > 100) { s -= 1; goto again; }
        }
        goto done;
    done:;
    }
    if (s == 0) goto again;
    return s;
again:
    return -s;
}
