// C11 6.5.15p6: the second and third operands of a conditional expression go
// through the usual conversions, which includes the array-to-pointer decay of
// 6.3.2.1p3. musl's `select`, `pselect`, `ppoll` and `utime` all pass an
// optional timeout the same way:
//
//   syscall_cp(SYS_ppoll, fds, n, to ? ((long[]){s, ns}) : 0, ...)
//
// The branch types were compared without decaying either one, so an array and
// a null pointer constant never met and the whole call failed with
// "conditional expression predicted incompatible branch types".
//
// The values are read through the resulting pointer at run time: a conditional
// that produced the wrong branch, or a pointer to the wrong object, still
// compiles and only the loaded elements say which one it was.

struct Timespec
{
    long seconds;
    long nanoseconds;
};

static long sum_pair(const long *pair)
{
    return pair ? pair[0] * 100 + pair[1] : -1;
}

static long sum_times(const struct Timespec *times)
{
    return times ? times[0].seconds * 1000 + times[1].seconds : -1;
}

static long fixed[2] = {5, 6};

int main(void)
{
    long seconds = 1;
    long nanoseconds = 2;

    // The musl shape: a compound literal on one side, a null pointer constant
    // on the other, consumed by a pointer parameter.
    int present = 1;
    if (sum_pair(present ? ((long[]){seconds, nanoseconds}) : 0) != 102)
    {
        return 1;
    }
    present = 0;
    if (sum_pair(present ? ((long[]){seconds, nanoseconds}) : 0) != -1)
    {
        return 2;
    }

    // The null constant on the other side, and spelled as a cast.
    present = 1;
    if (sum_pair(present ? 0 : ((long[]){seconds, nanoseconds})) != -1)
    {
        return 3;
    }
    if (sum_pair(present ? ((long[]){7, 8}) : (long *)0) != 708)
    {
        return 4;
    }

    // A named array rather than a literal, against a null constant and
    // against another pointer.
    if (sum_pair(present ? fixed : 0) != 506)
    {
        return 5;
    }
    long *other = fixed;
    if (sum_pair(present ? fixed : other) != 506)
    {
        return 6;
    }

    // The aggregate-element form utime.c writes, where the literal is an
    // array of structs.
    long actime = 11;
    long modtime = 22;
    if (sum_times(present ? ((struct Timespec[2]){{actime, 0}, {modtime, 0}}) : 0) != 11022)
    {
        return 7;
    }
    present = 0;
    if (sum_times(present ? ((struct Timespec[2]){{actime, 0}, {modtime, 0}}) : 0) != -1)
    {
        return 8;
    }

    // The result is a pointer: it is assignable to one, and the decay happens
    // whether or not the conditional is an argument.
    present = 1;
    const long *selected = present ? fixed : 0;
    if (!selected || selected[0] != 5 || selected[1] != 6)
    {
        return 9;
    }
    selected = present ? (long *)0 : fixed;
    if (selected)
    {
        return 10;
    }
    if (sizeof(present ? fixed : 0) != sizeof(long *))
    {
        return 11;
    }

    // Both branches arrays of one type still meet as that type's pointer.
    long spare[2] = {9, 10};
    if (sum_pair(present ? fixed : spare) != 506 || sum_pair(!present ? fixed : spare) != 910)
    {
        return 12;
    }

    // A char array against a string literal, which is the same decay on both
    // sides.
    char text[4] = {'a', 'b', 'c', 0};
    const char *chosen = present ? text : "xyz";
    if (chosen[0] != 'a')
    {
        return 13;
    }
    chosen = !present ? text : "xyz";
    if (chosen[0] != 'x')
    {
        return 14;
    }

    return 0;
}
