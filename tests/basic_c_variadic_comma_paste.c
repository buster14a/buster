// GNU's `, ## __VA_ARGS__` comma-deletion idiom, both ways.  With empty
// varargs the paste deletes the comma; with tokens present GNU performs no
// paste at all -- the comma stays and the arguments follow.  A preprocessor
// that ran a real paste there refused every non-empty call ("token paste
// ',##x' does not form one preprocessing token"), which is how CPython's
// Parser/pegen.c stopped compiling: its RAISE_SYNTAX_ERROR_KNOWN_LOCATION
// forwards `##__VA_ARGS__` through a second macro layer, so both the direct
// and the forwarded shape are pinned here.
#include <stdio.h>
#include <string.h>

#define RENDER(buffer, fmt, ...) snprintf(buffer, sizeof buffer, fmt, ##__VA_ARGS__)
#define FORWARD(buffer, fmt, ...) RENDER(buffer, fmt, ##__VA_ARGS__)

int main(void)
{
    char plain[32];
    char direct[32];
    char forwarded[32];
    RENDER(plain, "plain");
    RENDER(direct, "%d-%s", 42, "answer");
    FORWARD(forwarded, "%s:%d", "line", 7);
    if (strcmp(plain, "plain") != 0)
    {
        return 1;
    }
    if (strcmp(direct, "42-answer") != 0)
    {
        return 2;
    }
    if (strcmp(forwarded, "line:7") != 0)
    {
        return 3;
    }
    return 0;
}
