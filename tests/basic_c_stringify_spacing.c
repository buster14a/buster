// The white space a stringified macro argument reproduces (C11 6.10.3.2p2):
// each run of white space between the argument's preprocessing tokens
// becomes one space, there is none before the first token or after the last,
// and tokens that were written adjacent stay adjacent. The last rule is the
// one a naive "join with spaces" loses, and it is visible in real code — LZ4
// builds its version string as
// LZ4_EXPAND_AND_QUOTE(LZ4_VERSION_MAJOR.LZ4_VERSION_MINOR.LZ4_VERSION_RELEASE),
// which must read "1.10.0" and not "1 . 10 . 0". Every expectation here is
// what Clang and GCC print for the same source.
#define QUOTE(text) #text
#define EXPAND_AND_QUOTE(text) QUOTE(text)

#define VERSION_MAJOR 1
#define VERSION_MINOR 10
#define VERSION_RELEASE 0
#define VERSION VERSION_MAJOR.VERSION_MINOR.VERSION_RELEASE
#define SPACED VERSION_MAJOR + VERSION_MINOR
#define TIGHT VERSION_MAJOR+VERSION_MINOR
#define PASTE(left, right) left##right
#define NOTHING
#define TWO_ARGUMENTS(left, right) left right

static int text_equal(char const* left, char const* right)
{
    unsigned index = 0;
    while (left[index] && left[index] == right[index])
    {
        index += 1;
    }
    return left[index] == right[index];
}

int main(void)
{
    // Adjacency in the argument as written.
    if (!text_equal(QUOTE(VERSION_MAJOR.VERSION_MINOR.VERSION_RELEASE), "VERSION_MAJOR.VERSION_MINOR.VERSION_RELEASE"))
        return 1;
    if (sizeof QUOTE(a.b) != 4)
        return 2;
    // Adjacency through one expansion, and through a definition that spaces
    // its own tokens: the argument's spacing is the spacing it was defined
    // with, not one space per token boundary.
    if (!text_equal(EXPAND_AND_QUOTE(VERSION), "1.10.0"))
        return 3;
    if (!text_equal(EXPAND_AND_QUOTE(SPACED), "1 + 10"))
        return 4;
    if (!text_equal(EXPAND_AND_QUOTE(TIGHT), "1+10"))
        return 5;
    // Leading and trailing white space is dropped, interior runs collapse to
    // one space, and a comment counts as white space.
    if (!text_equal(QUOTE(  a   b  ), "a b"))
        return 6;
    if (!text_equal(QUOTE(a /* comment */ b), "a b"))
        return 7;
    if (!text_equal(QUOTE(a
                          b),
                    "a b"))
        return 8;
    // A line splice is removed before the tokens are formed, so the two
    // halves are one token with nothing between them.
    if (!text_equal(QUOTE(a\
b),
                    "ab"))
        return 9;
    // Punctuators are tokens like any other: nothing is inserted around them.
    if (!text_equal(QUOTE(f(1, 2)), "f(1, 2)"))
        return 10;
    if (!text_equal(QUOTE(-1 -2 - 3), "-1 -2 - 3"))
        return 11;
    // The argument of # is not expanded, so a paste survives as its spelling;
    // one level of indirection expands it first and the joined token carries
    // the spacing of its left half.
    if (!text_equal(QUOTE(PASTE(x, y)), "PASTE(x, y)"))
        return 12;
    if (!text_equal(EXPAND_AND_QUOTE(PASTE(x, y)), "xy"))
        return 13;
    // A parameter's spacing belongs to the first token of the argument that
    // replaces it; the argument's own interior spacing is preserved.
    if (!text_equal(EXPAND_AND_QUOTE(TWO_ARGUMENTS(VERSION_MAJOR, VERSION_MINOR)), "1 10"))
        return 14;
    // A macro that expands to nothing leaves the white space around it.
    if (!text_equal(QUOTE(x NOTHING y), "x NOTHING y"))
        return 15;
    if (!text_equal(EXPAND_AND_QUOTE(x NOTHING y), "x y"))
        return 16;
    if (!text_equal(EXPAND_AND_QUOTE(NOTHING), ""))
        return 17;
    // An empty argument stringifies to the empty string.
    if (!text_equal(QUOTE(), ""))
        return 18;
    // Quotes and backslashes inside the argument are escaped, and the
    // spelling of a string literal argument keeps its own spaces.
    if (!text_equal(QUOTE("a b" 'c'), "\"a b\" 'c'"))
        return 19;
    if (!text_equal(QUOTE("a\\b"), "\"a\\\\b\""))
        return 20;
    return 0;
}
