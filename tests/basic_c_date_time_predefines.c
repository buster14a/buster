// C11 6.10.8.1 mandates __DATE__ ("Mmm dd yyyy") and __TIME__ ("hh:mm:ss").
// CPython's getbuildinfo falls back to "xx/xx/xx" when __DATE__ is missing,
// and platform.py then rejects sys.version -- the date field only admits
// word characters and spaces.  The shape is asserted, not the value: the
// fixed epoch keeps builds reproducible.
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char* date = __DATE__;
    const char* clock = __TIME__;
    if (strlen(date) != 11 || date[3] != ' ' || date[6] != ' ')
    {
        return 1;
    }
    if (strlen(clock) != 8 || clock[2] != ':' || clock[5] != ':')
    {
        return 2;
    }
    for (const char* cursor = date; *cursor; cursor += 1)
    {
        if (*cursor == '/')
        {
            return 3;
        }
    }
    printf("date %s time %s\n", date, clock);
    return 0;
}
