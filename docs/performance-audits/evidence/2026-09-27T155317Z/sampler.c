// LD_PRELOAD SIGPROF sampler: frame-pointer stack walk, raw addresses to $SAMPLER_OUT.
#define _GNU_SOURCE
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
    uint64_t start = __atomic_fetch_add(&used, depth + 1, __ATOMIC_RELAXED);
    if (start + depth + 1 <= MAX_WORDS)
    {
        buffer[start] = depth;
        memcpy(&buffer[start + 1], frame, depth * 8);
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
    // Header: text load map, then binary stack words.
    FILE* maps = fopen("/proc/self/maps", "r");
    char line[8192];
    while (maps && fgets(line, sizeof(line), maps))
    {
        if (strstr(line, " r-xp ") || strstr(line, " r--p 00000000 ")) fprintf(f, "MAP %s", line);
    }
    if (maps) fclose(maps);
    uint64_t n = used < MAX_WORDS ? used : MAX_WORDS;
    fprintf(f, "WORDS %lu\n", (unsigned long)n);
    fwrite(buffer, 8, n, f);
    fclose(f);
}

__attribute__((constructor)) static void start(void)
{
    if (!getenv("SAMPLER_OUT")) return;
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
