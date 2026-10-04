struct S { int a; int b[3]; };
static void release(int* p) { *p = 0; }
int f(int k, void* q)
{
    int v __attribute__((cleanup(release))) = k;
    __typeof__(k) t = ({ int tmp = k * 2; tmp + 1; });
    int sel = _Generic(k, int: 1, default: 2);
    unsigned long off = __builtin_offsetof(struct S, b[1]);
    static void* targets[] = {&&one, &&two};
    if (__builtin_expect(k > 5, 0))
    {
        goto *targets[k & 1];
    }
one:
    t += 1;
two:
    t += (int)off + sel + (q != 0);
    return t + v;
}
