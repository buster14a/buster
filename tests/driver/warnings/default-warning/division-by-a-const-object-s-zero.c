const int k = 0;
static const int ks = 0;
const volatile int kv = 0;
int nk = 0;
const long kl = 0;
const int ka[2] = { 0, 1 };
struct P { int m; };
const struct P kp = { 0 };
const int one = 1;
const char s[] = "a";
int f(int x) {
    const int lk = 0;
    const int lk2 = x;
    int r = 1 / k + 1 % ks + 1 / kv + 1 / nk + x / kl + 1 / lk + 1 / lk2 + 1 / ka[0] + 1 / kp.m + 1 / (k) + 1 / (int)k;
    if (0) { r = 1 / k; }
    r += (int)sizeof(1 / k) + 1 / one + 1 / s[1];
    r += x ? 1 / lk : 0;
    return r;
    r = 1 / k;
}
