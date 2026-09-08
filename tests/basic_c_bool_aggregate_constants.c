// A boolean conversion tests the whole value, before narrowing to one bit.
enum { even = 4 };
struct flags { _Bool a, b, c, d, e, f, g, h; };
struct nested { int tag; struct flags bits; };

static struct flags global = {2, 256, 1, -2, 2147483648u, '\2', even, 0};
static _Bool array[] = {2, 256, 1, -2, 2147483648u, '\2', even, 0};
static struct nested designated = {.tag = 7, .bits = {.a = 2, .b = -2, .h = 256}};

static int check(struct flags *p)
{
    return p->a == 1 && p->b == 1 && p->c == 1 && p->d == 1 &&
           p->e == 1 && p->f == 1 && p->g == 1 && p->h == 0;
}

int main(void)
{
    struct flags local = {2, 256, 1, -2, 2147483648u, '\2', even, 0};
    _Bool automatic[] = {2, 256, 1, -2, 2147483648u, '\2', even, 0};
    struct nested nested = {.tag = 7, .bits = {.a = 2, .b = -2, .h = 256}};
    int result = !check(&global) || !check(&local);
    for (int i = 0; i < 8; i += 1)
    {
        if (array[i] != (i != 7) || automatic[i] != (i != 7)) result = 2;
    }
    if (designated.tag != 7 || !designated.bits.a || !designated.bits.b || !designated.bits.h || designated.bits.c ||
        nested.tag != 7 || !nested.bits.a || !nested.bits.b || !nested.bits.h || nested.bits.c) result = 3;
    return result;
}
