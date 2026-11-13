int f(int n, const unsigned char *code) {
    static const void *const table[] = { &&add, &&twice, &&done };
    void *last = &&done;
    int acc = n;
    goto *table[*code];
add:
    acc += code[1];
    code += 2;
    goto *table[*code];
twice:
    acc += acc;
    last = &&twice;
    code += 1;
    goto *(acc > 100 ? table[2] : table[*code]);
done:
    return last == &&twice ? acc : -acc;
}
int g(int n) {
    void *out = &&end;
    for (int i = 0; i < 3; i++) { int v[(n & 3) + 1]; v[0] = i; if (v[0] == 2) goto *out; }
    n = 0;
end:
    return n;
}
