// A braced string literal is the contents of a character array but one
// element of an array of pointers.  SQLite lists the shared-library suffixes
// to try as `static const char *azEndings[] = { "so" };`, which sizes the
// array at one element rather than at the length of the string.  The 128-bit
// integer spellings below are the names Clang and GCC predefine for the
// keyword; SQLite's decimal extension uses them directly.
static const char *endings[] = {"so"};
static const char *many[] = {"a", "bb", "ccc"};
static char letters[] = {"abc"};
static char plain[] = "wxyz";
static const char *const qualified[] = {"one"};

static unsigned long string_length(const char *text)
{
    unsigned long length = 0;
    while (text[length]) length += 1;
    return length;
}

int main(void)
{
    __int128_t wide = 0;
    __uint128_t unsigned_wide = 0;
    unsigned long long low;

    if (sizeof endings / sizeof endings[0] != 1) return 1;
    if (string_length(endings[0]) != 2 || endings[0][0] != 's') return 2;
    if (sizeof many / sizeof many[0] != 3) return 3;
    if (string_length(many[2]) != 3) return 4;
    if (sizeof letters != 4 || letters[2] != 'c') return 5;
    if (sizeof plain != 5) return 6;
    if (sizeof qualified / sizeof qualified[0] != 1) return 7;

    wide = (__int128_t)1 << 100;
    unsigned_wide = (__uint128_t)wide + 1;
    low = (unsigned long long)(unsigned_wide >> 64);
    if (sizeof(__int128_t) != 16 || sizeof(__uint128_t) != 16) return 8;
    if (low != (1ull << 36)) return 9;
    if ((unsigned long long)(unsigned_wide & 0xffffffffffffffffull) != 1ull) return 10;
    return 0;
}
