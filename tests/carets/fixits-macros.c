// Whole expansions can be wrapped/replaced; operators belonging to a macro body cannot (#2376).
#define BODY(x,y) x = y
#define ARGUMENT(x) x
#define NAME countr
int counter;
int macro_fixes(int x, int y)
{
    if (BODY(x, y)) x += 1;
    if (ARGUMENT(x = y)) x += 2;
    return ARGUMENT(countr);
}
int object_typo(void) { return NAME; }

// A before-token hint inside a macro rejects the group; an unusable after-token location omits just that hint.
#define START if (x =
int partial_begin(int x, int y) { START y) return 1; return 0; }
#define END = y)
int partial_end(int x, int y) { if (x END return 1; return 0; }
