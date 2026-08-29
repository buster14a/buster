// The prototyped half of the unprototyped-call fixture. Nothing here is
// visible to the caller as anything but `()`, which is the point: the call
// sites in basic_c_unprototyped_call.c must place these arguments from their
// own promoted types and still land where these parameters are read.
typedef struct UnprototypedPair
{
    int a;
    int b;
} UnprototypedPair;

typedef struct UnprototypedBig
{
    long long a;
    long long b;
    long long c;
    long long d;
} UnprototypedBig;

int unprototyped_integers(int a, int b, int c, int d, int e, int f, int g, int h)
{
    return a * 1 + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8;
}

double unprototyped_floats(double a, double b, double c, int d, double e)
{
    return a + b * 2 + c * 3 + d * 4 + e * 5;
}

int unprototyped_promotions(int c, int s, int b, double f)
{
    return c + s * 2 + b * 3 + (int)(f * 4);
}

UnprototypedPair unprototyped_pair(int a, int b, int k)
{
    UnprototypedPair result;
    result.a = a * 10 + k;
    result.b = b * 10 + k;
    return result;
}

int unprototyped_big(UnprototypedBig value, int k)
{
    return (int)(value.a + value.b * 2 + value.c * 3 + value.d * 4) + k;
}

int unprototyped_none(void)
{
    return 91;
}

int unprototyped_adder(int a, int b)
{
    return a * 100 + b;
}

int unprototyped_muler(int a, int b)
{
    return a * b;
}
