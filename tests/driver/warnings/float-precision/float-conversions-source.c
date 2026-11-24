typedef float F;
typedef double D;
typedef const double CD;
struct P { float x; F y; double z; };
union U { float f; double d; };
float take(float, ...);
float old();
void vf(F);
static float ret(D d) { return d; }
static long double ret_quad(__float128 q) { return q; }
#define SET(a, b) ((a) = (b))
#define WRAP(x) (x)
static float gf = (float)1.0 + 0.1;
static struct P gp = { 0.1, 0.25, 0.1L };
static float garr[] = { [1] = 0.1, [0] = 1.0 };
static struct P gnest[2] = { { 0.5, 0.1 }, { .z = 0.1L } };
static double gq = 1.1Q;
static _Complex float gz = 0.1 + 0.25i;
float use(double d, long double l, F f, D dd, CD cd, volatile double vd, struct P* p, union U u, int i, _Float16 h, __bf16 b, _Complex double z)
{
    float a = d, c = l;
    F e = dd;
    float g = cd;
    float k = vd;
    _Complex float w = z;
    p->x = d;
    p->y = p->z;
    u.f = u.d;
    f = take(d, d);
    f = old(d);
    vf(d);
    vf(WRAP(d));
    SET(f, d);
    f = WRAP(d);
    f = ((d));
    f = i ? d : l;
    f = i ? (d, f) : 0.5;
    f = (struct P){ d, 0.1, 0.1 }.x;
    f = ({ d; });
    f = __builtin_choose_expr(1, d, f);
    f = _Generic(i, int: d);
    f = p->z;
    f = (&d)[0];
    f = __builtin_fabs(d);
    f = ++d;
    f = d++;
    f = (d = 0.5);
    p->x += d;
    p->y -= 1.0;
    f *= cd;
    f /= 0.5;
    h += 1.0;
    h *= b;
    b /= h;
    d -= l;
    dd += l;
    vd += l;
    h = f;
    b = h;
    f = d > 0 ? 1.0 : 0.1f;
    f = 1e39;
    f = 1e-50;
    f = 0x1p-150;
    f = __builtin_nan("1");
    f = __builtin_nans("");
    f = 1.0 / 3.0;
    h = 65520.0;
    h = 0.1f16;
    b = 1.0078125;
    if (0)
        f = d;
    i = i || (f = d);
    return a + c + e + g + k + ret(d) + (float)ret_quad(2.0Q) + __real__ w + f + gp.x + gnest[0].y + garr[0] + gf + (float)gq + __real__ gz;
}
