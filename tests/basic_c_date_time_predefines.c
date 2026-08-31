// C11 6.10.8.1 mandates __DATE__ ("Mmm dd yyyy") and __TIME__ ("hh:mm:ss").
// CPython's getbuildinfo falls back to "xx/xx/xx" when __DATE__ is missing,
// and platform.py then rejects sys.version -- the date field only admits
// word characters and spaces.  The shape is asserted, not the value: the
// fixed epoch keeps builds reproducible.
static unsigned long text_length(const char* text)
{
    unsigned long length = 0;
    while (text[length])
    {
        length += 1;
    }
    return length;
}

int main(void)
{
    const char* date = __DATE__;
    const char* clock = __TIME__;
    if (text_length(date) != 11 || date[3] != ' ' || date[6] != ' ')
    {
        return 1;
    }
    if (text_length(clock) != 8 || clock[2] != ':' || clock[5] != ':')
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
    return 0;
}
