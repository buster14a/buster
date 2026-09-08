#include "basic_c_parameter_alignment.h"

// A separate host-compiled observer prevents an optimizer from answering the
// address-alignment question from the parameter's declared type alone.
int parameter_aligned32(struct ParameterAligned32 value)
{
    int failed = parameter_alignment_observe(&value, 32, 11, 14);
    value.values[0] += 100;
    value.values[3] += 100;
    failed |= parameter_alignment_observe(&value, 32, 111, 114);
    return failed;
}

int parameter_aligned64(struct ParameterAligned64 value)
{
    int failed = parameter_alignment_observe(&value, 64, 21, 28);
    value.values[0] += 100;
    value.values[7] += 100;
    failed |= parameter_alignment_observe(&value, 64, 121, 128);
    return failed;
}

int parameter_aligned_after_integers(int a, int b, int c, int d, int e, int f, int g, struct ParameterAligned64 value, int tail)
{
    return parameter_alignment_observe(&value, 64, 21, 28) || a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6 || g != 7 || tail != 9;
}

int parameter_aligned_after_vectors(double a, double b, double c, double d, double e, double f, double g, double h, double i,
                                    struct ParameterAligned32 value, double tail)
{
    return parameter_alignment_observe(&value, 32, 11, 14) || a != 1.0 || b != 2.0 || c != 3.0 || d != 4.0 || e != 5.0 || f != 6.0 ||
           g != 7.0 || h != 8.0 || i != 9.0 || tail != 10.0;
}

// Exhaust both register banks in the same call, with two aligned stack
// parameters followed by a scalar. Separate-bank cases above remain covered.
int parameter_aligned_after_both(int a, int b, int c, int d, int e, int f, int g,
                                double h, double i, double j, double k, double l, double m, double n, double o, double p,
                                struct ParameterAligned32 first, struct ParameterAligned64 second, int tail)
{
    int failed = parameter_alignment_observe(&first, 32, 11, 14) || parameter_alignment_observe(&second, 64, 21, 28) ||
                 a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6 || g != 7 || h != 1.0 || i != 2.0 || j != 3.0 ||
                 k != 4.0 || l != 5.0 || m != 6.0 || n != 7.0 || o != 8.0 || p != 9.0 || tail != 10;
    first.values[0] += 100;
    first.values[3] += 100;
    second.values[0] += 100;
    second.values[7] += 100;
    failed |= parameter_alignment_observe(&first, 32, 111, 114) || parameter_alignment_observe(&second, 64, 121, 128);
    return failed;
}
