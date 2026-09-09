#ifndef DIFFERENTIAL_ABI_H
#define DIFFERENTIAL_ABI_H
struct Pair { unsigned long long a; double b; };
struct Large { unsigned long long a, b, c; };
long long many_ints(signed char a, unsigned char b, short c, unsigned short d, int e, unsigned f, long long g, long long h);
double many_floats(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j);
struct Pair pair(struct Pair p, int x);
struct Large large(struct Large p, unsigned long long x);
long long host_ints(long long a, long long b, long long c, long long d, long long e, long long f, long long g, long long h);
struct Pair host_pair(struct Pair p);
int call_host(void);
#endif
