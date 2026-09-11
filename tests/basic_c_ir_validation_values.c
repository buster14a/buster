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
    int result;
    if (evaluations != 1) result = 1;
    else if (sizeof(low) != 8 || (char*)&low.value - (char*)&low != 2) result = 2;
    else if (sizeof(high) != 32 || (char*)&high.value - (char*)&high != 16) result = 3;
    else if (low.tag != 'a' || low.value != value || low.tail != 'z') result = 4;
    else if (high.tag != 'b' || high.value != value || high.tail != 'y') result = 5;
    else if (plain.value != value || plain.control != value) result = 6;
    else if (floats.single != single || floats.wide != wide) result = 7;
    else if (zero.single != 0 || zero.wide != 0) result = 8;
    else if (nested.low.value != value || nested.floats.wide != wide) result = 9;
    else if (choice.wide != wide || array[0] != wide || array[1] != single) result = 10;
    else
    {
        low.value = value + 1;
        result = low.value != value + 1;
    }
    return result;
}

int main(void)
{
    double _Complex zero = __builtin_complex(0.0, -0.0);
    double _Complex real = __builtin_complex(2.0, 0.0);
    double _Complex imaginary = __builtin_complex(0.0, 3.0);
    double _Complex nan = __builtin_complex(__builtin_nan(""), 0.0);
    int result;
    if (!equal(real, real) || equal(real, imaginary)) result = 11;
    else if (unequal(real, real) || !unequal(real, imaginary)) result = 12;
    else if (truth(zero) || !truth(real) || !truth(imaginary)) result = 13;
    else if (equal(nan, nan) || !unequal(nan, nan) || !truth(nan)) result = 14;
    else if (!finite(1.0) || finite(__builtin_huge_val()) || finite(__builtin_nan(""))) result = 15;
    else if (infinite(1.0) || !infinite(__builtin_huge_val()) || !infinite(-__builtin_huge_val()) || infinite(__builtin_nan(""))) result = 16;
    else result = initialize(123, 1.25f, 2.5);
    return result;
}
