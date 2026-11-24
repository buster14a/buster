typedef int A2[2];
typedef const int CA[2];
void f(void)
{
    int x[3] = (A2){1,2};
    long y[2] = (A2){1,2};
    int z[2] = (CA){1,2};
}
