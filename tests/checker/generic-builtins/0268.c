typedef double D __attribute__((aligned(16)));
volatile D v;
const double c;
_Complex double f(void)
{
return __builtin_complex(v, c);
}
