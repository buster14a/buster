#pragma once

#include <buster/lib/compiler/object/object.h>

typedef enum JitError
{
    JIT_ERROR_NONE,
    JIT_ERROR_INVALID_INPUT,
    JIT_ERROR_FOREIGN_TARGET,
    JIT_ERROR_CAPACITY,
    JIT_ERROR_EXECUTABLE_MEMORY,
    JIT_ERROR_PROTECTION,
    JIT_ERROR_UNRESOLVED_IMPORT,
    JIT_ERROR_TLS_UNSUPPORTED,
    JIT_ERROR_UNSUPPORTED_RELOCATION,
    JIT_ERROR_EXTERNAL_DATA,
    JIT_ERROR_SYMBOL_BOUNDS,
    JIT_ERROR_SYMBOL_NOT_FOUND,
    JIT_ERROR_BINDING_KIND,
    JIT_ERROR_INVALID_BINDING,
    JIT_ERROR_COUNT,
} JitError;

// Checked, architecture-independent word patching for Darwin AArch64 PAGE
// relocations.  The JIT uses this helper after resolving the target address;
// exposing the byte-level operation also lets host-side tests cover the
// AArch64 encoding rules without requiring an AArch64 executable host.
BUSTER_F_DECL bool jit_apply_aarch64_mach_page_relocation(ObjectRelocationKind kind, u8* patch, u64 place, u64 target, s64 addend);

typedef struct JitHostBinding JitHostBinding;
struct JitHostBinding
{
    String8 name;
    void* address;
    ObjectSymbolKind kind;
};

typedef struct JitOptions JitOptions;
struct JitOptions
{
    // Read only during jit_link_object. On binding failure the returned
    // failing_symbol can borrow a binding name while its diagnostic is read.
    JitHostBinding const* bindings;
    u32 binding_count;
    u32 reserved;
};

typedef struct JitProgram JitProgram;
// Single-owner executable mapping. Do not copy a successfully linked program:
// symbol addresses remain valid only until release, and release must not race
// code executing from the mapping. Bound host targets, including imported data
// addresses, must remain valid for every execution that can reach them.
struct JitProgram
{
    // Primary mapping, followed by an optional contiguous second mapping on
    // Apple Silicon macOS. Code uses MAP_JIT there while read-only and mutable
    // sections stay in a normal mapping so the per-thread JIT write-protect
    // mode cannot make data immutable while generated code executes.
    void* allocation_base;
    u64 allocation_size;
    void* auxiliary_allocation_base;
    u64 auxiliary_allocation_size;
    u64 executable_size;
    void* section_addresses[OBJECT_SECTION_COUNT];
    u64 section_sizes[OBJECT_SECTION_COUNT];
    // Borrowed: the ObjectFile header, its sections and symbols, and all
    // symbol-name storage must outlive this program and every symbol lookup.
    ObjectFile const* object;
    // Borrowed from object, options, or lookup-name storage; the originating
    // storage must outlive every diagnostic that reads this field.
    String8 failing_symbol;
    JitError error;
};

// The host process must permit JIT executable memory. Apple Silicon macOS code
// starts in a nominal RWX MAP_JIT mapping and uses
// pthread_jit_write_protect_np around construction; the call assumes write
// protection is enabled on entry and restores it before returning. Intel macOS
// retains a RW MAP_JIT mapping and finalizes its code pages RX. Hardened
// processes still need the matching entitlement/policy, and denial is reported
// as an executable-memory/protection error. The whole-program CLI creates one
// region. Repeated direct API use is unsupported under Apple's public
// one-MAP_JIT-region contract, as are single-jit and JIT write-function
// allowlist policies.
BUSTER_F_DECL JitProgram jit_link_object(ObjectFile const* object, JitOptions options);
BUSTER_F_DECL void* jit_program_symbol(JitProgram* program, String8 name);
BUSTER_F_DECL void jit_program_release(JitProgram* program);
BUSTER_F_DECL String8 jit_error_string(JitError error);
