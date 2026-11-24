int a1[(1, 2)];
int a2[1 ? 2 : (3, 4)];
int a3[(1, 2, 3)];
int a4[((void)0, 2)];
int a5[(1 + 1, 2)];
int a6[(sizeof(int), 2)];
int a7[sizeof((1, 2))];
int a8[((1, 0), 2)];
int a9[(1 == 1, 2)];
int a10[((char)1, 2)];
int a11[(-1, 2) + (3, 4)];
typedef int T[(5, 6)];
struct S { int m[(7, 8)]; int n : (1, 3); };
enum { E1 = (1, 2), E2 = ((void)0, 3), E3 = (E1, 4) };
int f(void)
{
    static int s[(1, 2)];
    enum { L = (3, 4) };
    int u = 1 / 0;
    return s[0] + L + u;
}
