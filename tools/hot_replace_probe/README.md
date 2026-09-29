# Controlled-host hot replacement probe

Research owner: GitHub issue #1926. This directory is an isolated C diagnostic,
outside production, CMake, CI, and admission policy. It loads this host's own
compiled fixture modules through Buster's existing `object_read`,
`jit_link_object`, `jit_program_symbol`, and `jit_program_release` APIs.
The module compiler remains `ide cc`: C -> validated canonical IR -> native
object. There is no second compiler or generic compiler dispatch mechanism.

This host supports Linux x86-64 only. Its control operations and calls run on
one OS thread. Results from it do not establish concurrent correctness,
cross-platform support, or accepted performance measurements.

## Build and run

Use an already-built trusted `build/Release/ide`. From the repository root,
compile each of `pilot_v1`, `pilot_v2`, `pilot_bad_abi`, `pilot_missing_entry`,
`pilot_missing_import`, `pilot_tls`, `pilot_state_v1`, and `pilot_state_v2`:

```sh
build/Release/ide cc -g0 -fverify-codegen -fregister-allocator=mir-stack \
    -fno-machine-fallback -target x86_64-unknown-linux \
    -c tools/hot_replace_probe/pilot_v1.c -o /tmp/pilot_v1.o
```

Substitute the fixture basename for the other seven objects. The exact source
revision, fixture digests, compiler identity, and commands belong with the
results. These source fixtures and their explicit contract are trusted by the
host. The `pilot_abi` integer is an application contract label; neither that
integer nor the object symbol table proves an arbitrary C function's ABI.

Build the controlled host using existing repository C sources only:

```sh
clang-20 -O1 -g0 -std=c11 -Wall -Wextra -Werror -Wpedantic \
    -Wno-missing-field-initializers \
    -fwrapv -fno-strict-aliasing -funsigned-char \
    -ffunction-sections -fdata-sections \
    -DBUSTER_SINGLE_THREADED=1 -DBUSTER_INCLUDE_TESTS=0 -DBUSTER_UNITY_BUILD=0 \
    -Isrc -Ibuild/generated tools/hot_replace_probe/host.c \
    src/buster/lib/arena.c src/buster/lib/integer.c src/buster/lib/os.c \
    src/buster/lib/string.c src/buster/lib/target.c src/buster/lib/x86_64.c \
    src/buster/lib/hash.c src/buster/lib/byte_writer.c \
    src/buster/lib/compiler/dwarf/dwarf.c \
    src/buster/lib/compiler/assembly/aarch64_encoding.c \
    src/buster/lib/compiler/assembly/x86_64_metadata.c \
    src/buster/lib/compiler/object/object.c src/buster/lib/compiler/jit/jit.c \
    -Wl,--gc-sections -pthread -ldl -lm -o /tmp/buster-hot-replace-host

/tmp/buster-hot-replace-host \
    /tmp/pilot_v1.o /tmp/pilot_v2.o /tmp/pilot_bad_abi.o \
    /tmp/pilot_missing_entry.o /tmp/pilot_missing_import.o /tmp/pilot_tls.o \
    /tmp/pilot_state_v1.o /tmp/pilot_state_v2.o
```

This standalone link uses section garbage collection to retain the needed
object/JIT runtime. It does not change the `ide` target's module registration.
Do not compile these modules with another compiler and label that result a
Buster compilation/JIT composition result. Native emitted relocation kinds
remain untouched; the missing-import case records the actual `JitError` even
when a relocation restriction is reported before import resolution.

## Ownership and transitions

Each `ProbeVersion` owns exactly one object arena and one `JitProgram`. It must
never be copied after construction. Input bytes, section/symbol records, names,
and the `ObjectFile` header stay alive with that version because the JIT borrows
them. Host-provided imported addresses must outlive every reachable
version; the final fixture binds one permanent host function explicitly.
The earlier missing-import fixture supplies no binding. Internal calls retain the object
version's own relocation bindings and never consult the active selector.

The transitions are:

| From | Operation | To / outcome |
|---|---|---|
| EMPTY or RELEASED | Read, parse, policy, isolated JIT link, entry lookup | PREPARING, then READY or REJECTED |
| READY | Publish with no old pins | ACTIVE; prior ACTIVE becomes RETIRED |
| READY | Publish a different ABI mode within an active session | ABI error; neither version changes |
| READY | Quiescent publish with old pins | BUSY; candidate stays READY and old stays ACTIVE |
| READY | Separate versioned publish with old pins | ACTIVE; old becomes RETIRED and its leases stay callable |
| ACTIVE | Acquire typed entry lease | ACTIVE; increase pins |
| ACTIVE or RETIRED | Drop an existing lease | Same state; clear borrowed addresses, then decrease pins |
| RETIRED with pins | Reclaim | BUSY; code and object metadata retained |
| RETIRED, READY, or REJECTED without pins | Release mapping, then arena | RELEASED |
| Reclamation with failed arena release | Retain arena/object owner | RELEASE_PENDING; retry allowed, prepare/publication refused |

The exact publication point is `host->active = candidate` in `probe_publish`.
This assignment is serial and publishes a fully prepared slot. Existing leases
name their original version; acquiring a new lease selects the active version.
There is no claim that a plain pointer store supports concurrent readers.

The reclamation decision requires both absence from `host->active` and zero
pins. The code mapping is released by `jit_program_release`, followed by the
object arena. A lease must not be copied and a raw callable address must not
escape it. Dropping a lease clears its typed callable before decrementing pins.
An untracked escaped function/data address makes this reclamation protocol
invalid; such escapes are rejected by the application contract.

The quiescent experiment holds an old lease, prepares a separate candidate
mapping, verifies BUSY, calls the original lease, drops it, publishes, and
reclaims. The separate versioned experiment holds an old lease across a new
publication, calls both generations, verifies that old reclamation is BUSY,
then drops the final old lease and reclaims. This is logical lifetime overlap,
not simultaneous threads or a live-stack-frame rewrite.

## Failure semantics and restrictions

Read/parse/policy/JIT/entry failure affects only the candidate. A failed
candidate cannot publish. Every failure test calls the old version again and
then releases candidate resources. ABI mismatch is refused before executing
the candidate. Entry conversion requires a defined function in the text
section; lookup and relocation failures preserve their actual JIT diagnostics.
The active session also refuses publication of a correctly tagged candidate
with a different ABI mode. An injected arena-release failure verifies retained
ownership and successful retry; it is a controlled host fault, not an observed
OS failure. The configured project's missing-field-initializer warning
suppression is retained when compiling the existing metadata source.

Module writable globals/statics, constructors, and destructors are rejected.
TLS is allowed through the diagnostic's preflight specifically to exercise
the existing JIT's `JIT_ERROR_TLS_UNSUPPORTED` refusal. The host does not
migrate arbitrary C state, convert ABI changes, patch existing calls, resume
frames in new code, accept unmanaged callbacks, or repair stale addresses.

Object debug/unwind sections can remain in the owned object arena, but this
JIT does not register them with debuggers or unwind runtimes. The pilot prints
their aggregate bytes as unregistered metadata and provides no host stackwalk,
exception/nonlocal-exit, signal-handler, or debugger-registration guarantee.
Future metadata registration would be per version: after executable addresses
are fixed and before publication; unregister only after that version's final
code/address/stackwalker user is gone and before unmapping its owned storage.

The native JIT API does not report executable-unmap failure. Its lifetime
decision can be demonstrated here, but successful OS reclamation is not
independently measured. Arena creation/allocation can terminate the process
on resource exhaustion, so this experiment establishes preservation after
the specified recoverable candidate failures, not whole-process survival
under arbitrary allocation failure or executing faulty candidate code.

The first stateful extension uses the host-owned, fixed-layout `PilotState`
passed explicitly to `int pilot_step(PilotState *, int)`. v1 and v2 update the
same host storage across a quiescent replacement. A mismatched contract label
is rejected with the selector and state unchanged. This retains an existing
state layout; it is not migration. Versioned execution against mutable shared
state is deliberately not exercised.

## Costs and evidence

Default execution prints deterministic correctness checks and actual mechanism
counts: prepared candidates, mapped versions, cumulative mapping bytes,
section/symbol/relocation records, unregistered metadata bytes, publications,
reclamations, and calls. A zero exit code means those host protocol checks
passed on that exact run, not that hot replacement is production-admitted.

`--timings` opts into diagnostic `CLOCK_MONOTONIC` samples for object read,
object parse plus policy, JIT load/finalize plus entry lookup, publication
attempts, reclamation attempts, and ordinary fixture calls. These are phase
sums in a correctness script, affected by timer overhead and tiny samples.
They are not performance acceptance evidence. There are no default timing
calls and no claim of an end-to-end speedup.

Ordinary compilation is a separate external `ide cc` invocation and is not
measured in the host. Replacement preparation consists of object read/decode,
checks, mapping, relocation, protection finalization, and lookup. Selector
publication is a separate operation. Actual wait for an application quiescent
point is workload-dependent and is not measured here. Reclamation is separate
from publication and may be deferred. Ordinary typed calls and their lease
bookkeeping must be compared separately against the controlled host's normal
execution baseline on the dedicated benchpress 9700X before a performance
decision. The standard GitHub runners remain suitable for protocol validation.
