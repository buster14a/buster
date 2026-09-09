#ifndef DIFFERENTIAL_ALIGNED_H
#define DIFFERENTIAL_ALIGNED_H
struct __attribute__((aligned(32))) A32 { unsigned long long v[4]; };
struct __attribute__((aligned(64))) A64 { unsigned long long v[8]; };
struct __attribute__((aligned(128))) A128 { unsigned long long v[16]; };
int observe(void const *p, unsigned mask);
int aligned32(struct A32 value);
int aligned64(struct A64 value);
int aligned128(struct A128 value);
int exhausted(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d,
              unsigned long long e, unsigned long long f, struct A64 value);
#endif
