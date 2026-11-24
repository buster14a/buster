#include <stdio.h>
#include <string.h>
int main(void)
{
    FILE* file = fopen("/proc/self/status", "r");
    char line[256];
    int found = 0;
    while (file != 0 && fgets(line, sizeof(line), file) != 0)
    {
        if (strncmp(line, "THP_enabled:", 12) == 0)
        {
            fputs(line, stdout);
            found = 1;
        }
    }
    if (file != 0)
    {
        fclose(file);
    }
    return found ? 0 : 1;
}
