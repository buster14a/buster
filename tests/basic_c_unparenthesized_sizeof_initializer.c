// `sizeof` over an expression takes no parentheses of its own (C11 6.5.3.4):
// `sizeof buf` is the size of `buf`. The static-initializer constant
// evaluator only recognized the parenthesized spelling, so musl's
//
//   static size_t old_tz_size = sizeof old_tz_buf;              (src/time/__tz.c)
//   const struct __locale_map __c_dot_utf8 = {
//           .map = empty_mo, .map_size = sizeof empty_mo, ... }; (src/locale/c_locale.c)
//
// failed as "unsupported C global initializer". The parenthesized form and
// function-scope `sizeof` already worked, so the fixture pairs each
// unparenthesized initializer with the answer clang gives and with the same
// size measured at run time, where a wrong fold would have to be wrong twice
// in the same way to pass.

static char bytes[10];
static int words[7];

struct Point
{
    long x;
    long y;
};

static struct Point points[3];

struct Map
{
    const void *map;
    unsigned long map_size;
    const char *name;
};

// The two musl shapes: a scalar initializer, and a designated member of an
// aggregate initializer.
static unsigned long byte_size = sizeof bytes;
static unsigned long word_size = sizeof words;
static const struct Map utf8 = {
    .map = words,
    .map_size = sizeof words,
    .name = "C.UTF-8",
};

// The operand is a unary expression, so a postfix suffix and arithmetic
// around the whole sizeof both belong to it.
static unsigned long element_size = sizeof words[0];
static unsigned long member_size = sizeof points[0].x;
static unsigned long trailing_term = sizeof bytes - 1;
static unsigned long leading_term = 1 + sizeof bytes;
static unsigned long element_count = sizeof words / sizeof words[0];
static unsigned long dereferenced = sizeof *points;
static unsigned long scalar = sizeof byte_size;

// A static local takes the same path as a file-scope object.
static unsigned long from_static_local(void)
{
    static unsigned long local_size = sizeof bytes;
    return local_size;
}

int main(void)
{
    if (byte_size != 10 || word_size != 7 * sizeof(int))
    {
        return 1;
    }
    if (utf8.map != (const void *)words || utf8.map_size != 7 * sizeof(int) || utf8.name[0] != 'C')
    {
        return 2;
    }
    if (element_size != sizeof(int) || member_size != sizeof(long))
    {
        return 3;
    }
    if (trailing_term != 9 || leading_term != 11)
    {
        return 4;
    }
    if (element_count != 7)
    {
        return 5;
    }
    if (dereferenced != sizeof(struct Point) || scalar != sizeof(unsigned long))
    {
        return 6;
    }
    if (from_static_local() != 10)
    {
        return 7;
    }
    // The same expressions evaluated where they always worked, so a fold that
    // silently guessed a width disagrees with itself here.
    if (byte_size != sizeof bytes || word_size != sizeof words || element_count != sizeof words / sizeof words[0])
    {
        return 8;
    }
    // An array whose bound comes from an unparenthesized sizeof is that long.
    static char mirror[sizeof bytes];
    mirror[9] = 3;
    if (sizeof mirror != 10 || mirror[9] != 3)
    {
        return 9;
    }
    return 0;
}
