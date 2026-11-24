int *g = (int[(1, 2)]){ 3, 4 };
int f(int n)
{
    int *p = (int[(1, 2)]){ 5, 6 };
    int *r = (int[2][(1, 1)]){ { 7 }, { 8 } }[1];
    return p[0] + p[1] + r[0] + g[1] + n + (int)sizeof((int[(1, 3)]){ 0 });
}
int main(void) { return f(1) == 5 + 6 + 8 + 4 + 1 + 12 ? 0 : 1; }
