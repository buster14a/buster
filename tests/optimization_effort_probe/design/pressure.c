/* Frozen design control: many values live through calls. */
typedef unsigned long long U64;

static U64 consume(U64 a, U64 b, U64 c, U64 d, U64 e, U64 f, U64 g, U64 h)
{
    return a + 3*b + 5*c + 7*d + 11*e + 13*f + 17*g + 19*h;
}

U64 probe(U64 x, unsigned n)
{
    U64 a = x + 1, b = x * 3 + n, c = x ^ 0x4567ULL, d = x >> 3;
    U64 e = x * 17, f = x + n * 21, g = x ^ (x << 11), h = x * x + 5;
    U64 i = (x >> 7) + n, j = (x << 3) + 41, k = x ^ n, l = x * 73;
    U64 m = x + 79, o = x ^ 83, p = x * 89, q = x + 97;
    U64 first = consume(a,b,c,d,e,f,g,h);
    U64 second = consume(i,j,k,l,m,o,p,q);
    U64 third = consume(q,p,o,m,l,k,j,i);
    return first + second + third + a+b+c+d+e+f+g+h+i+j+k+l+m+o+p+q;
}
