// External compiler fixture for non-adjacent AArch64 initial-exec TLS (#2582).
// Built with -O2 -ftls-model=initial-exec -fno-pie, every access to the two
// thread-locals the Buster-compiled aarch64_elf_tls_initial_exec_main.c
// defines is an ADRP/LDR pair (541/542). Clang schedules `mrs TPIDR_EL0` and
// the surrounding loads between the halves, so the fixed-address Buster link
// must pair them by register and prove the enclosed words leave it alone.
extern __thread int buster_ie_value;
extern __thread long buster_ie_total;

int foreign_ie_mix(int const* a, long const* b)
{
    return a[0] * a[1] + buster_ie_value + a[2] + (int)b[3];
}

long foreign_ie_store(long const* b, int n)
{
    long t = b[0] + b[1] * n;
    buster_ie_total += t;
    return buster_ie_total + b[2];
}

int foreign_ie_weighted(int const* weights, int count)
{
    int sum = weights[0] + buster_ie_value;
    for (int index = 1; index < count; index += 1)
    {
        sum += weights[index] * index;
    }
    return sum;
}
