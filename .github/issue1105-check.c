#define main split_main
#include "issue1105-split.c"
#undef main

static bool has_anchor(const char *path, const char *anchor)
{
    char *data = read_file(path);
    bool found = false;
    for (char *line = data; *line; )
    {
        char *end = strchr(line, '\n');
        if (end == NULL) end = line + strlen(line);
        char *p = line;
        while (p < end && *p == '#') ++p;
        if (p > line && p < end && *p == ' ')
        {
            char heading[1024];
            size_t n = (size_t)(end - p - 1);
            require(n < sizeof(heading), "heading too long");
            memcpy(heading, p + 1, n);
            heading[n] = 0;
            char id[256];
            slug(id, heading);
            if (strcmp(id, anchor) == 0) found = true;
        }
        line = *end ? end + 1 : end;
    }
    free(data);
    return found;
}

int main(int argc, char **argv)
{
    require(argc >= 3, "usage: check ORIGINAL FILE...");
    char *old = read_file(argv[1]);
    char *root = read_file("AGENTS.md");
    char *policy_start = strstr(old, "## Goal and scope\n");
    char *policy_end = strstr(old, "## Current repository and exact smoke commands\n");
    require(policy_start && policy_end, "policy boundaries");
    char *policies = strndup(policy_start, (size_t)(policy_end - policy_start));
    require(policies && strstr(root, policies), "shared policies must be unchanged");
    const char *smoke_marker = "From the repository root, on the approved Linux Ryzen 7 9700X host:";
    char *smoke = strstr(old, smoke_marker);
    require(smoke != NULL, "smoke start");
    char *smoke_end = strstr(smoke, "## Sharing the 9700X\n");
    require(smoke_end != NULL, "smoke end");
    char *smoke_copy = strndup(smoke, (size_t)(smoke_end - smoke));
    require(smoke_copy && strstr(root, smoke_copy), "smoke commands must be unchanged");
    char *cores = strstr(old, "## Sharing the owner's machines' cores\n");
    require(cores != NULL, "cores start");
    char *cores_end = strstr(cores, "## Call graph check\n");
    require(cores_end != NULL, "cores end");
    char *cores_copy = strndup(cores, (size_t)(cores_end - cores));
    require(cores_copy && strstr(root, cores_copy), "cores and agent-memory rules must be unchanged");

    Buffer pp = {0};
    char *pp_text = read_file("docs/commands/preprocessor.md");
    text(&pp, pp_text);
    const char *duplicate = "`sqlite3.c`, musl 1.2.5's 1,357 sources, Lua 5.4.8 and this repository's\ntranslation units are token-identical; `docs/c-preprocessor.md` has the\nconfigurations. The 9700X job runs the cases and the repository's translation\nunits with `--clang`.\n\n";
    require(strstr(pp.data, "as every file's skips by guards must be. `sqlite3.c`") != NULL, "retain the fuller corpus qualification paragraph");
    if (strstr(pp.data, duplicate) != NULL)
    {
        replace_all(&pp, duplicate, "");
        write_file("docs/commands/preprocessor.md", &pp);
        puts("Removed the redundant preprocessor corpus paragraph; retained the fuller --tables src version.");
    }
    size_t links = 0;
    size_t failures = 0;
    for (int i = 2; i < argc; ++i)
    {
        char *data = read_file(argv[i]);
        char *p = data;
        while ((p = strstr(p, "](")) != NULL)
        {
            p += 2;
            char *end = strchr(p, ')');
            require(end != NULL, "unterminated link");
            char target[2048];
            size_t n = (size_t)(end - p);
            require(n < sizeof(target), "link too long");
            memcpy(target, p, n);
            target[n] = 0;
            p = end + 1;
            if (strstr(target, "://") || strncmp(target, "mailto:", 7) == 0) continue;
            char *fragment = strchr(target, '#');
            if (fragment) *fragment++ = 0;
            char path[4096];
            const char *slash = strrchr(argv[i], '/');
            int directory_length = slash ? (int)(slash - argv[i] + 1) : 0;
            if (!target[0]) snprintf(path, sizeof(path), "%s", argv[i]);
            else snprintf(path, sizeof(path), "%.*s%s", directory_length, argv[i], target);
            struct stat st;
            bool exists = stat(path, &st) == 0;
            bool anchor_ok = !fragment || (exists && has_anchor(path, fragment));
            ++links;
            if (!exists || !anchor_ok)
            {
                fprintf(stderr, "BAD LINK: %s -> %s%s%s (%s)\n", argv[i], path, fragment ? "#" : "", fragment ? fragment : "", exists ? "anchor" : "path");
                ++failures;
            }
        }
        free(data);
    }
    printf("Verified shared policies, complete smoke command list, cores and memory rules; %zu local Markdown links, %zu failures.\n", links, failures);
    require(failures == 0, "repair local Markdown links");
    return 0;
}
