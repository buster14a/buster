// #3339: an integer constant expression converted to a floating type that can't hold it, -Wimplicit-const-int-float-conversion,
// on by default: clang's DiagRuntimeBehavior, at a body's end (unreachable code quiet) and at a file-scope declarator's, its
// value as APFloat prints it (an overflow's +Inf), the source's likely range first wider than the significand.
float file_float = 16777217;
double file_double = 9007199254740993LL;
float file_list[] = { 16777217, 1, 16777219 };
_Float16 file_half = 2049;
void take(float);
int printf(const char *, ...);
float constants(int i, float f, unsigned long long u)
{
    float a = 16777217;
    a = 16777216;
    a = (1 << 24) + 1;
    a = 0x7fffffff;
    a = -2147483647 - 1;
    a = 4294967295u;
    a = 18446744073709551615ull;
    double d = 18446744073709551615ull;
    a = (unsigned __int128)-1;
    long double l = 18446744073709551615ull;
    __float128 q = (unsigned __int128)-1;
    _Float16 h = 65535;
    h = 4097;
    __bf16 b = 257;
    a = i ? 16777217 : 1;
    a = (i, 16777219);
    take(33554433);
    if (f == 16777217) {}
    a = f * 16777217;
    a += 16777217;
    if (0)
        a = 16777217;
    a = 1 / 0;
    return a + (float)d + (float)l + (float)q + h + b + 16777217;
}

void take_mixed(double, int, float, double, ...);
void argument_edges(void)
{
    take_mixed(9007199254740993LL, 16777217, 16777217, 9007199254740995LL, 16777217);
}
