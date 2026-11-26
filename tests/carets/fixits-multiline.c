// Each note owns its lines: insertion-only second lines and removals across lines (#2376).
int multiline_fixes(int x, int y)
{
    if (x =
        y + 1) x += 1;
    if ((x
         == y)) x += 2;
    x ==
        y;
    return x;
}
