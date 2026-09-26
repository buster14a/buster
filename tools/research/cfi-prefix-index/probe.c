/* Disposable research driver: builds remain owned by repository build.c. */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PIN "ade6ac4b6ecb21f30b61b656439bac476c145e2f"
#define OBJECT "src/buster/lib/compiler/object/object.c"
#define ROOT "tools/research/cfi-prefix-index/"
static void die(const char *s) { fprintf(stderr, "CFI_ERROR %s\n", s); exit(1); }
static void *alloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die("allocation"); return p; }
static char *load(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) die(path);
    if (fseek(f, 0, SEEK_END)) die("seek");
    long n = ftell(f); if (n < 0 || n > 16000000) die("file length");
    rewind(f); char *s = alloc((size_t)n + 1);
    if (fread(s, 1, (size_t)n, f) != (size_t)n || fclose(f)) die("read");
    s[n] = 0; return s;
}
static void save(const char *path, const char *s) {
    FILE *f = fopen(path, "wb"); if (!f) die(path);
    if (fwrite(s, 1, strlen(s), f) != strlen(s) || fclose(f)) die("write");
}
static char *join(const char *a, const char *b) {
    size_t n = strlen(a), m = strlen(b); if (n > SIZE_MAX - m - 1) die("length overflow");
    char *s = alloc(n + m + 1); memcpy(s, a, n); memcpy(s + n, b, m + 1); return s;
}
static char *replace(char *s, const char *old, const char *new_text) {
    char *p = strstr(s, old); if (!p || strstr(p + strlen(old), old)) die("nonunique source anchor");
    size_t prefix = (size_t)(p - s); char *a = alloc(prefix + 1);
    memcpy(a, s, prefix); a[prefix] = 0;
    char *b = join(a, new_text), *out = join(b, p + strlen(old)); free(a); free(b); free(s); return out;
}
static char *function(const char *s) {
    const char *start = strstr(s, "BUSTER_GLOBAL_LOCAL bool object_mach_eh_frame_record_start(");
    if (!start) die("baseline function absent");
    const char *end = strstr(start, "\nBUSTER_GLOBAL_LOCAL ObjectFile object_read_mach_o64(");
    if (!end) die("baseline function end absent");
    char *out = alloc((size_t)(end - start) + 1); memcpy(out, start, (size_t)(end - start)); out[end - start] = 0; return out;
}
static char *instrument(const char *original, const char *header) {
    char *fn = join(original, "");
    fn = replace(fn, "BUSTER_GLOBAL_LOCAL bool object_mach_eh_frame_record_start(",
                     "BUSTER_GLOBAL_LOCAL bool cfi_scan(CfiProbeCache *c, ");
    fn = replace(fn, "memcpy(&length32, bytes.pointer + offset, sizeof(length32));",
                     "cfi_bump(c, &c->headers, 1);\n        memcpy(&length32, bytes.pointer + offset, sizeof(length32));");
    char *out = replace(join(header, ""), "/* CFI_BASELINE */", fn); free(fn); return out;
}
static void extract(const char *source, const char *header_path, const char *output) {
    char *s = load(source), *h = load(header_path), *fn = function(s), *impl = instrument(fn, h);
    char *a = join("#define CFI_PROBE_MODE 2\n", fn), *b = join(a, "\n"), *c = join(b, impl);
    save(output, c); free(c); free(b); free(a); free(impl); free(fn); free(h); free(s);
}
static void patch(const char *source, const char *header_path, unsigned mode) {
    char *s = load(source), *h = load(header_path), *fn = function(s), *impl = instrument(fn, h);
    char define[64]; snprintf(define, sizeof(define), "\n#define CFI_PROBE_MODE %u\n", mode);
    char *a = join(fn, define), *b = join(a, impl); s = replace(s, fn, b);
    s = replace(s, "    u64* prel32_place_offsets = arena_allocate(arena, u64, object->relocation_count);",
        "    CfiProbeCache *cfi_caches = 0;\n    u64* prel32_place_offsets = arena_allocate(arena, u64, object->relocation_count);");
    s = replace(s, "!object_mach_eh_frame_record_start(object->sections[source->section].data, source->offset, sizeof(u32), &place_offset)",
        "!cfi_writer_query(arena, &cfi_caches, section_count, source->section, object->sections[source->section].data, source->offset, sizeof(u32), &place_offset)");
    s = replace(s, "    if (prel32_count > UINT32_MAX - object->symbol_count || object->symbol_count + prel32_count > 0x00ffffff)",
        "    cfi_report(cfi_caches, section_count);\n    if (prel32_count > UINT32_MAX - object->symbol_count || object->symbol_count + prel32_count > 0x00ffffff)");
    save(source, s); free(b); free(a); free(impl); free(fn); free(h); free(s);
}
static unsigned number(const char *s) {
    char *end = 0; errno = 0; unsigned long n = strtoul(s, &end, 10);
    if (errno || !*s || *end || n > 65536) die("integer outside 0..65536"); return (unsigned)n;
}
static void generate(const char *path, unsigned functions, unsigned references, unsigned shape) {
    if (!functions || shape > 1) die("generator dimensions");
    FILE *f = fopen(path, "wb"); if (!f) die(path);
    for (unsigned i = 0; i < functions; ++i) {
        fprintf(f, "unsigned cfi_f%u(unsigned x) { ", i);
        if (shape) fprintf(f, "if (x & 1u) x ^= %uu; else x += %uu; ", i + 1, i + 7);
        fprintf(f, "return x + %uu; }\n", i);
    }
    if (references) {
        fprintf(f, "unsigned (*cfi_references[%u])(unsigned) = {\n", references);
        for (unsigned i = 0; i < references; ++i) fprintf(f, "cfi_f%u,\n", i % functions);
        fprintf(f, "};\n");
    }
    if (ferror(f) || fclose(f)) die("generator write");
}
static void run(const char *cmd) { printf("CFI_COMMAND %s\n", cmd); fflush(stdout); if (system(cmd) != 0) die(cmd); }
static uint64_t equal_files(const char *a, const char *b) {
    FILE *x = fopen(a, "rb"), *y = fopen(b, "rb"); if (!x || !y) die("missing object");
    uint64_t bytes = 0; int p, q;
    do { p = fgetc(x); q = fgetc(y); if (p != EOF) ++bytes; if (p != q) die("object mismatch"); } while (p != EOF);
    if (ferror(x) || ferror(y)) die("object read");
    fclose(x); fclose(y); return bytes;
}
static uint64_t counts(const char *path, uint64_t *calls) {
    FILE *f = fopen(path, "rb"); if (!f) die(path); char line[4096]; uint64_t total = 0; *calls = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "CFI_COUNTS ", 11)) continue;
        unsigned mode, section, overflow; unsigned long long q, h, c, cp, a, live, cache;
        int n = sscanf(line, "CFI_COUNTS mode=%u section=%u calls=%llu headers=%llu comparisons=%llu copied_entries=%llu allocated_entries=%llu live_entries=%llu cache_bytes=%llu overflow=%u",
            &mode, &section, &q, &h, &c, &cp, &a, &live, &cache, &overflow);
        if (n != 10 || overflow || total > UINT64_MAX - h || *calls > UINT64_MAX - q) die("bad count row");
        total += h; *calls += q; printf("%s", line);
    }
    if (ferror(f)) die("count read"); fclose(f); return total;
}
static void hosted(void) {
    const char *actions = getenv("GITHUB_ACTIONS"); if (!actions || strcmp(actions, "true")) die("requires authorized GitHub hosted workflow");
    /* All command arguments below are fixed or bounded numeric identifiers;
       there is no user-controlled shell interpolation. */
    run("test \"$(git hash-object " OBJECT ")\" = b798889750f58b6605064ffebcc0b376625b978a");
    char cmd[2048], path[256];
    for (unsigned m = 0; m < 3; ++m) {
        snprintf(cmd, sizeof(cmd), "git worktree add --detach cfi-work-%u " PIN, m); run(cmd);
        snprintf(cmd, sizeof(cmd), "cd cfi-work-%u && clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o cfi-build", m); run(cmd);
        snprintf(cmd, sizeof(cmd), "cd cfi-work-%u && ./cfi-build generate > ../cfi-evidence/configure-%u.log 2>&1", m, m); run(cmd);
    }
    run("cd cfi-work-0 && ./cfi-build test_self_host --config Release > ../cfi-evidence/baseline-self-host.log 2>&1");
    for (unsigned m = 0; m < 3; ++m) {
        snprintf(path, sizeof(path), "cfi-work-%u/" OBJECT, m); patch(path, ROOT "prototype.h", m);
        snprintf(cmd, sizeof(cmd), "cd cfi-work-%u && ./cfi-build build --config Release -t ide > ../cfi-evidence/build-%u.log 2>&1", m, m); run(cmd);
    }
    static const unsigned sizes[] = {16, 64, 256, 1024, 4096};
    static const char *targets[] = {"x86_64-apple-macos", "aarch64-apple-macos", "x86_64-linux-gnu"};
    for (unsigned z = 0; z < sizeof(sizes) / sizeof(*sizes); ++z) {
        for (unsigned refs = 0; refs < 2; ++refs) {
            for (unsigned shape = 0; shape < 2; ++shape) {
                char source[256]; snprintf(source, sizeof(source), "cfi-evidence/f%u-r%u-s%u.c", sizes[z], refs * 1024, shape);
                generate(source, sizes[z], refs * 1024, shape);
                for (unsigned t = 0; t < 3; ++t) {
                    char outputs[3][256], logs[3][256]; uint64_t h[3], q[3];
                    for (unsigned m = 0; m < 3; ++m) {
                        snprintf(outputs[m], sizeof(outputs[m]), "cfi-evidence/f%u-r%u-s%u-t%u-m%u.o", sizes[z], refs * 1024, shape, t, m);
                        snprintf(logs[m], sizeof(logs[m]), "cfi-evidence/f%u-r%u-s%u-t%u-m%u.log", sizes[z], refs * 1024, shape, t, m);
                        snprintf(cmd, sizeof(cmd), "cfi-work-%u/build/Release/ide cc --target=%s -g0 -O0 -c %s -o %s > %s 2>&1", m, targets[t], source, outputs[m], logs[m]); run(cmd);
                        h[m] = counts(logs[m], &q[m]);
                    }
                    if (t < 2 && (!q[0] || q[0] != q[1] || q[0] != q[2] || h[2] > h[0])) die("production count contract");
                    if (t == 2 && (q[0] || q[1] || q[2])) die("ELF control entered Mach writer");
                    uint64_t bytes = equal_files(outputs[0], outputs[1]); equal_files(outputs[0], outputs[2]);
                    printf("CFI_CASE functions=%u refs=%u shape=%u target=%s bytes=%" PRIu64 " calls=%" PRIu64 " base=%" PRIu64 " hint=%" PRIu64 " index=%" PRIu64 " identical=1\n",
                        sizes[z], refs * 1024, shape, targets[t], bytes, q[0], h[0], h[1], h[2]); fflush(stdout);
                }
            }
        }
    }
    /* Unrelated real fixture, held out from generated-family construction. */
    for (unsigned m = 0; m < 3; ++m) {
        snprintf(cmd, sizeof(cmd), "cfi-work-%u/build/Release/ide cc -g0 -O0 -c tests/basic_c_call_abi.c -o cfi-evidence/heldout-%u.o > cfi-evidence/heldout-%u.log 2>&1", m, m, m); run(cmd);
    }
    equal_files("cfi-evidence/heldout-0.o", "cfi-evidence/heldout-1.o"); equal_files("cfi-evidence/heldout-0.o", "cfi-evidence/heldout-2.o");
    run("cd cfi-work-2 && ./cfi-build test_self_host --config Release > ../cfi-evidence/index-self-host.log 2>&1");
    run("cd cfi-work-2 && ./cfi-build build --config Release -t test_all > ../cfi-evidence/index-tests.log 2>&1");
    puts("CFI_PRODUCTION_PASS");
}
int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "extract")) extract(argv[2], argv[3], argv[4]);
    else if (argc == 5 && !strcmp(argv[1], "patch")) { unsigned m = number(argv[4]); if (m > 2) die("mode"); patch(argv[2], argv[3], m); }
    else if (argc == 6 && !strcmp(argv[1], "generate")) generate(argv[2], number(argv[3]), number(argv[4]), number(argv[5]));
    else if (argc == 2 && !strcmp(argv[1], "hosted")) hosted();
    else die("usage: extract source prototype output | patch source prototype mode | generate output functions references shape | hosted");
    return 0;
}
