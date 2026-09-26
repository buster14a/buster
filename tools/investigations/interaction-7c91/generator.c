/* Isolated valid-C generator: unrelated aggregate count x dependency order/depth.
 * No compiler implementation or build defaults are changed by this program. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_count(char const* text, unsigned maximum, unsigned* result)
{
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    int valid = text[0] && text[0] != '-' && !errno && end && !*end && value <= maximum;
    if (valid)
    {
        *result = (unsigned)value;
    }
    return valid;
}

int main(int argc, char** argv)
{
    unsigned unrelated = 0;
    unsigned depth = 0;
    int status = 2;
    if (argc == 4 && parse_count(argv[1], 4096, &unrelated) &&
        parse_count(argv[2], 512, &depth) && depth &&
        (!strcmp(argv[3], "root") || !strcmp(argv[3], "leaf")))
    {
        for (unsigned i = 0; i < unrelated; i += 1)
        {
            printf("struct Noise%04u { int payload; };\n", i);
        }
        for (unsigned i = 0; i < depth; i += 1)
        {
            unsigned tag = !strcmp(argv[3], "root") ? i : depth - 1 - i;
            printf("struct Chain%04u;\n", tag);
        }
        for (unsigned i = depth; i; i -= 1)
        {
            unsigned tag = i - 1;
            if (i == depth)
            {
                printf("struct Chain%04u { int payload; };\n", tag);
            }
            else
            {
                printf("struct Chain%04u { struct Chain%04u child; };\n", tag, tag + 1);
            }
        }
        puts("int interaction_probe(void) { return (int)sizeof(struct Chain0000); }");
        status = ferror(stdout) ? 1 : 0;
    }
    else
    {
        fputs("usage: generator UNRELATED[0..4096] DEPTH[1..512] root|leaf\n", stderr);
    }
    return status;
}
