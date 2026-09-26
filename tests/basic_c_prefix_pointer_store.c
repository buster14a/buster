// A store through a pointer that the same expression pre-increments or
// pre-decrements: `*--q = c`. `--q` is `q -= 1` (C17 6.5.3.1p2) and not an
// lvalue (6.5.16p3), so the destination is the object the updated pointer
// designates (6.5.3.2p4), and the right operand converts to that object's
// type (6.5.16.1p2), never to the pointer's. The constraint pass read the
// identifier in front of `=` as the whole destination and refused every one
// of these as a conversion to the pointer type: QuickJS's `js_u64toa`
// (`*--q = '0' + digit`, int to char),
// libpng's `png_format_number` (`*--end = digits[...]`, const char to char),
// zstd's `tr_copy` (`*++d = s`, int to int) and musl's `vfprintf`.
//
// Each shape runs and its objects are checked afterwards, because a store
// that landed on the pointer or on the neighbouring element still compiles.

struct Pair
{
    int first;
    int second;
};

static const char digit_table[] = "0123456789abcdef";

// QuickJS: the character is an int expression converted on store.
static char* format_backward(char* q, unsigned long long n, unsigned base)
{
    int digit;
    if (base == 10)
    {
        do
        {
            digit = (int)(n % 10);
            n /= 10;
            *--q = '0' + digit;
        } while (n != 0);
    }
    else
    {
        do
        {
            digit = (int)(n % base);
            n /= base;
            *--q = digit_table[digit];
        } while (n != 0);
    }
    return q;
}

// zstd: an int store through a pre-incremented cursor into the same array.
static int* copy_forward(int* d, int const* source, int count)
{
    for (int index = 0; index < count; index += 1)
    {
        *++d = source[index];
    }
    return d;
}

static int text_equal(char const* left, char const* right)
{
    while (*left && *left == *right)
    {
        left += 1;
        right += 1;
    }
    return *left == *right;
}

static int answer(void)
{
    return 42;
}

int main(void)
{
    char buffer[32];
    buffer[31] = 0;
    char* text = format_backward(buffer + 31, 1234567890123ull, 10);
    if (!text_equal(text, "1234567890123") || text != buffer + 18)
    {
        return 1;
    }
    text = format_backward(buffer + 31, 0xbeefu, 16);
    if (!text_equal(text, "beef") || text != buffer + 27)
    {
        return 2;
    }

    int values[6] = {0, 0, 0, 0, 0, -1};
    int const source[4] = {7, 8, 9, 10};
    int* last = copy_forward(values, source, 4);
    if (last != values + 4 || values[0] != 0 || values[1] != 7 || values[4] != 10 || values[5] != -1)
    {
        return 3;
    }

    // A chained assignment takes the stored element's value, converted to
    // the element type, not the pointer's.
    unsigned char bytes[3] = {1, 2, 3};
    unsigned char* cursor = bytes + 2;
    int chained = *--cursor = 0x1ff;
    if (chained != 0xff || cursor != bytes + 1 || bytes[0] != 1 || bytes[1] != 0xff || bytes[2] != 3)
    {
        return 4;
    }

    // Floating, pointer, aggregate and function-pointer elements.
    double reals[2] = {0.0, 0.0};
    double* real = reals;
    *++real = 3;
    if (real != reals + 1 || reals[0] != 0.0 || reals[1] != 3.0)
    {
        return 5;
    }
    char* names[3] = {0, 0, 0};
    char** name = names + 3;
    *--name = buffer;
    if (name != names + 2 || names[2] != buffer || names[1] != 0)
    {
        return 6;
    }
    struct Pair pairs[2] = {{0, 0}, {0, 0}};
    struct Pair* pair = pairs;
    struct Pair filled = {5, 6};
    *++pair = filled;
    if (pair != pairs + 1 || pairs[1].first != 5 || pairs[1].second != 6 || pairs[0].first != 0)
    {
        return 7;
    }
    int (*calls[2])(void) = {0, 0};
    int (**call)(void) = calls + 2;
    *--call = answer;
    if (call != calls + 1 || calls[1]() != 42 || calls[0] != 0)
    {
        return 8;
    }

    // A pre-updated store beside a plain one: both designate elements.
    _Bool flags[2] = {0, 0};
    _Bool* flag = flags;
    *flag = 2;
    *++flag = 5;
    if (flags[0] != 1 || flags[1] != 1)
    {
        return 9;
    }
    return 0;
}
