#include "/workspace/scratch/d28011d09620/buster-168/docs/audits/2026-09-06/evidence/runtime.h"
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/compiler/ir/ir.c>
static u64 audit_key_probes;
#include "ebpf_baseline_instrumented.c"
int main(void)
{
    audit_initialize();
    u32 sizes[] = {0, 1, 8, 9, 16, 17, 32, 33, 64, 65, 1024};
    int failed = 0;
    for (u32 item = 0; item < sizeof(sizes) / sizeof(*sizes); item += 1)
    {
        u32 count = sizes[item];
        EbpfContext context = {.arena = program_state->arena};
        audit_key_probes = 0;
        for (u32 key = 0; key < count; key += 1)
            ebpf_add_symbol_record(&context, key * 2, S8("witness"), 0, EBPF_STB_LOCAL, EBPF_STT_OBJECT, false);
        u64 expected = (u64)count * (count - (count != 0)) / 2;
        if (audit_key_probes != expected || context.symbol_count != count) failed = 1;
        printf("symbols=%u insertion_comparisons=%llu expected=%llu\n", count, (unsigned long long)audit_key_probes, (unsigned long long)expected);
    }
    return failed;
}
