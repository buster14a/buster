typedef long L;
int printf(const char *, ...);
int n;
void f(void)
{
    int a[(unsigned)n];
    int b[(long unsigned)n];
    int c[(const short)n + (signed)1];
    int d[(unsigned long long int)n];
    int e[(L)n * (char)2];
    int g[(int long)n];
    int h[(unsigned char)n];
    int i[(signed char)n + (short int)n];
    a(); b(); c(); d(); e(); g(); h(); i();
}
