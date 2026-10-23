// Original qualification adapter: pass through Clang arguments except Buster's feature-mode flag.
// Disable implicit Clang configuration, matching the frozen predefines invocation.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    int result = 126;
    char** forwarded = calloc((size_t)argc + 2, sizeof(char*));
    if (forwarded != 0)
    {
        int count = 2;
        forwarded[0] = "/usr/bin/clang";
        forwarded[1] = "--no-default-config";
        for (int i = 1; i < argc; i += 1)
        {
            if (strcmp(argv[i], "--clang-features") != 0)
            {
                forwarded[count] = argv[i];
                count += 1;
            }
        }
        execv("/usr/bin/clang", forwarded);
        perror("clang-oracle-adapter: execv");
        free(forwarded);
    }
    else
    {
        perror("clang-oracle-adapter: calloc");
    }
    return result;
}
