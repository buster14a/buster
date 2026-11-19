// Generate N separate C inputs with a data relocation to the next unit and a callable function per unit.
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    const unsigned count = argc == 3 ? (unsigned)strtoul(argv[1], NULL, 10) : 0;
    if (count != 8 && count != 32 && count != 128)
        return 1;
    for (unsigned u = 0; u < count; ++u)
    {
        char path[512];
        if (snprintf(path, sizeof(path), "%s/unit-%03u.c", argv[2], u) >= (int)sizeof(path))
            return 1;
        FILE* file = fopen(path, "w");
        if (!file)
            return 1;
        const unsigned next = (u + 1) % count;
        fprintf(file, "extern int data_%u;\nint data_%u = %u;\nstatic int *p = &data_%u;\nint get_%u(void) { return *p; }\n", next, u, u + 1, next, u);
        if (u == 0)
        {
            fputs("extern int printf(const char *, ...);\n", file);
            for (unsigned i = 1; i < count; ++i)
                fprintf(file, "extern int get_%u(void);\n", i);
            fputs("int main(void) { int sum = 0;\n", file);
            for (unsigned i = 0; i < count; ++i)
                fprintf(file, "sum += get_%u();\n", i);
            fprintf(file, "printf(\"units %u sum %%d\\n\", sum); return sum != %u; }\n", count, count * (count + 1) / 2);
        }
        if (fclose(file))
            return 1;
    }
    return 0;
}
