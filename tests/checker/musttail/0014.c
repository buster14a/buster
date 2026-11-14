int f(int);
int g(int x)
{
return x + ({
__attribute__((musttail)) return f(x);
0;
});
}
