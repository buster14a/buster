int strings(const char *s, int x) {
    int r = s == "x";
    r += "x" != s;
    r += s < "x";
    r += (char *)"x" == s;
    r += ("x") == s;
    r += "x" == "x";
    r += "x" == "y";
    r += "x" == 0;
    r += "x" != (void *)0;
    r += "x" == (char *)0;
    r += "x" == (1 - 1);
    r += sizeof(s == "x");
    if (0) r += s == "x";
    r += 0 && (s == "x");
    r += _Generic(x, int: s == "x", default: s == "x");
    return r;
    r += s == "x";
}
