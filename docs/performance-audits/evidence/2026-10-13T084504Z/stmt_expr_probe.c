typedef int word;
int f(int x)
{
    int a = x ? ({ int t = x + 1; t; }) : 0;
    int b = x ? ({ word u = x; u * 2; }) : 1;
    __typeof__(({ int v = x; v; })) c = 3;
    unsigned long d = sizeof(({ word w = x; w; }));
    return a + b + c + (int)d;
}
