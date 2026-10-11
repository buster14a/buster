int sink;
int f(int v)
{
    int once = v * 2;
    int twice = v * 3;
    sink = once + twice;   // line 6
    twice = twice + 1;
    sink = once + twice;   // line 8
    return sink;
}
int main(void) { return f(3) == 0; }
