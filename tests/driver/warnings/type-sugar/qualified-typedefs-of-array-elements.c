typedef const int CI;
typedef volatile int VI;
typedef const int CA[3];
void f(void)
{
    CI arr[2] = { 1, 2 };
    arr[0] = 3;
    arr++;
    VI v[4];
    v++;
    CA m[2] = { 0 };
    m[1][2] = 4;
    m++;
}
