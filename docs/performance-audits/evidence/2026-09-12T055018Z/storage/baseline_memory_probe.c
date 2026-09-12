#include "/workspace/scratch/d28011d09620/buster-168/docs/audits/2026-09-06/evidence/runtime.h"
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/compiler/ir/ir.c>
static u64 audit_key_probes;
#include "ebpf_baseline_instrumented.c"
int main(void)
{
    audit_initialize();
    printf("context_bytes=%zu record_bytes=%zu\n", sizeof(EbpfContext), sizeof(EbpfSymbolRecord));
    u32 sizes[] = {0, 1, 8, 9, 64, 65, 1024, 10240};
    for (u32 item = 0; item < sizeof(sizes) / sizeof(*sizes); item += 1)
    {
        u32 count = sizes[item];
        IrProgram program = {.arena = program_state->arena};
        program.symbols.symbols = arena_allocate(program.arena, IrSymbol, count);
        program.symbols.count = count;
        program.symbols.capacity = count;
        for (u32 index = 0; index < count; index += 1)
            program.symbols.symbols[index] = (IrSymbol){.name = S8("witness"), .kind = IR_SYMBOL_FUNCTION, .linkage = IR_LINKAGE_EXTERNAL};
        EbpfContext context = {.arena = program.arena, .program = &program};
        u64 before = program.arena->position;
        bool success = ebpf_initialize_symbols(&context);
        printf("symbols=%u arena_bytes=%llu valid=%d\n", count, (unsigned long long)(program.arena->position - before), success && context.symbol_count == count);
    }
    return 0;
}
