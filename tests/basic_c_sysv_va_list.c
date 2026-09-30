// Public System V objects, not a padded compiler-private cursor. The same
// definitions are compiled independently by the host in the differential pair.
#include <stdarg.h>

#if defined(__x86_64__) && !defined(_WIN32)
struct SysvVaBox
{
    va_list ap;
    volatile unsigned long long guard;
};
struct SysvVaArray
{
    va_list ap[2];
    volatile unsigned long long guard;
};
typedef int SysvVaReader(va_list, int);
typedef int SysvVaCopier(va_list*, va_list, int);
typedef int SysvVaExternal(va_list*, int, ...);
#define SYSV_VA_GUARD 0x736576656e747931ull
#define SYSV_VA_VALUES 1ll, 1.5, 2ll, 2.5, 3ll, 3.5, 4ll, 4.5, 5ll, 5.5, 6ll, 6.5, \
                       7ll, 7.5, 8ll, 8.5, 9ll, 9.5, 10ll, 10.5, 11ll, 11.5, 12ll, 12.5, 99ll

int sysv_va_layout(void)
{
    struct SysvVaBox box;
    struct SysvVaArray array;
    int bad = sizeof(va_list) != 24 || _Alignof(va_list) != 8;
    bad |= sizeof(array.ap) != 48 || sizeof(box) != 32 || sizeof(array) != 56;
    bad |= __builtin_offsetof(struct SysvVaBox, guard) != 24;
    bad |= __builtin_offsetof(struct SysvVaArray, guard) != 48;
    bad |= (char*)&box.guard - (char*)&box.ap != 24;
    bad |= (char*)&array.ap[1] - (char*)&array.ap[0] != 24;
    return bad;
}

int sysv_va_read(va_list ap, int first)
{
    int bad = 0;
    for (int index = first; index <= 12; index += 1)
    {
        bad |= va_arg(ap, long long) != index;
        bad |= va_arg(ap, double) != index + 0.5;
    }
    bad |= va_arg(ap, long long) != 99;
    return bad;
}

int sysv_va_copy_to(va_list* destination, va_list source, int first)
{
    va_copy(*destination, source);
    int bad = sysv_va_read(*destination, first);
    va_end(*destination);
    return bad;
}

// Construction and va_end execute in the compiler opposite the owner of the
// destination. A native owner places its guard at byte 24 independently of
// Buster's layout, so an oversized write cannot hide behind matching metadata.
int sysv_va_external(va_list* destination, int marker, ...)
{
    va_start(*destination, marker);
    int bad = marker != 23;
    bad |= sysv_va_read(*destination, 1);
    va_end(*destination);
    return bad;
}

int sysv_va_call_external(SysvVaExternal* external)
{
    struct SysvVaBox box;
    box.guard = SYSV_VA_GUARD;
    int bad = external(&box.ap, 23, SYSV_VA_VALUES);
    bad |= box.guard != SYSV_VA_GUARD;
    return bad;
}

int sysv_va_produce(SysvVaReader* reader, SysvVaCopier* copier, int first, ...)
{
    struct SysvVaBox source;
    struct SysvVaBox copy;
    source.guard = SYSV_VA_GUARD;
    copy.guard = SYSV_VA_GUARD;
    va_start(source.ap, first);
    int bad = 0;
    // Copy once with registers available and once after both pools exhaust.
    for (int index = 1; index < first; index += 1)
    {
        bad |= va_arg(source.ap, long long) != index;
        bad |= va_arg(source.ap, double) != index + 0.5;
    }
    bad |= copier(&copy.ap, source.ap, first);
    bad |= copy.guard != SYSV_VA_GUARD;
    // The copy must not advance its source. The by-value public array
    // parameter must instead advance the producer's original cursor.
    bad |= reader(source.ap, first);
    va_end(source.ap);
    bad |= source.guard != SYSV_VA_GUARD;
    return bad;
}

int sysv_va_named(SysvVaReader* reader, long long a, long long b, long long c,
                  long long d, long long e, long long f, long long g,
                  double h, double i, double j, double k, double l,
                  double m, double n, double o, double p, int marker, ...)
{
    struct SysvVaBox box;
    box.guard = SYSV_VA_GUARD;
    va_start(box.ap, marker);
    int bad = a + b + c + d + e + f + g != 28 || h + i + j + k + l + m + n + o + p != 45;
    bad |= marker != 23;
    bad |= reader(box.ap, 1);
    va_end(box.ap);
    bad |= box.guard != SYSV_VA_GUARD;
    return bad;
}

int sysv_va_places(int marker, ...)
{
    struct SysvVaArray lists;
    struct SysvVaBox member;
    va_list* pointer = &member.ap;
    lists.guard = SYSV_VA_GUARD;
    member.guard = SYSV_VA_GUARD;
    int index = 0;
    va_start(lists.ap[index++], marker);
    va_copy(*pointer, lists.ap[0]);
    va_copy(lists.ap[index++], *pointer);
    // Ending the first element must not overwrite the next one's offsets.
    int ended = 0;
    va_end(lists.ap[ended++]);
    int bad = ended != 1;
    bad |= sysv_va_read(lists.ap[1], 1);
    bad |= sysv_va_read(member.ap, 1);
    va_end(*pointer);
    va_end(lists.ap[1]);
    bad |= index != 2 || lists.guard != SYSV_VA_GUARD || member.guard != SYSV_VA_GUARD;
    va_start(member.ap, marker);
    bad |= sysv_va_read(*pointer, 1);
    va_end(member.ap);
    va_start(*pointer, marker);
    bad |= sysv_va_read(member.ap, 1);
    va_end(*pointer);
    bad |= member.guard != SYSV_VA_GUARD;
    return bad;
}

int sysv_va_suite(SysvVaReader* reader, SysvVaCopier* copier, SysvVaExternal* external)
{
    int bad = sysv_va_layout();
    bad |= sysv_va_produce(reader, copier, 1, SYSV_VA_VALUES);
    bad |= sysv_va_produce(reader, copier, 10, SYSV_VA_VALUES);
    bad |= sysv_va_named(reader, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll,
                         1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 23, SYSV_VA_VALUES);
    bad |= sysv_va_call_external(external);
    bad |= sysv_va_places(23, SYSV_VA_VALUES);
    return bad;
}
#endif

#ifndef SYSV_VA_LIST_NO_MAIN
int main(void)
{
    int bad = 0;
#if defined(__x86_64__) && !defined(_WIN32)
    bad = sysv_va_suite(sysv_va_read, sysv_va_copy_to, sysv_va_external);
#endif
    return bad;
}
#endif
