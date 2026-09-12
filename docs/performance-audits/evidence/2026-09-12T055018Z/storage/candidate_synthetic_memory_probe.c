#include "/workspace/scratch/d28011d09620/buster-168/docs/audits/2026-09-06/evidence/runtime.h"
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/compiler/ir/ir.c>
#include <buster/lib/compiler/ebpf/ebpf.c>
#include <buster/lib/target.c>
#include "candidate_synthetic_memory_internal.h"
void buster_test_error_arguments(UnitTestArguments* args, u32 line, String8 function, String8 file, String8 format, ...)
{
    printf("assertion failed at line %u\n", line);
}
static void probe_show(UnitTestArguments* args, String8 format, ...)
{
    printf("diagnostic emitted\n");
}
int main(void)
{
    audit_initialize();
    UnitTestArguments arguments = {.arena = program_state->arena, .show = probe_show};
    UnitTestResult result = codegen_test_ebpf_string_symbols(&arguments);
    printf("synthetic ELF assertions: %llu/%llu\n", (unsigned long long)result.succeeded_test_count, (unsigned long long)result.test_count);
    return result.succeeded_test_count != result.test_count;
}
