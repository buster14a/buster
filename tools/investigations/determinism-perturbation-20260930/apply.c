/* Exact-marker overlay installer. Run only in a disposable pinned worktree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int replace_once(char const* path, char const* needle, char const* replacement)
{
    int result = 1;
    FILE* input = fopen(path, "rb");
    char* data = 0;
    long size = -1;
    if (input && fseek(input, 0, SEEK_END) == 0)
    {
        size = ftell(input);
    }
    if (size >= 0 && size < 8000000 && fseek(input, 0, SEEK_SET) == 0)
    {
        data = malloc((size_t)size + 1);
    }
    if (data && fread(data, 1, (size_t)size, input) == (size_t)size)
    {
        data[size] = 0;
        char* match = strstr(data, needle);
        if (match && !strstr(match + strlen(needle), needle))
        {
            size_t prefix = (size_t)(match - data);
            size_t suffix = (size_t)size - prefix - strlen(needle);
            FILE* output = fopen(path, "wb");
            if (output)
            {
                int okay = fwrite(data, 1, prefix, output) == prefix &&
                    fwrite(replacement, 1, strlen(replacement), output) == strlen(replacement) &&
                    fwrite(match + strlen(needle), 1, suffix, output) == suffix;
                result = (fclose(output) == 0 && okay) ? 0 : 1;
            }
        }
    }
    if (input)
    {
        fclose(input);
    }
    free(data);
    if (result)
    {
        fprintf(stderr, "HARNESS_ERROR unique marker/read/write failure in %s\n", path);
    }
    return result;
}

int main(void)
{
    char const* driver = "src/buster/lib/compiler/driver/driver.c";
    char const* ide = "src/buster/apps/ide/ide.c";
    int result = replace_once(driver,
        "#include <buster/lib/compiler/driver/archive.c>",
        "#include <buster/lib/compiler/driver/archive.c>\n#include \"../../../../../tools/investigations/determinism-perturbation-20260930/hooks.h\"");
    if (!result)
    {
        result = replace_once(driver,
            "            unit->result = compiler_driver_execute_c_single(unit->arena, single, true, &unit->warnings);",
            "            det_unit_begin(batch->first_input + index, unit->arena);\n"
            "            if (det_active && det_tracing)\n"
            "            {\n"
            "                single.bootstrap_trace_prefix = string_format(unit->arena, S8(\"det-stage/tu-{u64}\"), (u64)batch->first_input + index);\n"
            "            }\n"
            "            unit->result = compiler_driver_execute_c_single(unit->arena, single, true, &unit->warnings);\n"
            "            det_unit_end(batch->first_input + index);");
    }
    if (!result)
    {
        result = replace_once(ide,
            "BUSTER_GLOBAL_LOCAL ProcessResult run_c_compiler(void)\n{",
            "BUSTER_GLOBAL_LOCAL ProcessResult run_c_compiler(void)\n{\n    det_prepare();");
    }
    if (!result)
    {
        result = replace_once(ide,
            "    CompilerDriverResult compile = compiler_driver_execute_invocation(arena, invocation);",
            "    CompilerDriverResult compile = compiler_driver_execute_invocation(arena, invocation);\n    det_finish(&compile);");
    }
    return result;
}
