// Fix-its owned by each note: two insertions, replacements and pure removals (#2376).
int condition_fixes(int x, int y)
{
    if (x = y) x += 1;
    x == y;
    x != y;
    if ((x == y)) x += 2;
    return x;
}
