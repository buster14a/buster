#ifndef BASIC_C_OVERALIGNED_STACK_SHAPES_H
#define BASIC_C_OVERALIGNED_STACK_SHAPES_H

struct __attribute__((aligned(32))) stack_aligned32
{
    unsigned long long values[4];
};

struct __attribute__((aligned(64))) stack_aligned64
{
    unsigned long long values[8];
};

typedef unsigned char stack_vector32 __attribute__((vector_size(32)));
typedef unsigned char stack_vector64 __attribute__((vector_size(64)));

extern int stack_observe_alignment(void const* pointer, unsigned long long alignment);
extern int stack_check32(struct stack_aligned32 value);
extern int stack_check64(struct stack_aligned64 value);
extern int stack_check64_after_scalars(int a, int b, int c, int d, int e, int f, int g, struct stack_aligned64 value);
extern int stack_check_variadic(struct stack_aligned64 value, int marker, ...);
extern int stack_check_variadic_tail(int marker, ...);
extern int stack_check_vector32(double a, double b, double c, double d, double e, double f, double g, double h, stack_vector32 value);
extern int stack_check_vector64(double a, double b, double c, double d, double e, double f, double g, double h, stack_vector64 value);

#endif
