int sink;
int f(int v)
{
    int both;
    int one;
    if (v > 2)
    {
        both = v + 1;
        one = v * 5;
        sink = one;
    }
    else
    {
        both = v - 1;
    }
    sink = both;            // line 16: both=4, one unavailable (intended)
    int total = 0;
    for (int i = 0; i < v; i += 1)
    {
        int t = i * 7 + 1;
        sink = t;           // line 21: first hit i=0 t=1, second hit i=1 t=8
        total = total + t;
    }
    sink = total;           // line 24: total=24
    return sink;
}
int main(void) { return f(3) == 0; }
