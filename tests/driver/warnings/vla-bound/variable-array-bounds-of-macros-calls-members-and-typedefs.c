#define N(x) ((x)*2)
int g(int);
struct S { int k; };
void f(int n, int m, struct S *s, int *ip)
{
    int a[N(n)]; a++;
    int c[g(n) + 1]; c++;
    int d[n ? m : 1]; d++;
    int e[-n + ~m]; e++;
    int h[s->k]; h++;
    int i[*ip]; i++;
    int j[n++]; j++;
    int k[sizeof n]; k++;
    int l[n][3]; l++;
    int o[3][n]; o++;
    typedef int T[n * m];
    T t; t++;
    int p[n << 1 | m]; p++;
    int q[ip[0]]; q++;
    int r[(int)sizeof(struct S) * n]; r++;
    int v[!n + 1]; v++;
}
