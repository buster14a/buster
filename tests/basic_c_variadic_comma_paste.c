// GNU's `, ## __VA_ARGS__` comma-deletion idiom, both ways.  With empty
// varargs the paste deletes the comma; with tokens present GNU performs no
// paste at all -- the comma stays and the arguments follow.  A preprocessor
// that ran a real paste there refused every non-empty call ("token paste
// ',##x' does not form one preprocessing token"), which is how CPython's
// Parser/pegen.c stopped compiling: its RAISE_SYNTAX_ERROR_KNOWN_LOCATION
// forwards `##__VA_ARGS__` through a second macro layer, so both the direct
// and the forwarded shape are pinned here.  The check is the arity of the
// call each expansion forms, which needs no library.
static int one_argument(int a)
{
    return a;
}

static int two_arguments(int a, int b)
{
    return a * 10 + b;
}

#define CALL(function, first, ...) function(first, ##__VA_ARGS__)
#define FORWARD(function, first, ...) CALL(function, first, ##__VA_ARGS__)

int main(void)
{
    // Empty varargs: the comma before __VA_ARGS__ disappears and the call
    // takes one argument.
    if (CALL(one_argument, 3) != 3)
    {
        return 1;
    }
    // Present varargs: no paste happens, the comma stays, and the call
    // takes both.
    if (CALL(two_arguments, 3, 4) != 34)
    {
        return 2;
    }
    if (FORWARD(one_argument, 5) != 5)
    {
        return 3;
    }
    if (FORWARD(two_arguments, 5, 6) != 56)
    {
        return 4;
    }
    return 0;
}
