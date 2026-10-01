/* Disposable Linux-hosted target-policy census. Sole writer: this research
 * branch. It wraps two existing target-policy functions only in the runner's
 * checkout, restores them before compiling inputs, and never times a compiler.
 * Entry: main; instrument wraps exact unique source anchors; sample retains
 * commands, statuses, diagnostics, outputs, and outcome histograms.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define EVIDENCE "policy-census-evidence"
#define TARGET_SOURCE "src/buster/lib/target.c"
#define IR_SOURCE "src/buster/lib/compiler/ir/ir.c"
#define BASE EVIDENCE "/ide-baseline"
#define PROBE EVIDENCE "/ide-probe"
#define MAX_SOURCE (16L * 1024L * 1024L)

static int command(char const* text)
{
    FILE* log = fopen(EVIDENCE "/commands.txt", "a");
    int status = -1;
    if (log)
    {
        fprintf(log, "$ %s\n", text);
        fflush(log);
        status = system(text);
        fprintf(log, "system_status=%d\n", status);
        fclose(log);
    }
    return status;
}

static char* read_text(char const* path)
{
    FILE* file = fopen(path, "rb");
    char* result = NULL;
    if (file)
    {
        long size = -1;
        if (!fseek(file, 0, SEEK_END))
        {
            size = ftell(file);
        }
        if (size >= 0 && size <= MAX_SOURCE && !fseek(file, 0, SEEK_SET))
        {
            result = malloc((size_t)size + 1);
            if (result)
            {
                if (fread(result, 1, (size_t)size, file) != (size_t)size)
                {
                    free(result);
                    result = NULL;
                }
                else
                {
                    result[size] = 0;
                }
            }
        }
        fclose(file);
    }
    return result;
}

static int write_text(char const* path, char const* text)
{
    FILE* file = fopen(path, "wb");
    int okay = 0;
    if (file)
    {
        size_t size = strlen(text);
        okay = fwrite(text, 1, size, file) == size;
        okay = fclose(file) == 0 && okay;
    }
    return okay;
}

static int copy_file(char const* source, char const* destination)
{
    FILE* input = fopen(source, "rb");
    FILE* output = input ? fopen(destination, "wb") : NULL;
    int okay = input && output;
    if (okay)
    {
        unsigned char bytes[65536];
        size_t count;
        while ((count = fread(bytes, 1, sizeof(bytes), input)) != 0 && okay)
        {
            okay = fwrite(bytes, 1, count, output) == count;
        }
        okay = !ferror(input) && okay;
    }
    if (input) fclose(input);
    if (output) okay = fclose(output) == 0 && okay;
    return okay;
}

static int equal_files(char const* left, char const* right, int require_bytes)
{
    FILE* a = fopen(left, "rb");
    FILE* b = fopen(right, "rb");
    int okay = a && b;
    size_t total = 0;
    size_t count = 1;
    while (okay && count)
    {
        unsigned char x[65536], y[65536];
        count = fread(x, 1, sizeof(x), a);
        size_t other = fread(y, 1, sizeof(y), b);
        okay = count == other && !memcmp(x, y, count);
        total += count;
    }
    if (a) { okay = !ferror(a) && okay; fclose(a); }
    if (b) { okay = !ferror(b) && okay; fclose(b); }
    okay = okay && (!require_bytes || total != 0);
    return okay;
}

/* Repository style puts a function's closing brace at column zero. Both
 * audited helpers are nonrecursive and have this exact signature. Duplicate
 * anchors, missing boundaries, and oversized inputs fail before a write.
 */
static int instrument(char const* path, char const* type, char const* name, char const* trace)
{
    char signature[256];
    snprintf(signature, sizeof(signature), "%s %s(Target target)\n{", type, name);
    char* source = read_text(path);
    char* begin = source ? strstr(source, signature) : NULL;
    char* end = begin ? strstr(begin, "\n}\n") : NULL;
    int okay = begin && end && !strstr(begin + 1, signature);
    if (okay)
    {
        char* brace = strchr(begin, '{');
        end += 3;
        size_t capacity = strlen(source) + strlen(trace) + 2048;
        char* changed = malloc(capacity);
        okay = changed != NULL;
        if (okay)
        {
            int length = snprintf(changed, capacity,
                "#include <stdio.h> /* disposable policy census */\n"
                "%.*sBUSTER_GLOBAL_LOCAL %s policy_probe_original_%s(Target target)\n{%.*s\n"
                "%s %s(Target target)\n{\n"
                "    %s result = policy_probe_original_%s(target);\n"
                "%s"
                "    return result;\n}\n%s",
                (int)(begin - source), source, type, name,
                (int)(end - brace - 1), brace + 1, type, name, type, name, trace, end);
            okay = length > 0 && (size_t)length < capacity;
            if (okay) okay = write_text(path, changed);
            free(changed);
        }
    }
    if (!okay) fprintf(stderr, "instrumentation failed: %s (%s)\n", path, name);
    free(source);
    return okay;
}

/* Strip only the two private trace prefixes. Keep all original diagnostic
 * bytes, including long lines, verbatim. The census histogram is bounded and
 * fails rather than silently dropping a new outcome.
 */
static int summarize(char const* path, char const* clean_path, FILE* result)
{
    FILE* input = fopen(path, "rb");
    FILE* clean = fopen(clean_path, "wb");
    char keys[128][512];
    unsigned long long counts[128] = {0};
    unsigned used = 0;
    int okay = input && clean;
    int line_start = 1;
    char line[1024];
    while (okay && fgets(line, sizeof(line), input))
    {
        int policy = line_start && (!strncmp(line, "POLICY_LAYOUT ", 14) || !strncmp(line, "POLICY_ABI ", 11));
        if (policy)
        {
            char* value = strstr(line, " value=");
            char key[512];
            int length = value ? snprintf(key, sizeof(key), "%s%s", !strncmp(line, "POLICY_LAYOUT ", 14) ? "layout" : "abi", value) : -1;
            okay = length > 0 && (size_t)length < sizeof(key) && strchr(line, '\n');
            if (okay)
            {
                unsigned index = 0;
                while (index < used && strcmp(keys[index], key)) index += 1;
                if (index == used)
                {
                    okay = used < 128;
                    if (okay) { strcpy(keys[used], key); used += 1; }
                }
                if (okay) counts[index] += 1;
            }
        }
        else
        {
            okay = fputs(line, clean) >= 0;
        }
        line_start = strchr(line, '\n') != NULL;
    }
    if (input) { okay = !ferror(input) && okay; fclose(input); }
    if (clean) okay = fclose(clean) == 0 && okay;
    unsigned long long layout_calls = 0, abi_calls = 0;
    for (unsigned index = 0; index < used; index += 1)
    {
        if (!strncmp(keys[index], "layout", 6)) layout_calls += counts[index];
        else abi_calls += counts[index];
        fprintf(result, "outcome count=%llu %s", counts[index], keys[index]);
    }
    fprintf(result, "layout_calls=%llu abi_calls=%llu combined_distinct_outcomes=%u\n", layout_calls, abi_calls, used);
    return okay;
}

static int sample(char const* label, char const* arguments, int success, FILE* result)
{
    char text[4096], baseline_output[512], probe_output[512];
    char baseline_error[512], probe_error[512], clean_error[512];
    snprintf(baseline_output, sizeof(baseline_output), EVIDENCE "/%s.baseline-output", label);
    snprintf(probe_output, sizeof(probe_output), EVIDENCE "/%s.probe-output", label);
    snprintf(baseline_error, sizeof(baseline_error), EVIDENCE "/%s.baseline.stderr", label);
    snprintf(probe_error, sizeof(probe_error), EVIDENCE "/%s.probe.stderr", label);
    snprintf(clean_error, sizeof(clean_error), EVIDENCE "/%s.probe.clean.stderr", label);
    remove(EVIDENCE "/output");
    remove(EVIDENCE "/metrics");
    snprintf(text, sizeof(text), BASE " cc %s -fsource-metrics=" EVIDENCE "/metrics -o " EVIDENCE "/output > " EVIDENCE "/%s.baseline.stdout 2> %s", arguments, label, baseline_error);
    int baseline_status = command(text);
    int okay = success ? baseline_status == 0 && copy_file(EVIDENCE "/output", baseline_output) : baseline_status > 0;
    char baseline_metrics[512], probe_metrics[512];
    snprintf(baseline_metrics, sizeof(baseline_metrics), EVIDENCE "/%s.baseline.metrics", label);
    snprintf(probe_metrics, sizeof(probe_metrics), EVIDENCE "/%s.probe.metrics", label);
    if (success) okay = copy_file(EVIDENCE "/metrics", baseline_metrics) && okay;
    remove(EVIDENCE "/output");
    remove(EVIDENCE "/metrics");
    snprintf(text, sizeof(text), PROBE " cc %s -fsource-metrics=" EVIDENCE "/metrics -o " EVIDENCE "/output > " EVIDENCE "/%s.probe.stdout 2> %s", arguments, label, probe_error);
    int probe_status = command(text);
    okay = baseline_status == probe_status && okay;
    if (success) okay = copy_file(EVIDENCE "/output", probe_output) && equal_files(baseline_output, probe_output, 1) && okay;
    if (success) okay = copy_file(EVIDENCE "/metrics", probe_metrics) && equal_files(baseline_metrics, probe_metrics, 1) && okay;
    fprintf(result, "\ncase=%s baseline_status=%d probe_status=%d\n", label, baseline_status, probe_status);
    okay = summarize(probe_error, clean_error, result) && okay;
    okay = equal_files(baseline_error, clean_error, 0) && okay;
    fprintf(result, "artifact_metrics_and_diagnostic_parity=%s\n", okay ? "passed" : "failed");
    fflush(result);
    return okay;
}

int main(void)
{
    int okay = mkdir(EVIDENCE, 0700) == 0 || errno == EEXIST;
    FILE* result = okay ? fopen(EVIDENCE "/results.txt", "w") : NULL;
    okay = okay && result;
    if (okay)
    {
        fprintf(result, "scope=hosted correctness and work counts; no timing or performance verdict\n");
        okay = command("git diff --exit-code -- src > " EVIDENCE "/initial-diff.txt") == 0;
        okay = command("{ git rev-parse HEAD 'HEAD^{tree}'; git hash-object " TARGET_SOURCE " " IR_SOURCE "; clang --version; gcc --version; uname -a; } > " EVIDENCE "/host-and-source.txt") == 0 && okay;
        okay = command("clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o " EVIDENCE "/build-driver > " EVIDENCE "/bootstrap.log 2>&1") == 0 && okay;
    }
    if (okay) okay = command(EVIDENCE "/build-driver generate --config Release --cc clang --ci --linker DEFAULT > " EVIDENCE "/generate.log 2>&1") == 0;
    if (okay) okay = command(EVIDENCE "/build-driver build --config Release -t ide > " EVIDENCE "/baseline-build.log 2>&1") == 0;
    if (okay) okay = command("cp build/Release/ide " BASE) == 0;
    int target_saved = okay && copy_file(TARGET_SOURCE, EVIDENCE "/target.c.before");
    int ir_saved = okay && copy_file(IR_SOURCE, EVIDENCE "/ir.c.before");
    okay = okay && target_saved && ir_saved;
    if (okay)
    {
        okay = instrument(TARGET_SOURCE, "TargetDataLayout", "target_data_layout",
            "    fprintf(stderr, \"POLICY_LAYOUT cpu=%u os=%u value=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\\n\",\n"
            "        (unsigned)target.cpu_arch, (unsigned)target.os, result.pointer.size, result.long_integer.size,\n"
            "        result.long_double_type.size, result.long_double_type.bit_width, result.va_list.size,\n"
            "        (unsigned)result.plain_char_is_signed, (unsigned)result.has_128_bit_integer,\n"
            "        result.atomic_max_width, result.atomic_alignment, result.abi_stack_alignment,\n"
            "        result.abi_max_alignment, (unsigned)result.endianness);\n");
    }
    if (okay)
    {
        okay = instrument(IR_SOURCE, "IrAbiConvention", "ir_abi_convention_for_target",
            "    fprintf(stderr, \"POLICY_ABI cpu=%u os=%u value=%u\\n\",\n"
            "        (unsigned)target.cpu_arch, (unsigned)target.os, (unsigned)result);\n");
    }
    if (okay) okay = command("git diff -- " TARGET_SOURCE " " IR_SOURCE " > " EVIDENCE "/instrumentation.patch") == 0;
    if (okay) okay = command(EVIDENCE "/build-driver build --config Release -t ide > " EVIDENCE "/probe-build.log 2>&1") == 0;
    if (okay) okay = command("cp build/Release/ide " PROBE) == 0;
    /* Always restore before any compiler input is read. Baseline and probe
     * compile the same original source, not two differently instrumented TUs.
     */
    if (target_saved) okay = copy_file(EVIDENCE "/target.c.before", TARGET_SOURCE) && okay;
    if (ir_saved) okay = copy_file(EVIDENCE "/ir.c.before", IR_SOURCE) && okay;
    if (target_saved || ir_saved) okay = command("git diff --exit-code -- src > " EVIDENCE "/restored-diff.txt") == 0 && okay;
    if (okay)
    {
        okay = write_text(EVIDENCE "/tiny.c", "unsigned long policy_tiny(void) { return sizeof(long) + sizeof(void *) + sizeof(long double); }\n") &&
               write_text(EVIDENCE "/invalid.c", "int policy_bad(void) { return unknown_policy_identifier; }\n");
    }
    if (okay)
    {
        int samples = 1;
        samples = sample("tiny-linux", "-g0 -std=c11 -c -target x86_64-linux " EVIDENCE "/tiny.c", 1, result) && samples;
        samples = sample("tiny-windows", "-g0 -std=c11 -c -target x86_64-windows " EVIDENCE "/tiny.c", 1, result) && samples;
        samples = sample("tiny-aarch64", "-g0 -std=c11 -c -target aarch64-linux " EVIDENCE "/tiny.c", 1, result) && samples;
        samples = sample("tiny-debug", "-g -std=c11 -c " EVIDENCE "/tiny.c", 1, result) && samples;
        samples = sample("operations", "-g0 -std=c11 -c tests/basic_c_operations.c", 1, result) && samples;
        samples = sample("invalid", "-g0 -std=c11 -c " EVIDENCE "/invalid.c", 0, result) && samples;
        samples = sample("ide-unity", "-Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm", 1, result) && samples;
        okay = samples;
    }
    if (okay)
    {
        okay = command("chmod +x " EVIDENCE "/ide-unity.probe-output") == 0;
        if (okay) okay = command(EVIDENCE "/ide-unity.probe-output cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o " EVIDENCE "/output > " EVIDENCE "/stage2.stdout 2> " EVIDENCE "/stage2.stderr") == 0;
        if (okay) okay = copy_file(EVIDENCE "/output", EVIDENCE "/stage2-output") && equal_files(EVIDENCE "/ide-unity.probe-output", EVIDENCE "/stage2-output", 1);
        fprintf(result, "self_host_binary_fixed_point=%s\n", okay ? "passed" : "failed");
    }
    if (target_saved && ir_saved)
    {
        command("sha256sum " BASE " " PROBE " " TARGET_SOURCE " " IR_SOURCE " > " EVIDENCE "/identities.sha256");
        command("nm -S --size-sort " BASE " > " EVIDENCE "/baseline-symbols.txt");
        command("objdump -d --disassemble=target_data_layout " BASE " > " EVIDENCE "/baseline-layout.asm");
        command("objdump -d --disassemble=ir_abi_convention_for_target " BASE " > " EVIDENCE "/baseline-abi.asm");
        command("size " BASE " " PROBE " > " EVIDENCE "/diagnostic-build-sizes.txt");
    }
    if (result)
    {
        fprintf(result, "\nstatus=%s\nperformance=UNMEASURED\nsame_process_compilation_contrasts=NOT_RUN\n", okay ? "passed" : "failed");
        fclose(result);
        command("cat " EVIDENCE "/results.txt");
    }
    return okay ? 0 : 1;
}
