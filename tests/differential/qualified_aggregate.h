#ifndef QUALIFIED_AGGREGATE_H
#define QUALIFIED_AGGREGATE_H

struct QualifiedSmall
{
    unsigned char bytes[3];
};

struct QualifiedLarge
{
    unsigned long long a, b, c;
};

struct QualifiedNested
{
    volatile int* pointer;
    const unsigned char* bytes;
};

typedef int (*QualifiedFunction)(struct QualifiedSmall);

int qualified_mutate(volatile struct QualifiedSmall value);
int qualified_read(const volatile struct QualifiedSmall value);
int qualified_const(const struct QualifiedSmall value);
unsigned long long qualified_large(volatile struct QualifiedLarge value, unsigned delta);
int qualified_nested(struct QualifiedNested value);
int qualified_loop(struct QualifiedSmall value, unsigned count);
int qualified_call_host(struct QualifiedSmall value);
int qualified_host_mutate(volatile struct QualifiedSmall value);
int qualified_host_read(const volatile struct QualifiedSmall value);

#endif
