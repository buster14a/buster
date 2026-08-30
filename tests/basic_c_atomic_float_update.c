// Compound assignment and increment on an `_Atomic` floating-point object
// (issue #821). Both were refused, while the same operators on atomic integers
// worked and a plain load/modify/store of an atomic float worked -- so the gap
// was the read-modify-write, not the type.
//
// C11 6.5.16.2p3 makes a compound assignment on an atomic type one
// read-modify-write, and no hardware has floating-point atomic arithmetic, so
// this lowers to a compare-exchange loop over the object's bit pattern, which
// is what clang emits for the same source.
//
// The comparison the loop retries on is bitwise, and the last two blocks are
// why: a float comparison never settles once the object holds a NaN, and calls
// a negative zero equal to a positive one. A run that hangs is the regression
// this file catches.

static _Atomic float atomic_float = 2.0f;
static _Atomic double atomic_double = 2.0;
static _Atomic int atomic_int = 5;

int main(void)
{
    atomic_float += 1.5f;
    if ((int)(atomic_float * 10.0f) != 35)
    {
        return 1;
    }
    atomic_float -= 0.5f;
    if ((int)(atomic_float * 10.0f) != 30)
    {
        return 2;
    }
    atomic_float *= 2.0f;
    if ((int)atomic_float != 6)
    {
        return 3;
    }
    atomic_float /= 3.0f;
    if ((int)atomic_float != 2)
    {
        return 4;
    }
    atomic_double++;
    if ((int)atomic_double != 3)
    {
        return 5;
    }
    ++atomic_double;
    if ((int)atomic_double != 4)
    {
        return 6;
    }
    atomic_double--;
    if ((int)atomic_double != 3)
    {
        return 7;
    }
    {
        // The postfix form answers the value from before the exchange and the
        // prefix form the one from after, which is the pair the loop carries
        // out of its own blocks.
        double before = atomic_double++;
        if ((int)before != 3 || (int)atomic_double != 4)
        {
            return 8;
        }
        double after = ++atomic_double;
        if ((int)after != 5 || (int)atomic_double != 5)
        {
            return 9;
        }
        float assigned = (atomic_float += 1.0f);
        if ((int)assigned != 3 || (int)atomic_float != 3)
        {
            return 10;
        }
    }
    {
        // The controls: the integer read-modify-write and the plain
        // load/modify/store of an atomic float, both of which already worked.
        atomic_int += 3;
        atomic_int ^= 1;
        atomic_int++;
        if (atomic_int != 10)
        {
            return 11;
        }
        atomic_float = atomic_float + 1.5f;
        if ((int)(atomic_float * 10.0f) != 45)
        {
            return 12;
        }
    }
    {
        // A NaN in the object. The loop compares bit patterns, so it settles;
        // comparing the two floats would spin forever.
        atomic_double = 0.0;
        atomic_double /= 0.0;
        atomic_double += 1.0;
        if (atomic_double == atomic_double)
        {
            return 13;
        }
    }
    {
        // A negative zero differs from a positive one bitwise and compares
        // equal as a float, which is the other half of the same rule.
        atomic_float = -0.0f;
        atomic_float += 0.0f;
        if (atomic_float != 0.0f)
        {
            return 14;
        }
    }
    return 0;
}
