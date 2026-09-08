#pragma once
#include <buster/lib/compiler/codegen/machine.h>

// Opt-in bootstrap evidence. Version 1 uses little-endian u64 scalars and
// length-prefixed byte strings, never pointers, padding or arena capacities.
// A checked, buffered writer bounds memory independently of translation size.
#define BOOTSTRAP_TRACE_BUFFER_SIZE (64u * 1024u)
#define BOOTSTRAP_TRACE_END 0x31444e4552545342ull

typedef struct BootstrapTrace BootstrapTrace;
struct BootstrapTrace
{
    void* stream;
    u8* buffer;
    u64 used;
    bool failed;
    bool invalid_mir;
    String8 invalid_function;
    MachineVerifyResult invalid_validation;
};

BUSTER_F_DECL BootstrapTrace bootstrap_trace_open(Arena* arena, String8 path, String8 kind);
BUSTER_F_DECL void bootstrap_trace_u64(BootstrapTrace* trace, u64 value);
BUSTER_F_DECL void bootstrap_trace_string(BootstrapTrace* trace, String8 value);
BUSTER_F_DECL bool bootstrap_trace_close(BootstrapTrace* trace);
BUSTER_F_DECL void bootstrap_trace_ir(BootstrapTrace* trace, IrProgram* program, IrModule* module);
BUSTER_F_DECL void bootstrap_trace_machine(BootstrapTrace* trace, IrFunction* function, MachineSelectResult* selected);
