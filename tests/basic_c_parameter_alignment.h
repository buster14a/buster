#ifndef BASIC_C_PARAMETER_ALIGNMENT_H
#define BASIC_C_PARAMETER_ALIGNMENT_H

struct __attribute__((aligned(32))) ParameterAligned32 { unsigned long long values[4]; };
struct __attribute__((aligned(64))) ParameterAligned64 { unsigned long long values[8]; };

int parameter_aligned32(struct ParameterAligned32 value);
int parameter_aligned64(struct ParameterAligned64 value);
int parameter_aligned_after_integers(int a, int b, int c, int d, int e, int f, int g, struct ParameterAligned64 value, int tail);
int parameter_aligned_after_vectors(double a, double b, double c, double d, double e, double f, double g, double h, double i,
                                    struct ParameterAligned32 value, double tail);
int parameter_aligned_after_both(int a, int b, int c, int d, int e, int f, int g,
                                double h, double i, double j, double k, double l, double m, double n, double o, double p,
                                struct ParameterAligned32 first, struct ParameterAligned64 second, int tail);
int parameter_alignment_observe(void const* pointer, unsigned alignment, unsigned long long first, unsigned long long last);

#endif
