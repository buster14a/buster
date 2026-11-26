// Ignored warnings must leave no hint/range on the next emitted diagnostic (#2376).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wparentheses"
int ignored_assignment(int x, int y)
{
    if (x = y) x += 1;
    x + y;
    return x;
}
#pragma clang diagnostic pop
int visible_assignment(int x, int y)
{
    if (x = y) x += 1;
    return x;
}
