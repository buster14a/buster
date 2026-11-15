// #2678's table: Tier 0 took each constant as an int and converted it at run time.
unsigned long f1(unsigned long i) { return i + 1; }
long f2(long x) { return x == 0; }
unsigned long f3(unsigned long i) { return i * 8; }
double f4(double d) { return d + 1; }

unsigned long gu;
long gl;
double gd;

int tested(int n)
{
    gu = (unsigned long)n * 2654435761ul;
    gl = -(long)n;
    gd = n;
    return (int)(f1(gu) ^ f3(gu) ^ (unsigned long)f2(gl) ^ (unsigned long)f2(gl + (long)n) ^ (unsigned long)(long)f4(gd));
}
