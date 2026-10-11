// #3501 workflow scheduling policy; standalone hosted lint control.
// Ownership: ordinary external-project routes only. This bounded text scanner
// admits the canonical owner topology and rejects legacy/reusable fan-out.
// It is not a YAML interpreter; owner syntax changes require updating controls.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define EXTERNAL_POLICY_FILE_LIMIT (256u * 1024u)
static const char* projects[] = {"cjson", "zlib", "stb", "lua", "quickjs", "sqlite", "git", "rad"};
static const char* legacy[] = {"raddebugger-compatibility.yml", "capability-portfolio.yml", "throughput-real-source.yml", "git-qualification.yml"};
static unsigned count(const char* text, const char* needle)
{
    unsigned result = 0;
    size_t length = strlen(needle);
    const char* cursor = text;
    while ((cursor = strstr(cursor, needle)) != NULL)
    {
        result += 1;
        cursor += length;
    }
    return result;
}

static int owner_valid(const char* text)
{
    const char* prefix = "on:\n  push:\n    branches: [main]\npermissions:\n  contents: read\n";
    const char* guard = "if: ${{ github.event_name == 'push' && github.ref == 'refs/heads/main' }}";
    int valid = count(text, prefix) == 1 && count(text, "\non:") == 1 &&
        count(text, "\n  linux:\n") == 1 && count(text, "\n  macos:\n") == 1 && count(text, "\n  windows:\n") == 1 &&
        count(text, "    runs-on: ") == 3 && count(text, "    timeout-minutes: 360\n") == 3 &&
        count(text, guard) == 3 && count(text, "runs-on: ubuntu-26.04\n") == 1 &&
        count(text, "runs-on: macos-26\n") == 1 && count(text, "runs-on: windows-2025\n") == 1 &&
        strstr(text, "group: external-projects-${{ github.run_id }}\n") &&
        strstr(text, "cancel-in-progress: false\n") && !strstr(text, "cancel-in-progress: true") &&
        !strstr(text, "workflow_dispatch:") && !strstr(text, "workflow_call:") && !strstr(text, "pull_request:") &&
        !strstr(text, "pull_request_target:") && !strstr(text, "merge_group:") && !strstr(text, "continue-on-error:") &&
        !strstr(text, "matrix:") && !strstr(text, "\n    needs:") && !strstr(text, "ref: main") &&
        count(text, "ref: ${{ github.sha }}\n") == 3 &&
        strstr(text, "Build distinct RAD compiler configuration") && strstr(text, "build/rad-compiler") &&
        strstr(text, "Admit exact passing functional combinations") && strstr(text, "sha=%s") &&
        strstr(text, "qualification pending") && strstr(text, "pending/unqualified");
    for (unsigned i = 0; i < sizeof(projects) / sizeof(projects[0]); i += 1)
    {
        char command[96], artifact[96];
        int a = snprintf(command, sizeof(command), "external_projects %s ", projects[i]);
        int b = snprintf(artifact, sizeof(artifact), "evidence/projects/%s.tsv", projects[i]);
        valid = valid && a > 0 && (size_t)a < sizeof(command) && b > 0 && (size_t)b < sizeof(artifact) &&
            count(text, command) == 1 && strstr(text, artifact);
    }
    // Only the three frozen OS jobs are admitted, even if a fourth job has no
    // explicit runs-on (for example a reusable setup/project job).
    const char* cursor = strstr(text, "\njobs:\n");
    unsigned jobs = 0;
    if (cursor)
    {
        for (; *cursor; cursor += 1)
        {
            if (cursor[0] == '\n' && cursor[1] == ' ' && cursor[2] == ' ' &&
                cursor[3] != ' ' && cursor[3] != '\n' && cursor[3] != '#')
            {
                jobs += 1;
            }
        }
    }
    valid = valid && jobs == 3;
    return valid;
}

static int other_valid(const char* text)
{
    static const char* commands[] = {
        " test_cjson ", " test_zlib ", " test_stb ", " test_lua ", " test_quickjs ", " test_sqlite ",
        " test_yyjson ", " test_lz4 ", " test_sbase ", " test_doom ", " test_musl ", " test_cpython ",
        " test_ffmpeg ", " test_curl ", " test_openssl ", " test_qemu ", " test_linux ", " test_sdl3 ",
        " test_userspace ", " test_postgresql ", " test_mupdf ",
        " test_raddebugger --config", "git-qualification-cc\" --run", "git_qualification --run",
        "uses: ./.github/workflows/external-projects.yml", "uses: ./.github/workflows/throughput-real-source.yml",
    };
    int valid = 1;
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i += 1)
    {
        valid = valid && !strstr(text, commands[i]);
    }
    for (unsigned i = 0; i < sizeof(projects) / sizeof(projects[0]); i += 1)
    {
        char command[96];
        int length = snprintf(command, sizeof(command), "external_projects %s", projects[i]);
        valid = valid && length > 0 && (size_t)length < sizeof(command) && !strstr(text, command);
    }
    return valid;
}

static char* read_text(const char* path)
{
    char* result = NULL;
    FILE* file = fopen(path, "rb");
    if (file)
    {
        if (!fseek(file, 0, SEEK_END))
        {
            long length = ftell(file);
            if (length >= 0 && (unsigned long)length <= EXTERNAL_POLICY_FILE_LIMIT && !fseek(file, 0, SEEK_SET))
            {
                char* data = malloc((size_t)length + 1);
                if (data && fread(data, 1, (size_t)length, file) == (size_t)length)
                {
                    data[length] = 0;
                    if (!memchr(data, 0, (size_t)length))
                    {
                        result = data;
                    }
                }
                if (!result)
                {
                    free(data);
                }
            }
        }
        if (fclose(file) && result)
        {
            free(result);
            result = NULL;
        }
    }
    return result;
}

static int replace_control(const char* text, const char* old, const char* replacement)
{
    const char* found = strstr(text, old);
    int rejected = 0;
    if (found)
    {
        size_t prefix = (size_t)(found - text);
        size_t length = strlen(text) - strlen(old) + strlen(replacement);
        char* changed = malloc(length + 1);
        if (changed)
        {
            memcpy(changed, text, prefix);
            memcpy(changed + prefix, replacement, strlen(replacement));
            strcpy(changed + prefix + strlen(replacement), found + strlen(old));
            rejected = !owner_valid(changed);
            free(changed);
        }
    }
    return rejected;
}

static int self_test(const char* text)
{
    int passed = owner_valid(text) &&
        replace_control(text, "  push:\n", "  pull_request:\n") &&
        replace_control(text, "  push:\n", "  merge_group:\n") &&
        replace_control(text, "branches: [main]", "branches: [candidate]") &&
        replace_control(text, "branches: [main]", "tags: ['*']") &&
        replace_control(text, "permissions:\n", "  workflow_dispatch:\npermissions:\n") &&
        replace_control(text, "permissions:\n", "  workflow_call:\npermissions:\n") &&
        replace_control(text, "timeout-minutes: 360", "timeout-minutes: 90") &&
        replace_control(text, "runs-on: ubuntu-26.04", "runs-on: ubuntu-latest") &&
        replace_control(text, "cancel-in-progress: false", "cancel-in-progress: true") &&
        replace_control(text, "ref: ${{ github.sha }}", "ref: main") &&
        replace_control(text, "  linux:\n", "  fourth:\n    uses: ./extra.yml\n  linux:\n") &&
        replace_control(text, "external_projects cjson ", "external_projects zlib ") &&
        other_valid("run: build external_projects --self-test\n") &&
        !other_valid("uses: ./.github/workflows/external-projects.yml\n") &&
        !other_valid("run: build test_cjson external/cjson\n") &&
        !other_valid("run: build external_projects cjson\n");
    return passed;
}

int main(void)
{
    int failed = 0;
    char* owner = read_text(".github/workflows/external-projects.yml");
    if (!owner || !self_test(owner))
    {
        fputs("external-project owner topology or mutation controls failed\n", stderr);
        failed = 1;
    }
    free(owner);
    DIR* directory = opendir(".github/workflows");
    if (!directory)
    {
        failed = 1;
    }
    else
    {
        struct dirent* entry;
        unsigned owners = 0;
        while ((entry = readdir(directory)) != NULL)
        {
            size_t length = strlen(entry->d_name);
            if (length > 4 && (!strcmp(entry->d_name + length - 4, ".yml") ||
                (length > 5 && !strcmp(entry->d_name + length - 5, ".yaml"))))
            {
                int is_owner = !strcmp(entry->d_name, "external-projects.yml");
                owners += (unsigned)is_owner;
                for (unsigned i = 0; i < sizeof(legacy) / sizeof(legacy[0]); i += 1)
                {
                    if (!strcmp(entry->d_name, legacy[i]))
                    {
                        fprintf(stderr, "duplicate legacy owner: %s\n", entry->d_name);
                        failed = 1;
                    }
                }
                char path[1024];
                int size = snprintf(path, sizeof(path), ".github/workflows/%s", entry->d_name);
                char* text = size > 0 && (size_t)size < sizeof(path) ? read_text(path) : NULL;
                if (!text || (!is_owner && !other_valid(text)))
                {
                    fprintf(stderr, "unowned ordinary external route: %s\n", entry->d_name);
                    failed = 1;
                }
                free(text);
            }
        }
        if (closedir(directory) || owners != 1)
        {
            failed = 1;
        }
    }
    if (!failed)
    {
        puts("EXTERNAL_PROJECT_POLICY_PASS");
    }
    return failed;
}
