#include <stdbool.h>

static void write_narrow_values(bool *flag, signed char *small, short *medium)
{
    *flag = true;
    *small = -1;
    *medium = -2;
}

int main(void)
{
    bool flag;
    signed char small;
    short medium;
    write_narrow_values(&flag, &small, &medium);
    // Read the _Bool directly as a branch and compare it at its declared
    // width.  A qword frame load that leaks stale upper bytes can therefore
    // take the wrong branch even when its low byte is non-zero.
    if (!flag) return 1;
    if (flag != true) return 2;
    return small == (signed char)-1 && medium == (short)-2 ? 0 : 3;
}
