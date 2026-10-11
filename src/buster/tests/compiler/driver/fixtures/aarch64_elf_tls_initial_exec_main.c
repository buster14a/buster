// Buster-compiled half of the AArch64 initial-exec TLS fixture: it defines the
// thread-locals the foreign object reaches through relaxed initial-exec pairs
// and reports every value those accesses observe.
int printf(char const* format, ...);
int foreign_ie_mix(int const* a, long const* b);
long foreign_ie_store(long const* b, int n);
int foreign_ie_weighted(int const* weights, int count);

__thread int buster_ie_value = 7;
__thread long buster_ie_total = 100;

int main(void)
{
    int a[] = {3, 4, 5};
    long b[] = {10, 20, 30, 40};
    int weights[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int mix = foreign_ie_mix(a, b);
    buster_ie_value += 1;
    long stored = foreign_ie_store(b, 2);
    int weighted = foreign_ie_weighted(weights, 10);
    printf("TLS_IE_RUNTIME mix=%d stored=%ld total=%ld weighted=%d\n", mix, stored, buster_ie_total, weighted);
    return mix != 64 || stored != 180 || buster_ie_total != 150 || weighted != 339;
}
