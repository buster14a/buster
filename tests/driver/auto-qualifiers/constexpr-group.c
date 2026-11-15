constexpr int first = 1, second = first + 1, third = second + 1;
auto a = 3, b = 4, c = 5;
_Static_assert(first == 1 && second == 2 && third == 3, "constexpr group");

int use(void)
{
    constexpr auto x = 6, y = x + 1;
    return a + b + c + x + y;
}
