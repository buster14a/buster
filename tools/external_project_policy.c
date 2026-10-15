// #3501 workflow scheduling policy; standalone hosted lint control.
// Ownership: ordinary external-project routes only. This bounded text scanner
// admits the canonical owner topology and rejects legacy/reusable fan-out.
// It is not a YAML interpreter; owner syntax changes require updating controls.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define EXTERNAL_POLICY_FILE_LIMIT (256u * 1024u)
static const char* projects[] = {"cjson", "zlib", "stb", "lua", "quickjs", "sqlite", "git", "rad", "ffmpeg"};
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

static int ffmpeg_owner_valid(const char* text)
{
    const char* rad_artifact = strstr(text, "- name: Retain rad evidence immediately\n");
    const char* assembler = strstr(text, "- name: Install FFmpeg external assembler\n");
    const char* checkout = strstr(text, "- name: Checkout pinned FFmpeg 7.1.3 source after existing Linux stages\n");
    const char* identity = strstr(text, "- name: Record pristine FFmpeg source identity\n");
    const char* route = strstr(text, "- name: ffmpeg independent project result\n");
    const char* artifact = strstr(text, "- name: Retain FFmpeg profile evidence immediately\n");
    const char* publish = strstr(text, "- name: Publish Linux OS outcomes\n");
    const char* artifact_paths =
        "          path: |\n"
        "            evidence/ffmpeg.status\n"
        "            evidence/ffmpeg.commit\n"
        "            evidence/ffmpeg.tree\n"
        "            evidence/ffmpeg.worktree-status\n"
        "            evidence/ffmpeg.files\n"
        "            evidence/ffmpeg-LICENSE.md\n"
        "            evidence/projects/ffmpeg.tsv\n"
        "            evidence/ffmpeg.log\n"
        "            build/ffmpeg-portable/receipt.txt\n"
        "            build/ffmpeg-portable/h264-target-actions.tsv\n"
        "            build/ffmpeg-portable/*.argv\n"
        "            build/ffmpeg-portable/*.stdout\n"
        "            build/ffmpeg-portable/*.stderr\n"
        "            build/ffmpeg-portable/*.status\n"
        "            build/ffmpeg-portable/reference/config.h\n"
        "            build/ffmpeg-portable/reference/config_components.h\n"
        "            build/ffmpeg-portable/reference/ffbuild/config.mak\n"
        "            build/ffmpeg-portable/reference/ffbuild/config.log\n"
        "            build/ffmpeg-portable/reference/fate-counts.tsv\n"
        "            build/ffmpeg-portable/reference/*.argv\n"
        "            build/ffmpeg-portable/reference/*.stdout\n"
        "            build/ffmpeg-portable/reference/*.stderr\n"
        "            build/ffmpeg-portable/reference/*.status\n"
        "            build/ffmpeg-portable/buster/config.h\n"
        "            build/ffmpeg-portable/buster/config_components.h\n"
        "            build/ffmpeg-portable/buster/ffbuild/config.mak\n"
        "            build/ffmpeg-portable/buster/ffbuild/config.log\n"
        "            build/ffmpeg-portable/buster/fate-counts.tsv\n"
        "            build/ffmpeg-portable/buster/*.argv\n"
        "            build/ffmpeg-portable/buster/*.stdout\n"
        "            build/ffmpeg-portable/buster/*.stderr\n"
        "            build/ffmpeg-portable/buster/*.status\n"
        "            build/ffmpeg-portable/buster/compiler-actions/*.argv\n"
        "            build/ffmpeg-portable/buster/compiler-actions/*.stdout\n"
        "            build/ffmpeg-portable/buster/compiler-actions/*.stderr\n"
        "            build/ffmpeg-portable/buster/compiler-actions/*.status\n"
        "            build/ffmpeg-portable/reference/compiler-actions/*.argv\n"
        "            build/ffmpeg-portable/reference/compiler-actions/*.stdout\n"
        "            build/ffmpeg-portable/reference/compiler-actions/*.stderr\n"
        "            build/ffmpeg-portable/reference/compiler-actions/*.status\n"
        "            build/ffmpeg-profiles.tsv\n"
        "            build/ffmpeg-portable-gnu17/receipt.txt\n"
        "            build/ffmpeg-portable-gnu17/h264-target-actions.tsv\n"
        "            build/ffmpeg-portable-gnu17/*.argv\n"
        "            build/ffmpeg-portable-gnu17/*.stdout\n"
        "            build/ffmpeg-portable-gnu17/*.stderr\n"
        "            build/ffmpeg-portable-gnu17/*.status\n"
        "            build/ffmpeg-portable-gnu17/reference/config.h\n"
        "            build/ffmpeg-portable-gnu17/reference/config_components.h\n"
        "            build/ffmpeg-portable-gnu17/reference/ffbuild/config.mak\n"
        "            build/ffmpeg-portable-gnu17/reference/ffbuild/config.log\n"
        "            build/ffmpeg-portable-gnu17/reference/dialect-manifest.txt\n"
        "            build/ffmpeg-portable-gnu17/reference/fate-counts.tsv\n"
        "            build/ffmpeg-portable-gnu17/reference/*.argv\n"
        "            build/ffmpeg-portable-gnu17/reference/compiler-actions/*.argv\n"
        "            build/ffmpeg-portable-gnu17/reference/*.stdout\n"
        "            build/ffmpeg-portable-gnu17/reference/compiler-actions/*.stdout\n"
        "            build/ffmpeg-portable-gnu17/reference/*.stderr\n"
        "            build/ffmpeg-portable-gnu17/reference/compiler-actions/*.stderr\n"
        "            build/ffmpeg-portable-gnu17/reference/*.status\n"
        "            build/ffmpeg-portable-gnu17/reference/compiler-actions/*.status\n"
        "            build/ffmpeg-portable-gnu17/buster/config.h\n"
        "            build/ffmpeg-portable-gnu17/buster/config_components.h\n"
        "            build/ffmpeg-portable-gnu17/buster/ffbuild/config.mak\n"
        "            build/ffmpeg-portable-gnu17/buster/ffbuild/config.log\n"
        "            build/ffmpeg-portable-gnu17/buster/dialect-manifest.txt\n"
        "            build/ffmpeg-portable-gnu17/buster/fate-counts.tsv\n"
        "            build/ffmpeg-portable-gnu17/buster/*.argv\n"
        "            build/ffmpeg-portable-gnu17/buster/compiler-actions/*.argv\n"
        "            build/ffmpeg-portable-gnu17/buster/*.stdout\n"
        "            build/ffmpeg-portable-gnu17/buster/compiler-actions/*.stdout\n"
        "            build/ffmpeg-portable-gnu17/buster/*.stderr\n"
        "            build/ffmpeg-portable-gnu17/buster/compiler-actions/*.stderr\n"
        "            build/ffmpeg-portable-gnu17/buster/*.status\n"
        "            build/ffmpeg-portable-gnu17/buster/compiler-actions/*.status\n"
        "            build/ffmpeg-optimized-gnu17/receipt.txt\n"
        "            build/ffmpeg-optimized-gnu17/nasm-provenance.txt\n"
        "            build/ffmpeg-optimized-gnu17/checkasm-clang-counts.tsv\n"
        "            build/ffmpeg-optimized-gnu17/checkasm-buster-counts.tsv\n"
        "            build/ffmpeg-optimized-gnu17/*.argv\n"
        "            build/ffmpeg-optimized-gnu17/*.stdout\n"
        "            build/ffmpeg-optimized-gnu17/*.stderr\n"
        "            build/ffmpeg-optimized-gnu17/*.status\n"
        "            build/ffmpeg-optimized-gnu17/reference/config.h\n"
        "            build/ffmpeg-optimized-gnu17/reference/config_components.h\n"
        "            build/ffmpeg-optimized-gnu17/reference/ffbuild/config.mak\n"
        "            build/ffmpeg-optimized-gnu17/reference/ffbuild/config.log\n"
        "            build/ffmpeg-optimized-gnu17/reference/dialect-manifest.txt\n"
        "            build/ffmpeg-optimized-gnu17/reference/fate-counts.tsv\n"
        "            build/ffmpeg-optimized-gnu17/reference/config.asm\n"
        "            build/ffmpeg-optimized-gnu17/reference/feature-manifest.tsv\n"
        "            build/ffmpeg-optimized-gnu17/reference/*.argv\n"
        "            build/ffmpeg-optimized-gnu17/reference/compiler-actions/*.argv\n"
        "            build/ffmpeg-optimized-gnu17/reference/*.stdout\n"
        "            build/ffmpeg-optimized-gnu17/reference/compiler-actions/*.stdout\n"
        "            build/ffmpeg-optimized-gnu17/reference/*.stderr\n"
        "            build/ffmpeg-optimized-gnu17/reference/compiler-actions/*.stderr\n"
        "            build/ffmpeg-optimized-gnu17/reference/*.status\n"
        "            build/ffmpeg-optimized-gnu17/reference/compiler-actions/*.status\n"
        "            build/ffmpeg-optimized-gnu17/buster/config.h\n"
        "            build/ffmpeg-optimized-gnu17/buster/config_components.h\n"
        "            build/ffmpeg-optimized-gnu17/buster/ffbuild/config.mak\n"
        "            build/ffmpeg-optimized-gnu17/buster/ffbuild/config.log\n"
        "            build/ffmpeg-optimized-gnu17/buster/dialect-manifest.txt\n"
        "            build/ffmpeg-optimized-gnu17/buster/fate-counts.tsv\n"
        "            build/ffmpeg-optimized-gnu17/buster/config.asm\n"
        "            build/ffmpeg-optimized-gnu17/buster/feature-manifest.tsv\n"
        "            build/ffmpeg-optimized-gnu17/buster/*.argv\n"
        "            build/ffmpeg-optimized-gnu17/buster/compiler-actions/*.argv\n"
        "            build/ffmpeg-optimized-gnu17/buster/*.stdout\n"
        "            build/ffmpeg-optimized-gnu17/buster/compiler-actions/*.stdout\n"
        "            build/ffmpeg-optimized-gnu17/buster/*.stderr\n"
        "            build/ffmpeg-optimized-gnu17/buster/compiler-actions/*.stderr\n"
        "            build/ffmpeg-optimized-gnu17/buster/*.status\n"
        "            build/ffmpeg-optimized-gnu17/buster/compiler-actions/*.status\n";
    const char* path_begin = artifact ? strstr(artifact, "          path: |\n") : NULL;
    const char* path_end = path_begin ? strstr(path_begin, "          include-hidden-files: true\n") : NULL;
    size_t allowlist_length = strlen(artifact_paths);
    int allowlist_exact = path_begin && path_end &&
        (size_t)(path_end - path_begin) == allowlist_length &&
        !memcmp(path_begin, artifact_paths, allowlist_length);
    int ordered = rad_artifact && assembler && checkout && identity && route && artifact && publish &&
                  rad_artifact < assembler && assembler < checkout && checkout < identity && identity < route && route < artifact && artifact < publish;
    int valid = ordered && allowlist_exact &&
        count(text, "          repository: FFmpeg/FFmpeg\n") == 1 &&
        count(text, "ref: f46e514491172d15bd74b4abb1814cd2f05a763e\n") == 1 &&
        count(text, "path: external/ffmpeg\n") == 1 &&
        count(text, "external/ffmpeg\n") == 2 &&
        count(text, "external/ffmpeg/") == 1 &&
        count(text, "source-repository: FFmpeg/FFmpeg\n") == 1 &&
        strstr(text, "git -C external/ffmpeg rev-parse HEAD") &&
        strstr(text, "external_projects ffmpeg 2>&1 | tee evidence/ffmpeg.log") &&
        strstr(text, "steps.ffmpeg_input.outcome == 'success'") &&
        strstr(text, "steps.ffmpeg_source_identity.outcome == 'success'") &&
        strstr(text, "steps.ffmpeg_source_verified.outcome == 'success'") &&
        count(text, "          sudo apt-get install -y --no-install-recommends nasm\n") == 1 &&
        count(text, "- name: Retain FFmpeg profile evidence immediately\n") == 1 &&
        count(text, "build/ffmpeg-portable/") == count(artifact_paths, "build/ffmpeg-portable/") &&
        !strstr(text, "            external/ffmpeg/") &&
        !strstr(text, "build/ffmpeg-portable/**") &&
        !strstr(text, "build/ffmpeg-portable/**/*.o") &&
        !strstr(text, "external/ffmpeg/**") && !strstr(text, "external/**");
    const char* profiles[] = {"build/ffmpeg-portable-gnu17/", "build/ffmpeg-optimized-gnu17/"};
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i += 1)
    {
        valid = valid && count(text, profiles[i]) == count(artifact_paths, profiles[i]);
    }
    return valid;
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
        strstr(text, "qualification pending") && strstr(text, "pending/unqualified") && ffmpeg_owner_valid(text);
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

// In non-owner workflow files, allow only the exact no-input first-party
// FFmpeg harness self-test in the existing lint blocks; full-build routes stay banned.
static int ffmpeg_self_test_routes_valid(const char* text)
{
    const char* command = "\"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test";
    const char* token = "test_ffmpeg";
    const char* cursor = text;
    unsigned routes = 0;
    int valid = 1;
    while ((cursor = strstr(cursor, token)) != NULL)
    {
        const char* begin = cursor;
        const char* end = cursor;
        while (begin > text && begin[-1] != '\n')
        {
            begin -= 1;
        }
        while (*end && *end != '\n')
        {
            end += 1;
        }
        if (end > begin && end[-1] == '\r')
        {
            end -= 1;
        }
        while (begin < end && (*begin == ' ' || *begin == '\t'))
        {
            begin += 1;
        }
        size_t command_length = strlen(command);
        valid = valid && (size_t)(end - begin) == command_length && !memcmp(begin, command, command_length);
        routes += 1;
        cursor += strlen(token);
    }
    return valid && (routes == 0 || routes == 2);
}

static int other_valid(const char* text)
{
    static const char* commands[] = {
        " test_cjson ", " test_zlib ", " test_stb ", " test_lua ", " test_quickjs ", " test_sqlite ",
        " test_yyjson ", " test_lz4 ", " test_sbase ", " test_doom ", " test_musl ", " test_cpython ",
        " test_curl ", " test_openssl ", " test_qemu ", " test_linux ", " test_sdl3 ",
        " test_userspace ", " test_postgresql ", " test_mupdf ", " external_projects ffmpeg",
        " test_raddebugger --config", "git-qualification-cc\" --run", "git_qualification --run",
        "repository: FFmpeg/FFmpeg", "ref: f46e514491172d15bd74b4abb1814cd2f05a763e", "path: external/ffmpeg",
        "uses: ./.github/workflows/external-projects.yml", "uses: ./.github/workflows/throughput-real-source.yml",
    };
    int valid = ffmpeg_self_test_routes_valid(text);
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

static int ffmpeg_self_test_route_controls(void)
{
    const char* valid =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* wrong_driver =
        "      \"$RUNNER_TEMP/other-driver\" test_ffmpeg --harness-self-test\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* extra_argument =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test /tmp/checkout\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* portable =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --portable /tmp/checkout /tmp/results /tmp/ide\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* optimized =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --optimized /tmp/checkout /tmp/results /tmp/ide /usr/bin/nasm\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* portable_gnu17 =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --portable-gnu17 /tmp/checkout /tmp/results /tmp/ide\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* optimized_gnu17 =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --optimized-gnu17 /tmp/checkout /tmp/results /tmp/ide /usr/bin/nasm\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* checkout_self_test =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --self-test /tmp/checkout\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* chained =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test; \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --optimized\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n";
    const char* mixed =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --harness-self-test\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --portable /tmp/checkout /tmp/results /tmp/ide\n";
    const char* duplicate_full_route =
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --optimized /tmp/checkout /tmp/results /tmp/ide /usr/bin/nasm\n"
        "      \"$RUNNER_TEMP/external-project-driver\" test_ffmpeg --optimized /tmp/checkout /tmp/results /tmp/ide /usr/bin/nasm\n";
    int passed = other_valid(valid) && !other_valid(wrong_driver) && !other_valid(extra_argument) &&
                 !other_valid(portable) && !other_valid(optimized) && !other_valid(portable_gnu17) && !other_valid(optimized_gnu17) && !other_valid(checkout_self_test) &&
                 !other_valid(chained) && !other_valid(mixed) && !other_valid(duplicate_full_route);
    return passed;
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
        !other_valid("run: build external_projects cjson\n") &&
        ffmpeg_self_test_route_controls() &&
        !other_valid("run: build external_projects ffmpeg\n") &&
        replace_control(text, "ref: f46e514491172d15bd74b4abb1814cd2f05a763e", "ref: main") &&
        replace_control(text, "repository: FFmpeg/FFmpeg", "repository: FFmpeg/fork") &&
        replace_control(text, "build/ffmpeg-portable/buster/compiler-actions/*.argv", "build/ffmpeg-portable/**") &&
        replace_control(text, "build/ffmpeg-optimized-gnu17/buster/compiler-actions/*.argv", "build/ffmpeg-optimized-gnu17/**") &&
        replace_control(text, "build/ffmpeg-portable-gnu17/reference/config.h", "build/ffmpeg-portable-gnu17/reference/*.o") &&
        replace_control(text, "sudo apt-get install -y --no-install-recommends nasm", "true");
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
