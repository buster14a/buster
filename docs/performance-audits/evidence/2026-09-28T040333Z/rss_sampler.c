// LD_PRELOAD SIGPROF sampler: frame-pointer stack walk plus resident pages at
// each sample, raw words to $SAMPLER_OUT.<pid>. Derived from the 2026-09-27T155317Z
// sampler.c; the only addition is one RSS word per sample, read with the
// async-signal-safe pread(2) of /proc/self/statm (opened once at start-up).
// Record layout: depth, rss_pages, frames[depth].
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

#define MAX_DEPTH 96
#define MAX_WORDS (64u << 20)
static uint64_t* buffer;
static volatile uint64_t used;
static int statm_fd = -1;

static uint64_t resident_pages(void)
{
    char text[128];
    uint64_t value = 0;
    ssize_t n = statm_fd >= 0 ? pread(statm_fd, text, sizeof(text) - 1, 0) : -1;
    if (n > 0)
    {
        ssize_t i = 0;
        while (i < n && text[i] != ' ') i += 1; // skip size
        i += 1;
        while (i < n && text[i] >= '0' && text[i] <= '9') { value = value * 10 + (uint64_t)(text[i] - '0'); i += 1; }
    }
    return value;
}

static void handler(int sig, siginfo_t* info, void* context)
{
    (void)sig; (void)info;
    ucontext_t* uc = context;
    uint64_t rip = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
    uint64_t rbp = (uint64_t)uc->uc_mcontext.gregs[REG_RBP];
    uint64_t rsp = (uint64_t)uc->uc_mcontext.gregs[REG_RSP];
    uint64_t frame[MAX_DEPTH];
    uint32_t depth = 0;
    frame[depth++] = rip;
    uint64_t lo = rsp, hi = rsp + (64u << 20);
    while (depth < MAX_DEPTH && rbp >= lo && rbp < hi && (rbp & 7) == 0)
    {
        uint64_t next = ((uint64_t*)rbp)[0];
        uint64_t ret = ((uint64_t*)rbp)[1];
        if (!ret) break;
        frame[depth++] = ret;
        if (next <= rbp) break;
        rbp = next;
    }
    uint64_t rss = resident_pages();
    uint64_t start = __atomic_fetch_add(&used, depth + 2, __ATOMIC_RELAXED);
    if (start + depth + 2 <= MAX_WORDS)
    {
        buffer[start] = depth;
        buffer[start + 1] = rss;
        memcpy(&buffer[start + 2], frame, depth * 8);
    }
}

static void dump(void)
{
    const char* path = getenv("SAMPLER_OUT");
    if (!path || !buffer) return;
    struct itimerval off = {0};
    setitimer(ITIMER_PROF, &off, 0);
    char name[4096];
    snprintf(name, sizeof(name), "%s.%d", path, (int)getpid());
    FILE* f = fopen(name, "wb");
    if (!f) return;
    FILE* maps = fopen("/proc/self/maps", "r");
    char line[8192];
    while (maps && fgets(line, sizeof(line), maps))
    {
        if (strstr(line, " r-xp ") || strstr(line, " r--p 00000000 ")) fprintf(f, "MAP %s", line);
    }
    if (maps) fclose(maps);
    uint64_t n = used < MAX_WORDS ? used : MAX_WORDS;
    fprintf(f, "FINALRSS %lu\n", (unsigned long)resident_pages());
    fprintf(f, "WORDS %lu\n", (unsigned long)n);
    fwrite(buffer, 8, n, f);
    fclose(f);
}

__attribute__((constructor)) static void start(void)
{
    if (!getenv("SAMPLER_OUT")) return;
    statm_fd = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    buffer = mmap(0, (size_t)MAX_WORDS * 8, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    struct sigaction action = {0};
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &action, 0);
    long usec = getenv("SAMPLER_USEC") ? atol(getenv("SAMPLER_USEC")) : 250;
    struct itimerval timer = {{0, usec}, {0, usec}};
    setitimer(ITIMER_PROF, &timer, 0);
    atexit(dump);
}
