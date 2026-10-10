// External compiler fixture for AArch64 ELF TLS descriptor relaxation (#2582).
// Built with -fPIC -ftls-model=global-dynamic, every access below is a TLSDESC
// sequence (562/563/564/569): to foreign_tls_value, defined here, and to
// buster_tls_value, which the Buster-compiled aarch64_elf_tlsdesc_main.c
// defines. A fixed-address Buster link must relax each to local-exec.
__thread int foreign_tls_value = 42;
extern __thread int buster_tls_value;

int foreign_tls_get(void)
{
    return foreign_tls_value;
}

int foreign_tls_add(int delta)
{
    foreign_tls_value += delta;
    return foreign_tls_value + buster_tls_value;
}
