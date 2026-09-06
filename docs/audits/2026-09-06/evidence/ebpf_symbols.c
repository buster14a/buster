#include "runtime.h"
#include <buster/lib/hash.c>
#include <buster/lib/compiler/ir/ir.c>
#include <buster/lib/compiler/ebpf/ebpf.c>
int main(void)
{
    audit_initialize();
    u32 sizes[]={2048,4096,8192,16384};
    for(u32 n=0;n<4;n++)
    {
        EbpfContext context={.arena=program_state->arena};
        u64 before=os_now_microseconds();
        for(u32 key=0;key<sizes[n];key++)ebpf_add_symbol_record(&context,key,S8("witness"),0,EBPF_STB_LOCAL,EBPF_STT_FUNC,false);
        u64 elapsed=os_now_microseconds()-before;
        printf("N=%u key_comparisons=%llu elapsed_us=%llu count=%u\n",sizes[n],(unsigned long long)sizes[n]*(sizes[n]-1)/2,(unsigned long long)elapsed,context.symbol_count);
    }
    return 0;
}
