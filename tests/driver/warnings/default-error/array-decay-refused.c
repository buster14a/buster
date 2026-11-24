struct S { int m[3]; };
const char fixed[2];
long grid[2][3];
void take(int *values, char *text, int number, int (*rows)[3]);
int *pointer(struct S *p)
{
    int *i = "x";
    char *q = fixed;
    char *l = (const char[]){ 0 };
    int x = "s";
    int *b[2] = { "y", p->m };
    int y[1] = { "z" };
    int (*r)[3] = grid;
    i = "x";
    q = fixed;
    x = "s";
    take("x", q, "s", grid);
    (void)i; (void)q; (void)l; (void)x; (void)b; (void)y; (void)r;
    return "x";
}
char *discards(void) { return fixed; }
int number(void) { return "s"; }
