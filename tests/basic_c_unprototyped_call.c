// A call to a function declared `()` before C23 supplies the parameters
// itself: the declaration names none, so the arguments take the default
// argument promotions and the call site is what tells the ABI where they go.
// Every callee here lives in basic_c_unprototyped_call_callee.c with a real
// prototype, so a misplaced argument is a wrong answer rather than a warning.
// The shapes are the ones the register assignment can get wrong: eight
// integers past the register file, floats interleaved with an integer, the
// promotions themselves (char/short/bool widen to int, float widens to
// double), a small aggregate returned by value, a large one passed by memory,
// the empty argument list, and a call through an unprototyped function
// pointer.
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

int unprototyped_integers();
double unprototyped_floats();
int unprototyped_promotions();
UnprototypedPair unprototyped_pair();
int unprototyped_big();
int unprototyped_none();
int unprototyped_adder();
int unprototyped_muler();

static int (*unprototyped_pointer)() = 0;

int main(void)
{
    if (unprototyped_integers(1, 2, 3, 4, 5, 6, 7, 8) != 204)
    {
        return 1;
    }
    if (unprototyped_floats(1.0, 2.0, 3.0, 4, 5.0) != 1.0 + 4.0 + 9.0 + 16.0 + 25.0)
    {
        return 2;
    }
    char narrow_char = 3;
    short narrow_short = 5;
    _Bool narrow_bool = 1;
    float narrow_float = 2.5f;
    if (unprototyped_promotions(narrow_char, narrow_short, narrow_bool, narrow_float) != 3 + 10 + 3 + 10)
    {
        return 3;
    }
    UnprototypedPair pair = unprototyped_pair(1, 2, 7);
    if (pair.a != 17 || pair.b != 27)
    {
        return 4;
    }
    UnprototypedBig big;
    big.a = 1;
    big.b = 2;
    big.c = 3;
    big.d = 4;
    if (unprototyped_big(big, 5) != 35)
    {
        return 5;
    }
    if (unprototyped_none() != 91)
    {
        return 6;
    }
    unprototyped_pointer = unprototyped_adder;
    if (unprototyped_pointer(3, 4) != 304)
    {
        return 7;
    }
    unprototyped_pointer = unprototyped_muler;
    if (unprototyped_pointer(3, 4) != 12)
    {
        return 8;
    }
    return 0;
}
