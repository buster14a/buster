#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Keep object keys in source order: yyjson's writer preserves that order, so
// the Buster/Clang comparison below is byte-for-byte rather than a semantic
// comparison that could hide serializer drift.
int main(void)
{
    static const char *corpus[] = {
        "{\"z\":3,\"a\":[true,null,1.25],\"nested\":{\"unicode\":\"\\u20ac\"}}",
        "[0,-1,1.25,1e-9,1e100,-2.5e+7]",
        "\"escaped\\/quote\\nline\\u00e9\"",
        "{\"array\":[0,1,2,3],\"object\":{\"first\":true,\"last\":false}}",
        "{\"broken\":]",
    };
    for (size_t index = 0; index < sizeof(corpus) / sizeof(corpus[0]); index += 1)
    {
        const char *source = corpus[index];
        yyjson_doc *doc = yyjson_read(source, strlen(source), 0);
        printf("case%zu parse=%s", index, doc ? "ok" : "fail");
        if (doc)
        {
            size_t output_length = 0;
            char *output = yyjson_write(doc, 0, &output_length);
            if (!output)
            {
                yyjson_doc_free(doc);
                return 2;
            }
            printf(" bytes=%zu ", output_length);
            fwrite(output, 1, output_length, stdout);
            free(output);
            yyjson_doc_free(doc);
        }
        putchar('\n');
    }
    fflush(stdout);
    return 0;
}
