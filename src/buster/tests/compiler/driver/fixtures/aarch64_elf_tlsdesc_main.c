// Buster-compiled half of the AArch64 TLS relaxation fixture: it defines the
// thread-local the foreign object reaches through a descriptor and reports
// every value the foreign descriptor accesses observe.
int printf(char const* format, ...);
int foreign_tls_get(void);
int foreign_tls_add(int delta);

__thread int buster_tls_value = 7;

int main(void)
{
    int foreign = foreign_tls_get();
    buster_tls_value += 1;
    int sum = foreign_tls_add(5);
    int after = foreign_tls_get();
    printf("TLSDESC_RUNTIME foreign=%d buster=%d sum=%d after=%d\n", foreign, buster_tls_value, sum, after);
    return foreign != 42 || buster_tls_value != 8 || sum != 55 || after != 47;
}
