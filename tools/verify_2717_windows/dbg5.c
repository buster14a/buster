int sink;
int g(int x) { return x + 100; }
int f(int v)
{
    int a = v * 2;
    int b = v;
    int c = g(v);
    int d = 5;
    int e = v + c;
    long long w = (long long)v << 40;
    sink = a + b + c + d + e + (int)(w >> 40);
    return sink;
}
int main(void) { return f(3) == 0; }
