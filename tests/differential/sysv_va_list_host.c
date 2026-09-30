#define _GNU_SOURCE 1
#if defined(__x86_64__) && !defined(_WIN32)
#if __has_include(<sys/mman.h>) && __has_include(<unistd.h>)
#include <sys/mman.h>
#include <unistd.h>
#define SYSV_VA_HAS_PAGE_GUARD 1
#endif
// Independent compiler, public stdarg.h, identical scalar expectations.
// No Buster-private declaration describes a native va_list object.
#define SYSV_VA_LIST_NO_MAIN 1
#define sysv_va_layout host_va_layout
#define sysv_va_read host_va_read
#define sysv_va_copy_to host_va_copy_to
#define sysv_va_external host_va_external
#define sysv_va_call_external host_va_call_external
#define sysv_va_produce host_va_produce
#define sysv_va_named host_va_named
#define sysv_va_places host_va_places
#define sysv_va_suite host_va_suite
#include "../basic_c_sysv_va_list.c"
#undef sysv_va_read
#undef sysv_va_copy_to
#undef sysv_va_external
#undef sysv_va_suite
int sysv_va_read(va_list, int);
int sysv_va_copy_to(va_list*, va_list, int);
int sysv_va_external(va_list*, int, ...);
int sysv_va_suite(SysvVaReader*, SysvVaCopier*, SysvVaExternal*);

// Put the 24-byte source directly against an inaccessible page. Guard bytes
// detect destination overwrites; this independently detects a 32-byte read.
static int host_va_page_boundary(int marker, ...)
{
    int bad;
#if defined(SYSV_VA_HAS_PAGE_GUARD)
    long page_size = sysconf(_SC_PAGESIZE);
    bad = page_size < 24 || (page_size & 7) != 0;
    void* mapping = MAP_FAILED;
    if (!bad)
    {
        mapping = mmap(0, (unsigned long)page_size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        bad = mapping == MAP_FAILED;
    }
    if (!bad)
    {
        bad = mprotect((char*)mapping + page_size, (unsigned long)page_size, PROT_NONE) != 0;
        if (!bad)
        {
            va_list* source = (va_list*)((char*)mapping + page_size - 24);
            struct SysvVaBox destination;
            destination.guard = SYSV_VA_GUARD;
            va_start(*source, marker);
            bad |= sysv_va_copy_to(&destination.ap, *source, 1);
            va_end(*source);
            bad |= destination.guard != SYSV_VA_GUARD;
        }
    }
    if (mapping != MAP_FAILED) { bad |= munmap(mapping, (unsigned long)page_size * 2) != 0; }
#else
    // Cross-object census may not have a target SDK. Permit object creation,
    // but fail closed if such a binary is ever used as a runtime oracle.
    (void)marker;
    bad = 1;
#endif
    return bad;
}

int main(void)
{
    int bad = host_va_suite(sysv_va_read, sysv_va_copy_to, sysv_va_external);
    bad |= sysv_va_suite(host_va_read, host_va_copy_to, host_va_external);
    bad |= host_va_page_boundary(23, SYSV_VA_VALUES);
    return bad;
}

#else
int main(void)
{
    return 0;
}
#endif
