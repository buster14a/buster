// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3309: Tier 1's loop store motion (ir_store_motion.c). The default mode is the process supervisor: it compiles this
// file's SUBJECT, loops over globals in every shape the pass takes or refuses, by Clang -O0 and by Buster's --tier1 with
// the pass and without it, on both host paths, strictly and verified, and requires the three programs to print the
// same lines. Buster's report must give the pass's counts for the subject, and GNU objdump must find the issue's kernel
// with its globals referenced only outside the loop (five references to the two without the pass, six with it: each
// global's address, the preheader's load and the write-back's compare). Then the
// symbols: for each declaration below, a unit whose one loop updates the object must move its key or leave it, as the
// emission's list of the unit's distinct objects says (check_program_store_motion_symbols).

#if defined(BUSTER_TIER1_STORE_MOTION_SETJMP)

// #3346's setjmp program, built without --tier1-strict: Tier 1 doesn't admit a function that calls setjmp, which runs by
// Tier 0, but the loop is its callee's, whose stores sink to the exit before the call that jumps back.
#include <setjmp.h>
#include <stdio.h>

long jump_a;
long jump_b;
long jump_seen;
static jmp_buf jump_buffer;

__attribute__((noinline)) void jump_leave(void)
{
    jump_seen = jump_a * 3 + jump_b;
    longjmp(jump_buffer, 1);
}

__attribute__((noinline)) void jump_loop(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        jump_a += i;
        jump_b ^= jump_a;
    }
    jump_leave();
}

int main(void)
{
    for (int n = 0; n < 40; n += 13)
    {
        if (setjmp(jump_buffer) == 0)
        {
            jump_loop(n);
        }
        printf("%d: %ld %ld %ld\n", n, jump_a, jump_b, jump_seen);
    }
    return 0;
}

#elif defined(BUSTER_TIER1_STORE_MOTION_SUBJECT)

#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>

long motion_a;
long motion_b;
static long motion_count;
double motion_sum;
float motion_f = 1.5f;
int motion_i32;
short motion_i16;
unsigned char motion_u8;
long* motion_pointer;
struct MotionRecord
{
    long a;
    int b;
    int c[4];
    double d;
} motion_record;
long motion_array[16];
static int motion_table[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
long motion_other;
long motion_quad[4] = { 1, 2, 3, 4 };
union MotionUnion
{
    long l;
    int i[2];
    char c[8];
} motion_union;

// The issue's kernel: the second statement reads what the first stored.
__attribute__((noinline)) void motion_basic(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_a += i;
        motion_b ^= motion_a;
    }
}

__attribute__((noinline)) void motion_recurrence(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_a = (motion_a * 3 + 1) & 0xffffff;
    }
}

__attribute__((noinline)) void motion_conditional(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        if (i & 1)
        {
            motion_a += 3;
        }
        else
        {
            motion_b -= motion_a;
        }
    }
}

// The outer loop takes both keys, the inner loop's accesses with them.
__attribute__((noinline)) void motion_nested(int n, int m)
{
    for (int i = 0; i < n; i += 1)
    {
        for (int j = 0; j < m; j += 1)
        {
            motion_a += j ^ i;
        }
        motion_b += motion_a;
    }
}

// Five keys of five types: four are taken (the pass's limit).
__attribute__((noinline)) void motion_types(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_sum += i * 0.5;
        motion_f = motion_f * 1.0001f + 1;
        motion_i32 += i;
        motion_i16 = (short)(motion_i16 * 3 + i);
        motion_u8 += 7;
    }
}

__attribute__((noinline)) void motion_fields(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_record.a += i;
        motion_record.b ^= (int)motion_record.a;
        motion_record.c[2] += motion_record.b;
        motion_record.d += 1.25;
    }
}

// An indexed access of the array meets its element 3's key, which stays in memory; the other global is taken.
__attribute__((noinline)) void motion_indexed(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_array[i & 15] += 1;
        motion_array[3] ^= 5;
        motion_a += motion_array[3];
    }
}

__attribute__((noinline)) void motion_elements(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_array[3] ^= i;
        motion_array[4] += motion_array[3];
    }
}

// A store through a pointer may write the global: the loop is left as it is, and is right when the pointer is the
// global's own address.
__attribute__((noinline)) void motion_unknown_store(long* p, int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_a += i;
        *p += motion_a;
    }
}

// A load through a pointer reads memory, which every store still writes: the element is taken, and is right when the
// pointer reaches the element itself.
__attribute__((noinline)) void motion_unknown_load(const long* p, int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_quad[1] += p[i & 3];
    }
}

__attribute__((noinline)) long motion_side(long x)
{
    motion_a += x;
    return motion_a;
}

// A call may read and write the global.
__attribute__((noinline)) void motion_call(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_a += i;
        motion_b += motion_side(i);
    }
}

__attribute__((noinline)) void motion_static(int n)
{
    static long hits;
    for (int i = 0; i < n; i += 1)
    {
        hits += i;
        motion_count += hits;
    }
    motion_other += hits;
}

// Three views of the same bytes: every key meets another.
__attribute__((noinline)) void motion_overlap(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_union.l += i;
        motion_union.i[1] ^= i;
        motion_union.c[0] += 1;
    }
}

// #3346: the stores sink to every exit, a break, a return and a goto out of the loop among them.
__attribute__((noinline)) long motion_exits(int n, int k)
{
    long result = 0;
    for (int i = 0; i < n; i += 1)
    {
        motion_a += i;
        if (motion_a > 4000)
        {
            return motion_a ^ motion_b;
        }
        motion_b ^= motion_a;
        if (i == k)
        {
            break;
        }
        if (i == k + 3)
        {
            goto out;
        }
        motion_count -= 1;
    }
    result = motion_b;
out:
    return result + motion_a + motion_count;
}

// The issue's second kernel: two doubles and a long.
__attribute__((noinline)) void motion_doubles(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_sum = motion_sum * 1.0000001 + 0.5;
        motion_record.d += motion_sum;
        motion_count += i & 3;
    }
}

// A page of its own, read-only while these run (main): a loop that stores nothing, of no iteration or whose condition
// never holds, must store nothing, not even the value the object holds.
static long motion_page[512] __attribute__((aligned(4096)));

__attribute__((noinline)) void motion_guarded(int n, int limit)
{
    for (int i = 0; i < n; i += 1)
    {
        if (i > limit)
        {
            motion_page[1] += i;
        }
    }
}

// The flag a signal handler sets is volatile: every access of the body stays where it is.
static volatile sig_atomic_t motion_signalled;

static void motion_handler(int signal)
{
    (void)signal;
    motion_signalled = 1;
}

__attribute__((noinline)) void motion_poll(int n)
{
    for (int i = 0; i < n && !motion_signalled; i += 1)
    {
        motion_a += i;
    }
}

// The global's address escapes to a pointer the loop reads through.
long* motion_escaped;

__attribute__((noinline)) void motion_escape(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_b += i;
        motion_a += *motion_escaped;
    }
}

// The global is the loop's own bound.
__attribute__((noinline)) void motion_while(int n)
{
    int i = 0;
    while (motion_a < n)
    {
        motion_a += ++i;
    }
    motion_b += i;
}

__attribute__((noinline)) void motion_do(int n)
{
    int i = 0;
    do
    {
        motion_a += i;
        motion_b ^= motion_a;
        i += 1;
    } while (i < n);
}

__attribute__((noinline)) int motion_return(int n)
{
    int result = -1;
    for (int i = 0; result < 0 && i < n; i += 1)
    {
        motion_a += i;
        if (motion_a > 1000)
        {
            result = i;
        }
        else
        {
            motion_b += motion_a;
        }
    }
    return result;
}

__attribute__((noinline)) void motion_break(int n, int k)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_a += i;
        if (i == k)
        {
            break;
        }
        motion_b += 2;
    }
}

__attribute__((noinline)) void motion_internal(int n)
{
    for (int i = 0; i < n; i += 1)
    {
        motion_table[2] += motion_table[i & 7];
        motion_table[5] ^= motion_table[2];
    }
}

// The pointer is loaded from a global: its store may be any object's.
__attribute__((noinline)) void motion_loaded_pointer(int n)
{
    static long cell;
    motion_pointer = &cell;
    for (int i = 0; i < n; i += 1)
    {
        *motion_pointer += i;
        motion_a += cell;
    }
    motion_other ^= cell;
}

__attribute__((noinline)) void motion_goto(int n)
{
    int i = 0;
again:
    motion_a += i;
    motion_b ^= motion_a;
    i += 1;
    if (i < n)
    {
        goto again;
    }
}

// A load through the global's own address, taken before the loop.
__attribute__((noinline)) void motion_self(int n)
{
    long* p = &motion_a;
    for (int i = 0; i < n; i += 1)
    {
        motion_a += i;
        motion_b += *p;
    }
}

static void motion_show(const char* name)
{
    printf("%s: %ld %ld %ld %.6f %.3f %d %d %d | %ld %d %d %.3f | %ld %ld %ld | %d %d | %ld %ld %ld\n", name, motion_a, motion_b, motion_count, motion_sum, (double)motion_f, motion_i32, motion_i16,
           motion_u8, motion_record.a, motion_record.b, motion_record.c[2], motion_record.d, motion_array[0], motion_array[3], motion_array[4], motion_table[2], motion_table[5], motion_other,
           motion_union.l, motion_quad[1]);
}

int main(void)
{
    long local = 5;
    signal(SIGUSR1, motion_handler);
    static const long table[4] = { 3, 1, 4, 1 };
    for (int n = 0; n < 40; n += 13)
    {
        motion_basic(n);
        motion_show("basic");
        motion_recurrence(n);
        motion_show("recurrence");
        motion_conditional(n);
        motion_show("conditional");
        motion_nested(n, 5);
        motion_show("nested");
        motion_types(n);
        motion_show("types");
        motion_fields(n);
        motion_show("fields");
        motion_indexed(n);
        motion_show("indexed");
        motion_elements(n);
        motion_show("elements");
        motion_unknown_store(&local, n);
        motion_show("unknown store");
        motion_unknown_store(&motion_a, n);
        motion_show("unknown store of the global");
        motion_unknown_load(table, n);
        motion_show("unknown load");
        motion_unknown_load(motion_quad, n);
        motion_show("unknown load of the global");
        motion_call(n);
        motion_show("call");
        motion_static(n);
        motion_show("static");
        motion_overlap(n);
        motion_show("overlap");
        motion_a &= 0xffff;
        motion_while(n * 50);
        motion_show("while");
        motion_do(n);
        motion_show("do");
        printf("return=%d\n", motion_return(n));
        motion_show("return");
        motion_break(n, 7);
        motion_show("break");
        motion_internal(n);
        motion_show("internal");
        motion_loaded_pointer(n);
        motion_show("loaded pointer");
        motion_goto(n);
        motion_show("goto");
        motion_self(n);
        motion_show("self");
        printf("exits=%ld\n", motion_exits(n, 30));
        motion_show("exits");
        printf("exits=%ld\n", motion_exits(n, 5));
        motion_show("exits by break");
        printf("exits=%ld\n", motion_exits(n, 2));
        motion_show("exits by goto");
        motion_a = 3000;
        printf("exits=%ld\n", motion_exits(n, 30));
        motion_show("exits by return");
        motion_doubles(n);
        motion_show("doubles");
        motion_page[1] = n;
        motion_page[2] = n * 2;
        if (mprotect(motion_page, sizeof(motion_page), PROT_READ) != 0)
        {
            return 1;
        }
        motion_guarded(0, 0);
        motion_guarded(n, 1000);
        if (mprotect(motion_page, sizeof(motion_page), PROT_READ | PROT_WRITE) != 0)
        {
            return 1;
        }
        motion_guarded(n, n / 2);
        printf("page=%ld %ld\n", motion_page[1], motion_page[2]);
        motion_signalled = 0;
        if (n == 26)
        {
            raise(SIGUSR1);
        }
        motion_poll(n);
        motion_show("poll");
        motion_escaped = &motion_a;
        motion_escape(n);
        motion_show("escape");
        motion_escaped = &motion_b;
        motion_escape(n);
        motion_show("escape to the other");
        motion_a &= 0xffff;
        motion_b &= 0xffff;
    }
    printf("local=%ld\n", local);
    return 0;
}

#else

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// One command, its standard output to `output` and its standard error to `errors` where they aren't null.
static bool motion_run(char* const arguments[], const char* output, const char* errors)
{
    const pid_t child = fork();
    bool valid = child >= 0;
    if (child == 0)
    {
        const char* const paths[2] = { output, errors };
        const int streams[2] = { STDOUT_FILENO, STDERR_FILENO };
        for (unsigned k = 0; k < 2; k += 1)
        {
            if (paths[k] != NULL)
            {
                const int descriptor = open(paths[k], O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (descriptor < 0 || dup2(descriptor, streams[k]) < 0)
                {
                    perror("store motion output");
                    _exit(126);
                }
                close(descriptor);
            }
        }
        alarm(120);
        execvp(arguments[0], arguments);
        perror(arguments[0]);
        _exit(127);
    }
    if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        valid = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (!valid)
    {
        fprintf(stderr, "store motion command failed: %s\n", arguments[0]);
    }
    return valid;
}

static bool motion_same(const char* a, const char* b)
{
    FILE* first = fopen(a, "r");
    FILE* second = fopen(b, "r");
    bool same = first != NULL && second != NULL;
    unsigned long bytes = 0;
    while (same)
    {
        const int x = fgetc(first);
        const int y = fgetc(second);
        same = x == y;
        bytes += x != EOF;
        if (x == EOF || y == EOF)
        {
            break;
        }
    }
    same = same && bytes != 0;
    if (first != NULL)
    {
        fclose(first);
    }
    if (second != NULL)
    {
        fclose(second);
    }
    return same;
}

// The report's store motion line's field `name`, or -1 where the report has no such line.
static long motion_count(const char* report, const char* name)
{
    FILE* stream = fopen(report, "r");
    char line[4096];
    char key[64];
    long value = -1;
    snprintf(key, sizeof(key), "\t%s=", name);
    while (stream != NULL && fgets(line, sizeof(line), stream) != NULL)
    {
        const char* field = strncmp(line, "tier1-store-motion\t", strlen("tier1-store-motion\t")) == 0 ? strstr(line, key) : NULL;
        value = field != NULL ? strtol(field + strlen(key), NULL, 10) : value;
    }
    if (stream != NULL)
    {
        fclose(stream);
    }
    return value;
}

// How many relocations objdump -dr's text has in function `name`.
static unsigned motion_references(const char* path, const char* name)
{
    FILE* stream = fopen(path, "r");
    char line[4096];
    char label[160];
    bool inside = false;
    unsigned count = 0;
    snprintf(label, sizeof(label), "<%s>:", name);
    while (stream != NULL && fgets(line, sizeof(line), stream) != NULL)
    {
        const bool starts = strstr(line, ">:") != NULL;
        inside = starts ? strstr(line, label) != NULL : inside;
        count += inside && strstr(line, "R_X86_64_") != NULL;
    }
    if (stream != NULL)
    {
        fclose(stream);
    }
    return count;
}

// A unit whose one loop updates X, and the keys the pass must move in it.
typedef struct MotionSymbol MotionSymbol;
struct MotionSymbol
{
    const char* name;
    const char* declaration;
    const char* option;
    long keys;
};

static const MotionSymbol motion_symbols[] = {
    // The unit's own objects.
    { "a tentative definition", "long X;", NULL, 1 },
    { "an initialized definition", "long X = 5;", NULL, 1 },
    { "a static", "static long X;", NULL, 1 },
    { "a declaration and its definition", "extern long X; long X;", NULL, 1 },
    { "an aligned definition", "__attribute__((aligned(64))) long X;", NULL, 1 },
    { "an unused static", "__attribute__((unused)) static long X;", NULL, 1 },
    { "a hidden definition", "__attribute__((visibility(\"hidden\"))) long X;", NULL, 1 },
    { "a hidden definition in position-independent code", "__attribute__((visibility(\"hidden\"))) long X;", "-fPIC", 1 },
    { "a static in position-independent code", "static long X;", "-fPIC", 1 },
    { "a function's asm label beside it", "long X; extern int renamed(int) __asm__(\"other\");", NULL, 1 },
    { "an array's element", "long A[4];\n#define X A[2]", NULL, 1 },
    { "a record's member", "struct S { int a; long b; } s;\n#define X s.b", NULL, 1 },
    // Another unit's object, or one another definition may replace or another name may reach.
    { "a declaration alone", "extern long X;", NULL, -1 },
    { "an array of unknown bound", "extern long A[];\n#define X A[1]", NULL, -1 },
    { "a weak definition", "__attribute__((weak)) long X;", NULL, -1 },
    { "a unit with an alias", "long X; extern long Y __attribute__((alias(\"X\")));", NULL, -1 },
    { "an asm label", "long X __asm__(\"renamed\");", NULL, -1 },
    { "another object's asm label of its name", "long X; extern long Y __asm__(\"X\");", NULL, -1 },
    { "a section attribute", "__attribute__((section(\"mine\"))) long X;", NULL, -1 },
    { "a common attribute", "__attribute__((common)) long X;", NULL, -1 },
    { "-fcommon", "long X;", "-fcommon", -1 },
    { "a preemptible definition in position-independent code", "long X;", "-fPIC", -1 },
    { "#pragma weak of it", "long X;\n#pragma weak X", NULL, -1 },
    { "#pragma weak of another", "long X; long Z;\n#pragma weak Z", NULL, -1 },
    { "an asm statement at file scope", "long X; __asm__(\".globl other\\n.set other, X\");", NULL, -1 },
    // Objects whose accesses every pass keeps.
    { "a volatile object", "volatile long X;", NULL, -1 },
    { "an atomic object", "_Atomic long X;", NULL, -1 },
    { "a thread-local", "_Thread_local long X;", NULL, -1 },
    // A volatile expression anywhere in the body: its indirect accesses carry no flag (#2805).
    { "a volatile object beside it", "long X; volatile int V;\n#define AFTER V = 1;", NULL, -1 },
};

int main(int argc, char** argv)
{
    bool valid = argc == 3;
    if (!valid)
    {
        fprintf(stderr, "usage: %s BUSTER_COMPILER CLANG (from the repository root)\n", argv[0]);
    }
    char directory[] = "/tmp/buster-store-motion-XXXXXX";
    const bool created = valid && mkdtemp(directory) != NULL;
    valid = valid && created;
    char reference[128];
    char reference_output[128];
    char program[128];
    char program_output[128];
    char object[128];
    char report[128];
    char assembly[128];
    char unit[128];
    // All paths have a fixed, bounded prefix and fixed suffixes, smaller than their buffers.
    snprintf(reference, sizeof(reference), "%s/reference", directory);
    snprintf(reference_output, sizeof(reference_output), "%s/reference.txt", directory);
    snprintf(program, sizeof(program), "%s/program", directory);
    snprintf(program_output, sizeof(program_output), "%s/program.txt", directory);
    snprintf(object, sizeof(object), "%s/subject.o", directory);
    snprintf(report, sizeof(report), "%s/report.txt", directory);
    snprintf(assembly, sizeof(assembly), "%s/assembly.txt", directory);
    snprintf(unit, sizeof(unit), "%s/unit.c", directory);
    char source[] = "tests/driver/tier1_store_motion.c";
    if (valid)
    {
        char* compile[] = { argv[2], "-std=c17", "-O0", "-fwrapv", "-DBUSTER_TIER1_STORE_MOTION_SUBJECT", source, "-o", reference, NULL };
        char* execute[] = { reference, NULL };
        valid = motion_run(compile, NULL, NULL) && motion_run(execute, reference_output, NULL);
    }
    // With the pass and without it, by both host paths.
    for (unsigned variant = 0; valid && variant < 4; variant += 1)
    {
        const bool moved = variant < 2;
        const bool scalar = (variant & 1) != 0;
        unlink(program);
        unlink(program_output);
        unlink(object);
        unlink(report);
        unlink(assembly);
        char* build[] = { argv[1], "--tier1", "--tier1-strict", "--verify", "--lanes", "1", "--tier1-report", moved ? "--tier1-passes=+storemotion" : "--tier1-passes=-storemotion", "-std=c17",
                          "-DBUSTER_TIER1_STORE_MOTION_SUBJECT", source, "-o", program, scalar ? "--scalar" : NULL, NULL };
        char* compile[] = { argv[1], "--tier1", "--tier1-strict", "--verify", "--lanes", "1", moved ? "--tier1-passes=+storemotion" : "--tier1-passes=-storemotion", "-std=c17",
                            "-DBUSTER_TIER1_STORE_MOTION_SUBJECT", "-c", source, "-o", object, scalar ? "--scalar" : NULL, NULL };
        char* execute[] = { program, NULL };
        char* disassemble[] = { "objdump", "-dr", "--no-show-raw-insn", object, NULL };
        valid = motion_run(build, NULL, report) && motion_run(execute, program_output, NULL) && motion_run(compile, NULL, NULL) && motion_run(disassemble, assembly, NULL);
        if (valid && !motion_same(reference_output, program_output))
        {
            fprintf(stderr, "store motion: the program's lines differ from Clang -O0's\n");
            valid = false;
        }
        // The subject's loops that store to one of its objects, by outcome, and what the pass moved.
        // #3346's: the keys whose stores sink, those kept for a load the pass can't place or one that meets them, and
        // the write-backs.
        static const char* const fields[] = { "loops", "moved", "barrier", "unknown-store", "no-key", "keys", "key.met", "key.limit", "loads", "stores", "sunk", "sink.load", "sink.met", "sink.exit", "write-backs" };
        static const long expected[] = { 26, 21, 1, 2, 2, 43, 4, 1, 59, 43, 36, 5, 2, 0, 47 };
        for (unsigned k = 0; valid && k < sizeof(fields) / sizeof(fields[0]); k += 1)
        {
            const long count = motion_count(report, fields[k]);
            if (count != (moved ? expected[k] : -1))
            {
                fprintf(stderr, "store motion: the report's %s is %ld, %ld expected\n", fields[k], count, moved ? expected[k] : -1);
                valid = false;
            }
        }
        // The issue's kernel: with the pass, three references a global, all outside the loop (its address, the
        // preheader's load and the write-back's compare of the key's bytes); without it, five, two of them in the loop.
        const unsigned references = valid ? motion_references(assembly, "motion_basic") : 0;
        if (valid && references != (moved ? 6u : 5u))
        {
            fprintf(stderr, "store motion: motion_basic has %u references to its globals, %u expected\n", references, moved ? 6u : 5u);
            valid = false;
        }
        printf("Tier 1 store motion %s, %s: %s\n", moved ? "on" : "off", scalar ? "scalar" : "default", valid ? "pass" : "FAIL");
    }
    // #3346's setjmp program, by Clang -O0 and by --tier1 with the pass, which sinks its loop's two keys.
    if (valid)
    {
        unlink(reference);
        unlink(reference_output);
        unlink(program);
        unlink(program_output);
        unlink(report);
        char* reference_build[] = { argv[2], "-std=c17", "-O0", "-DBUSTER_TIER1_STORE_MOTION_SETJMP", source, "-o", reference, NULL };
        char* reference_run[] = { reference, NULL };
        char* build[] = { argv[1], "--tier1", "--verify", "--lanes", "1", "--tier1-report", "--tier1-passes=+storemotion", "-std=c17", "-DBUSTER_TIER1_STORE_MOTION_SETJMP", source, "-o", program, NULL };
        char* execute[] = { program, NULL };
        valid = motion_run(reference_build, NULL, NULL) && motion_run(reference_run, reference_output, NULL) && motion_run(build, NULL, report) && motion_run(execute, program_output, NULL);
        if (valid && !motion_same(reference_output, program_output))
        {
            fprintf(stderr, "store motion: the setjmp program's lines differ from Clang -O0's\n");
            valid = false;
        }
        if (valid && motion_count(report, "sunk") != 2)
        {
            fprintf(stderr, "store motion: the setjmp program sank %ld keys, 2 expected\n", motion_count(report, "sunk"));
            valid = false;
        }
        printf("Tier 1 store motion across setjmp: %s\n", valid ? "pass" : "FAIL");
    }
    for (unsigned k = 0; valid && k < sizeof(motion_symbols) / sizeof(motion_symbols[0]); k += 1)
    {
        const MotionSymbol* symbol = &motion_symbols[k];
        FILE* stream = fopen(unit, "w");
        valid = stream != NULL;
        if (valid)
        {
            fprintf(stream, "%s\n#ifndef AFTER\n#define AFTER\n#endif\nvoid f(int n) { for (int i = 0; i < n; i++) { X += i; } AFTER }\n", symbol->declaration);
            valid = fclose(stream) == 0;
        }
        unlink(object);
        unlink(report);
        char option[] = "-Wno-unknown-attributes";
        char* compile[] = { argv[1], "--tier1", "--tier1-report", "--tier1-passes=+storemotion", "-std=c17", "-c", unit, "-o", object, symbol->option != NULL ? (char*)symbol->option : option, NULL };
        valid = valid && motion_run(compile, NULL, report);
        const long keys = valid ? motion_count(report, "keys") : 0;
        if (valid && keys != symbol->keys)
        {
            fprintf(stderr, "store motion: %s: %ld keys moved, %ld expected\n", symbol->name, keys, symbol->keys);
            valid = false;
        }
    }
    printf("Tier 1 store motion symbols: %s\n", valid ? "pass" : "FAIL");
    if (valid)
    {
        unlink(reference);
        unlink(reference_output);
        unlink(program);
        unlink(program_output);
        unlink(object);
        unlink(report);
        unlink(assembly);
        unlink(unit);
        valid = rmdir(directory) == 0;
    }
    if (!valid && created)
    {
        fprintf(stderr, "store motion regression artifacts retained in %s\n", directory);
    }
    return valid ? 0 : 1;
}

#endif
