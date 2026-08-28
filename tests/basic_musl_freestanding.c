// The freestanding probe the musl harness links against a Buster-built musl
// archive.  It has no include of any kind: the harness compiles it with
// -nostdinc against musl's own headers, and everything it calls comes out of
// the archive, so the host libc is not on the link line and not in the
// process.  Entry is _start, and the only things that are not musl's are the
// two raw system calls that write the transcript and leave.
// The transcript is the comparison: the harness runs the Clang-built and the
// Buster-built programs and requires the bytes to be identical.  A routine
// that computes a different answer therefore fails the run rather than
// silently passing, which a link-and-exit check would not.

typedef unsigned long size_type;
typedef long signed_size_type;

// musl declares these in its own headers; the probe declares them itself so
// that a header change cannot quietly turn a call into a builtin expansion.
extern void* memcpy(void* restrict, const void* restrict, size_type);
extern void* memmove(void*, const void*, size_type);
extern void* memset(void*, int, size_type);
extern int memcmp(const void*, const void*, size_type);
extern void* memchr(const void*, int, size_type);
extern size_type strlen(const char*);
extern char* strcpy(char* restrict, const char* restrict);
extern char* strcat(char* restrict, const char* restrict);
extern int strcmp(const char*, const char*);
extern int strncmp(const char*, const char*, size_type);
extern char* strchr(const char*, int);
extern char* strrchr(const char*, int);
extern char* strstr(const char*, const char*);
extern size_type strspn(const char*, const char*);
extern size_type strcspn(const char*, const char*);
extern int atoi(const char*);
extern void* bsearch(const void*, const void*, size_type, size_type, int (*)(const void*, const void*));
extern int toupper(int);
extern int isalpha(int);
extern int isdigit(int);
// Names musl publishes only through weak_alias(): every one of them is
// __attribute__((alias)) over an internal name, so before that attribute was
// implemented the archive held the internal name and this link failed.  These
// four and not malloc, which musl publishes the same way but which reaches a
// lock through the thread pointer that a bare _start never established.
extern char* stpcpy(char* restrict, const char* restrict);
extern char* stpncpy(char* restrict, const char* restrict, size_type);
extern char* strchrnul(const char*, int);
extern void* memrchr(const void*, int, size_type);

void _start(void);

#if defined(__x86_64__) || defined(_M_X64)

#define SYSTEM_CALL_WRITE 1
#define SYSTEM_CALL_EXIT 60

static long system_call3(long number, long a1, long a2, long a3)
{
    unsigned long result;
    __asm__ __volatile__("syscall" : "=a"(result) : "a"(number), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return (long)result;
}

static void emit(const char* text, size_type length)
{
    size_type written = 0;
    while (written < length)
    {
        long step = system_call3(SYSTEM_CALL_WRITE, 1, (long)(text + written), (long)(length - written));
        if (step <= 0)
        {
            break;
        }
        written += (size_type)step;
    }
}

static char transcript[4096];
static size_type transcript_length;

static void append(const char* text)
{
    size_type length = strlen(text);
    memcpy(transcript + transcript_length, text, length);
    transcript_length += length;
}

static void append_signed(long value)
{
    char digits[24];
    size_type index = sizeof(digits);
    unsigned long magnitude = value < 0 ? (unsigned long)-value : (unsigned long)value;
    do
    {
        digits[--index] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude);
    if (value < 0)
    {
        digits[--index] = '-';
    }
    memcpy(transcript + transcript_length, digits + index, sizeof(digits) - index);
    transcript_length += sizeof(digits) - index;
}

static void record(const char* name, long value)
{
    append(name);
    append("=");
    append_signed(value);
    append("\n");
}

static int compare_int(const void* left, const void* right)
{
    int a = *(const int*)left;
    int b = *(const int*)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static int sorted_table[8] = {2, 3, 5, 7, 11, 13, 17, 19};

// The sign of strcmp and friends is what C fixes; the magnitude is not, so the
// transcript records the sign.  Two conforming implementations may disagree on
// the magnitude and still both be right, and the comparison here has to fail
// only on a real difference.
static long sign_of(long value)
{
    return value < 0 ? -1 : value > 0 ? 1 : 0;
}

static void run(void)
{
    char buffer[64];
    char joined[64];
    memset(buffer, 'x', sizeof(buffer));
    record("memset_first", (long)(unsigned char)buffer[0]);
    record("memset_last", (long)(unsigned char)buffer[sizeof(buffer) - 1]);

    strcpy(buffer, "the quick brown fox");
    record("strlen", (long)strlen(buffer));
    record("strcmp_equal", sign_of(strcmp(buffer, "the quick brown fox")));
    record("strcmp_less", sign_of(strcmp(buffer, "the quick brown gox")));
    record("strncmp_prefix", sign_of(strncmp(buffer, "the quick", 9)));
    record("strchr_offset", (long)(strchr(buffer, 'q') - buffer));
    record("strrchr_offset", (long)(strrchr(buffer, 'o') - buffer));
    record("strstr_offset", (long)(strstr(buffer, "brown") - buffer));
    record("strstr_missing", strstr(buffer, "purple") == 0);
    record("strspn", (long)strspn(buffer, "the "));
    record("strcspn", (long)strcspn(buffer, "qz"));

    strcpy(joined, "abc");
    strcat(joined, "def");
    record("strcat_length", (long)strlen(joined));
    record("strcat_compare", sign_of(strcmp(joined, "abcdef")));

    memcpy(buffer, "0123456789", 11);
    memmove(buffer + 2, buffer, 8);
    record("memmove_compare", sign_of(memcmp(buffer, "01012345", 8)));
    record("memchr_offset", (long)((const char*)memchr(buffer, '5', 10) - buffer));

    record("atoi_positive", atoi("  4711abc"));
    record("atoi_negative", atoi("-2026"));

    int key = 13;
    const int* found = (const int*)bsearch(&key, sorted_table, sizeof(sorted_table) / sizeof(sorted_table[0]), sizeof(sorted_table[0]), compare_int);
    record("bsearch_offset", found ? (long)(found - sorted_table) : -1);
    key = 14;
    record("bsearch_missing", bsearch(&key, sorted_table, sizeof(sorted_table) / sizeof(sorted_table[0]), sizeof(sorted_table[0]), compare_int) == 0);

    strcpy(buffer, "the quick brown fox");
    record("stpcpy_end", (long)(stpcpy(joined, "weak") - joined));
    memset(joined, '.', sizeof(joined));
    record("stpncpy_end", (long)(stpncpy(joined, "alias", 8) - joined));
    record("stpncpy_pad", (long)(unsigned char)joined[7]);
    record("strchrnul_found", (long)(strchrnul(buffer, 'q') - buffer));
    record("strchrnul_missing", (long)(strchrnul(buffer, 'z') - buffer));
    record("memrchr_offset", (long)((const char*)memrchr(buffer, 'o', strlen(buffer)) - buffer));
    record("memrchr_missing", memrchr(buffer, 'z', strlen(buffer)) == 0);

    record("toupper", toupper('q'));
    record("isalpha", isalpha('q') != 0);
    record("isdigit", isdigit('q') != 0);

    append("hello from a Buster-built musl\n");
}

// The process entry point. At process entry the stack pointer is 16-byte
// aligned, while the ABI a compiler generates code against expects the
// eight-byte offset a CALL instruction leaves, so a C _start sees the opposite
// parity from the one its callees were compiled for. Buster's emitters spill
// through plain moves and do not depend on the alignment; the reference build
// is compiled with -mstackrealign, which makes Clang realign here and hands
// every musl routine below the alignment its aligned vector spills need.
void _start(void)
{
    run();
    emit(transcript, transcript_length);
    system_call3(SYSTEM_CALL_EXIT, 0, 0, 0);
    __builtin_unreachable();
}

#else

void _start(void)
{
    __builtin_unreachable();
}

#endif
