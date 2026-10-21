// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An executable's inputs from the C runtime: what executable_write takes as plain data, from the runtime (runtime.c)
// and from binding (bind.c) a program's link against it. A dynamic PIE's (ExecutableDynamic, dynamic_inputs;
// docs/object-writer-and-linker.md, "The runtime snapshot") are:
//   - the program interpreter that GCC's driver names for x86-64 glibc (GLIBC_DYNAMIC_LINKER64 in
//     gcc/config/i386/linux64.h), which it passes to the linker with -dynamic-linker;
//   - libc.so.6's soname and its version definitions, but the one of its own name (VER_FLG_BASE), and among them
//     GLIBC_ABI_DT_RELR, with which the executable's RELATIVE relocations are RELR's;
//   - Scrt1.o's .note.ABI-tag;
//   - crti.o's _init and _fini, as the link's symbols of those names, for DT_INIT and DT_FINI, as GNU ld and lld find
//     them by name;
//   - binding's exports.
// A static executable's with the C runtime (ExecutableRuntime, dynamic_static_inputs) are crt1.o's ABI note and the
// link's __tls_get_addr. A static PIE's (dynamic_static_pie_inputs) are both: an ExecutableDynamic without an
// interpreter, a library, versions or exports, with rcrt1.o's ABI note and crti.o's _init and _fini, and the static
// executable's ExecutableRuntime.
//
// main.c includes this file after bind.c and executable.c.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define BUSTER_DYNAMIC_INTERPRETER "/lib64/ld-linux-x86-64.so.2"
// The version whose definition says that the library's loader takes DT_RELR (glibc 2.36 and later), which an
// executable with .relr.dyn needs.
#define BUSTER_DYNAMIC_RELR_VERSION "GLIBC_ABI_DT_RELR"
static const char dynamic_interpreter[] = BUSTER_DYNAMIC_INTERPRETER;

// The link's symbol of one of the runtime's global names, or 0: through the runtime's index of them, and binding's
// symbol for each.
static SymbolId dynamic_global(const Runtime* runtime, const Bind* bind, const char* name)
{
    const uint64_t length = strlen(name);
    const uint32_t entry = runtime_find_global_name(&runtime->global_names, (const uint8_t*)name, (uint32_t)length, link_hash((const uint8_t*)name, length));
    return entry != 0 ? bind->name_symbols[entry - 1] : 0;
}

// Fills `inputs` for a link that `bind` bound against the static runtime `runtime` (runtime_import_static): crt1.o's
// ABI note, which the executable repeats, and the link's __tls_get_addr, which nothing defines (bind.c leaves it to the
// writer, which relaxes its calls away), or 0 if the link has none.
static __attribute__((unused)) void dynamic_static_inputs(ExecutableRuntime* inputs, const Runtime* runtime, const Bind* bind)
{
    *inputs = (ExecutableRuntime){
        .note = runtime->abi_note,
        .tls_get_addr = dynamic_global(runtime, bind, "__tls_get_addr"),
    };
}

// Fills `dynamic` and `inputs` for a link that `bind` bound against a static PIE's runtime (runtime_import_static_pie),
// which executable_write takes together for a static PIE: no interpreter, library, versions or exports, rcrt1.o's ABI
// note, and crti.o's _init and _fini for DT_INIT and DT_FINI, as GNU ld and lld give a static PIE them; and the link's
// __tls_get_addr, as for a static executable.
static __attribute__((unused)) void dynamic_static_pie_inputs(ExecutableDynamic* dynamic, ExecutableRuntime* inputs, const Runtime* runtime, const Bind* bind)
{
    dynamic_static_inputs(inputs, runtime, bind);
    *dynamic = (ExecutableDynamic){
        .note = runtime->abi_note,
        .init = dynamic_global(runtime, bind, "_init"),
        .fini = dynamic_global(runtime, bind, "_fini"),
    };
}

// Fills `dynamic` for a link that `bind` bound against `runtime`, its versions pushed from `arena`. False if the arena
// has no room for them.
static __attribute__((unused)) bool dynamic_inputs(ExecutableDynamic* dynamic, const Runtime* runtime, const Bind* bind, Arena* arena)
{
    const ImportLibrary* libc = &runtime->libc;
    ExecutableVersion* versions = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableVersion, (uint64_t)libc->version_count + 1);
    uint32_t count = 0;
    uint16_t relr_version = 0;
    for (uint32_t v = 0; versions != 0 && v < libc->version_count; v += 1)
    {
        const ImportVersion* version = &libc->versions[v];
        versions[count] = (ExecutableVersion){
            .name = version->name,
            .hash = version->hash,
            .index = version->index,
        };
        count += version->base ? 0 : 1;
        relr_version = !version->base && import_equal(version->name, BUSTER_DYNAMIC_RELR_VERSION) ? version->index : relr_version;
    }
    *dynamic = (ExecutableDynamic){
        .interpreter = {
            .pointer = (char*)dynamic_interpreter,
            .length = sizeof(dynamic_interpreter) - 1,
        },
        .needed = libc->soname,
        .versions = versions,
        .version_count = count,
        .note = runtime->abi_note,
        .init = dynamic_global(runtime, bind, "_init"),
        .fini = dynamic_global(runtime, bind, "_fini"),
        .exports = bind->exports,
        .export_count = bind->export_count,
        .relr_version = relr_version,
    };
    return versions != 0;
}
