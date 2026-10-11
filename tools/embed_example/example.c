#include <buster/lib/compiler/embed/embed.h>

#include <stdio.h>
#include <string.h>

static long host_scale = 12;

static long host_add(long left, long right)
{
    return left + right;
}

int main(void)
{
    int success = 1;
    int exit_code = 0;
    BusterEmbedCompiler* compiler = NULL;
    BusterEmbedStatus status = buster_embed_compiler_create(&compiler);
    if (status == BUSTER_EMBED_STATUS_OK)
    {
        char const source[] =
            "extern long host_add(long, long); extern long host_scale; "
            "static long* const host_scale_ref = &host_scale; "
            "long run(void) { return host_add(*host_scale_ref, 5); }\n";
        BusterEmbedCompileOptions options = {.source_name = {"embed_example.c", 15}};
        BusterEmbedModule* module = NULL;
        status = buster_embed_compile(compiler, (BusterEmbedString){source, sizeof(source) - 1}, &options, &module);
        BusterEmbedImport imports[] = {
            {{"host_add", 8}, NULL, BUSTER_EMBED_SYMBOL_FUNCTION},
            {{"host_scale", 10}, &host_scale, BUSTER_EMBED_SYMBOL_DATA},
        };
        long (*host_add_function)(long, long) = host_add;
        if (sizeof(host_add_function) == sizeof(imports[0].address))
        {
            memcpy(&imports[0].address, &host_add_function, sizeof(imports[0].address));
        }
        else
        {
            success = 0;
        }
        if (status != BUSTER_EMBED_STATUS_OK || buster_embed_module_load(module, NULL, 0) != BUSTER_EMBED_STATUS_UNRESOLVED_IMPORT ||
            buster_embed_module_load(module, imports, 2) != BUSTER_EMBED_STATUS_OK)
        {
            success = 0;
        }
        else
        {
            void* address = NULL;
            long (*run)(void) = NULL;
            if (buster_embed_module_symbol(module, (BusterEmbedString){"run", 3}, &address) == BUSTER_EMBED_STATUS_OK &&
                sizeof(run) == sizeof(address))
            {
                memcpy(&run, &address, sizeof(run));
            }
            if (!run || run() != 17)
            {
                success = 0;
            }
        }
        buster_embed_module_release(module);

        BusterEmbedModule* invalid = NULL;
        status = buster_embed_compile(compiler, (BusterEmbedString){"int", 3}, &options, &invalid);
        if (status != BUSTER_EMBED_STATUS_COMPILE_FAILED || !invalid || !buster_embed_module_diagnostic_count(invalid) ||
            !buster_embed_module_diagnostic(invalid, 0).rendered.pointer)
        {
            success = 0;
        }
        else
        {
            BusterEmbedDiagnostic diagnostic = buster_embed_module_diagnostic(invalid, 0);
            fwrite(diagnostic.rendered.pointer, 1, diagnostic.rendered.length, stdout);
            putchar('\n');
        }
        buster_embed_module_release(invalid);
        if (buster_embed_compiler_destroy(compiler) != BUSTER_EMBED_STATUS_OK)
        {
            success = 0;
        }
    }
    else
    {
        success = 0;
    }
    if (!success)
    {
        printf("EMBED_EXAMPLE_RESULT failed\n");
        exit_code = 1;
    }
    else
    {
        printf("EMBED_EXAMPLE_RESULT ok\n");
    }
    return exit_code;
}
