// C11 6.7.9p14, p20: a string literal may initialize a row of a `char[N][M]`
// member reached through elided braces. A string is not itself an aggregate
// value for elision: it initializes an array of characters whole and opens
// the braces of any other aggregate (issue #2525). Each shape follows a
// scalar, an array or a nested-struct predecessor, in automatic and static
// storage, so the shape check, the store walker and the constant image all
// see it.

struct Scalar { int n; char t[2][3]; };
struct Array { int m[2][2]; char s[2][3]; };
struct Nested { struct { int a, b; } p; char s[2][3]; };
struct Trailing { int n; char t[2][3]; int z; };
struct Rows { char t[2][3]; };

static struct Scalar file_scalar = { 1, "ab", "cd" };
static struct Rows file_rows[] = { "ab", "cd", "ef" };

static int check_rows(char (*t)[3], char const* first, char const* second)
{
    return t[0][0] == first[0] && t[0][1] == first[1] && t[0][2] == first[2] &&
           t[1][0] == second[0] && t[1][1] == second[1] && t[1][2] == second[2];
}

static int check_array(struct Array* k)
{
    return k->m[0][0] == 1 && k->m[0][1] == 2 && k->m[1][0] == 3 && k->m[1][1] == 4 && check_rows(k->s, "ab", "cd");
}

static int check_nested(struct Nested* q)
{
    return q->p.a == 7 && q->p.b == 8 && check_rows(q->s, "ab", "cd");
}

int main(void)
{
    struct Scalar scalar = { 1, "ab", "cd" };
    static struct Scalar static_scalar = { 1, "ab", "cd" };
    struct Array array = { 1, 2, 3, 4, "ab", "cd" };
    static struct Array static_array = { 1, 2, 3, 4, "ab", "cd" };
    struct Nested nested = { 7, 8, "ab", "cd" };
    static struct Nested static_nested = { 7, 8, "ab", "cd" };
    struct Trailing trailing = { 1, "ab", "c", 9 };
    static struct Trailing static_trailing = { 1, "ab", "c", 9 };
    struct Trailing short_row = { 1, "ab", 9 };
    struct Rows rows[] = { "ab", "cd", "ef" };
    int result = 0;
    result |= !(scalar.n == 1 && check_rows(scalar.t, "ab", "cd")) << 0;
    result |= !(static_scalar.n == 1 && check_rows(static_scalar.t, "ab", "cd")) << 1;
    result |= !(file_scalar.n == 1 && check_rows(file_scalar.t, "ab", "cd")) << 2;
    result |= !(check_array(&array) && check_array(&static_array)) << 3;
    result |= !(check_nested(&nested) && check_nested(&static_nested)) << 4;
    result |= !(trailing.n == 1 && check_rows(trailing.t, "ab", "c\0") && trailing.z == 9) << 5;
    result |= !(static_trailing.n == 1 && check_rows(static_trailing.t, "ab", "c\0") && static_trailing.z == 9) << 6;
    result |= !(short_row.n == 1 && check_rows(short_row.t, "ab", "\11\0\0") && short_row.z == 0) << 7;
    result |= !(sizeof rows / sizeof rows[0] == 2 && check_rows(rows[0].t, "ab", "cd") && check_rows(rows[1].t, "ef", "\0\0\0")) << 8;
    result |= !(sizeof file_rows / sizeof file_rows[0] == 2 && check_rows(file_rows[0].t, "ab", "cd") && check_rows(file_rows[1].t, "ef", "\0\0\0")) << 9;
    return result;
}
