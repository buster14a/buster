int f(int x)
{
    int y;
    __asm__("movl %1, %0" : "=r"(y) : "r"(x));
    return y;
}
