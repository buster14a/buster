#include "cJSON.h"

#include <stdio.h>

int main(void)
{
    const char *source = "{\"z\":3,\"a\":[true,null,1.25]}";
    cJSON *value = cJSON_Parse(source);
    if (!value)
    {
        return 1;
    }

    char *printed = cJSON_PrintUnformatted(value);
    if (!printed)
    {
        cJSON_Delete(value);
        return 2;
    }

    puts(printed);
    // Buster's freestanding entry stub exits with a direct syscall after
    // main; flush explicitly so the hosted end-to-end probe observes stdout
    // without relying on libc's atexit machinery.
    fflush(0);
    cJSON_free(printed);
    cJSON_Delete(value);
    return 0;
}
