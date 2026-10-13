#define BUILD_ASSERT_OR_ZERO(cond) (sizeof(char [1 - 2*!(cond)]) - 1)
#define BARF_UNLESS_AN_ARRAY(arr) BUILD_ASSERT_OR_ZERO(!__builtin_types_compatible_p(__typeof__(arr), __typeof__(&(arr)[0])))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]) + BARF_UNLESS_AN_ARRAY(x))
static const char magic[] = {'B','I','T','M'};
struct Header { char bytes[ARRAY_SIZE(magic)]; };
_Static_assert(ARRAY_SIZE(magic) == 4, "array count");
const int count = ARRAY_SIZE(magic);
int main(void) { char p[2]; char q[ARRAY_SIZE(p)] = {0}; return count != 4 || sizeof(struct Header) != 4 || sizeof(q) != 2 || q[0] != 0; }
