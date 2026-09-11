// Certified frontend output must already satisfy the canonical value contract.
// Calls keep the operands live; both frontend forms and all allocators run it.
typedef int __attribute__((aligned(2))) LowInt;
typedef int __attribute__((aligned(16))) HighInt;
struct Low { char tag; volatile LowInt value; char tail; };
struct High { char tag; const HighInt value; char tail; };
struct Plain { volatile int value; const int control; };
struct Floats { volatile float single; volatile double wide; };
struct Nested { struct Low low; struct Floats floats; };
union Choice { volatile double wide; int integer; };

int equal(double _Complex a, double _Complex b) { return a == b; }
int unequal(double _Complex a, double _Complex b) { return a != b; }
int truth(double _Complex a) { return a ? 1 : 0; }
int finite(double a) { return __builtin_isfinite(a); }
int infinite(double a) { return __builtin_isinf(a); }

int initialize(int value, float single, double wide)
{
    int evaluations = 0;
    struct Low low = {'a', (evaluations++, value), 'z'};
    struct High high = {'b', value, 'y'};
    struct Plain plain = {value, value};
    struct Floats floats = {single, wide};
    struct Floats zero = {0};
    struct Nested nested = {{'c', value, 'x'}, {single, wide}};
    union Choice choice = {.wide = wide};
    volatile double array[2] = {wide, single};
    if (evaluations != 1) return 1;
    if (sizeof(low) != 8 || (char*)&low.value - (char*)&low != 2) return 2;
    if (sizeof(high) != 32 || (char*)&high.value - (char*)&high != 16) return 3;
    if (low.tag != 'a' || low.value != value || low.tail != 'z') return 4;
    if (high.tag != 'b' || high.value != value || high.tail != 'y') return 5;
    if (plain.value != value || plain.control != value) return 6;
    if (floats.single != single || floats.wide != wide) return 7;
    if (zero.single != 0 || zero.wide != 0) return 8;
    if (nested.low.value != value || nested.floats.wide != wide) return 9;
    if (choice.wide != wide || array[0] != wide || array[1] != single) return 10;
    low.value = value + 1;
    return low.value != value + 1;
}

int main(void)
{
    double _Complex zero = __builtin_complex(0.0, -0.0);
    double _Complex real = __builtin_complex(2.0, 0.0);
    double _Complex imaginary = __builtin_complex(0.0, 3.0);
    double _Complex nan = __builtin_complex(__builtin_nan(""), 0.0);
    if (!equal(real, real) || equal(real, imaginary)) return 11;
    if (unequal(real, real) || !unequal(real, imaginary)) return 12;
    if (truth(zero) || !truth(real) || !truth(imaginary)) return 13;
    if (equal(nan, nan) || !unequal(nan, nan) || !truth(nan)) return 14;
    if (!finite(1.0) || finite(__builtin_huge_val()) || finite(__builtin_nan(""))) return 15;
    if (infinite(1.0) || !infinite(__builtin_huge_val()) || !infinite(-__builtin_huge_val()) || infinite(__builtin_nan(""))) return 16;
    return initialize(123, 1.25f, 2.5);
}
