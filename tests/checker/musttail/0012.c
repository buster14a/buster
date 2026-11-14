int f(int);
int g(int x)
{
while (x)
{
switch (x)
{
case 1:
__attribute__((musttail)) return f(x);
}
{
{
__attribute__((musttail)) return f(x + 1);
}
}
}
return 0;
}
