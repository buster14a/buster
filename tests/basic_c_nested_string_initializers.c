// Nested braces select the iterative initializer walker. A string initializes
// its entire character-array subobject, including zero padding where it fits.
struct point { int x, y; };
struct config { struct point origin; char name[4]; int flags; };
static struct config global[] = {
    {.origin = {1, 2}, .name = "ab", .flags = 3},
    {{4, 5}, "abc", 6},
    {{7, 8}, "abcd", 9},
};

static int check(struct config *c, int index)
{
    int result = c->origin.x == index * 3 + 1 && c->origin.y == index * 3 + 2 && c->flags == index * 3 + 3 &&
                 c->name[0] == 'a' && c->name[1] == 'b' && c->name[2] == (index == 0 ? 0 : 'c') &&
                 c->name[3] == (index == 2 ? 'd' : 0);
    return result;
}

int main(void)
{
    struct config designated = {.origin = {1, 2}, .name = "a" "b", .flags = 3};
    struct config positional = {{4, 5}, "abc", 6};
    struct config exact = {{7, 8}, "abcd", 9};
    struct config array[] = {
        {.origin = {1, 2}, .name = "ab", .flags = 3},
        {{4, 5}, "abc", 6},
        {{7, 8}, "abcd", 9},
    };
    int result = !check(&designated, 0) || !check(&positional, 1) || !check(&exact, 2);
    for (int i = 0; i < 3; i += 1)
    {
        if (!check(&global[i], i) || !check(&array[i], i)) result = 2;
    }
    return result;
}
