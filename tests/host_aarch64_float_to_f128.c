// Native AArch64 oracle: the host compiler owns FP status/control access.
// BUSTER_F128_HOST_LIBRARY reverses the conversion/observer compilation roles.
#define BUSTER_F128_FENV 1
#ifdef BUSTER_F128_HOST_LIBRARY
#define BUSTER_F128_LIBRARY 1
#else
#define BUSTER_F128_CLIENT 1
#endif
#include "basic_c_aarch64_float_to_f128.c"

unsigned f128_read_status(void)
{
    unsigned long value;
    __asm__ volatile("mrs %0, fpsr" : "=r"(value));
    return (unsigned)value;
}

void f128_write_status(unsigned value)
{
    __asm__ volatile("msr fpsr, %0" : : "r"((unsigned long)value));
}

unsigned f128_read_control(void)
{
    unsigned long value;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return (unsigned)value;
}

void f128_write_control(unsigned value)
{
    __asm__ volatile("msr fpcr, %0" : : "r"((unsigned long)value));
}
