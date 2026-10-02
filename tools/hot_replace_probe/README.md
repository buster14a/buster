# Safe-point native C counter demonstration

This opt-in application extends the quiescent lifecycle from #1926's completed
probe at `b6bc250d9705f158c332a4881a19988aa78bf500`. It uses the existing
`ide cc` C frontend, validated canonical IR, native object writer, `object_read`,
and object JIT. The host is an application consumer, with its own typed dispatch;
it adds no compiler callbacks or parallel runtime. #2231 owns this slice.

The explicit support envelope is **Linux x86-64, libc, one application thread,
trusted module source using this exact contract header**. It is a controlled
safe-point demonstration, not transparent hot reload for arbitrary C programs.

## Build and run

Run these commands from the repository root on an authorized execution host.
Use a fresh build directory; `generate` deletes its selected directory.

```sh
./build.sh generate --build-directory build/hot-reload --config Release \
    --no-fuzz --no-lto --no-include-tests --linker DEFAULT -- \
    -DBUSTER_HOT_RELOAD_DEMO=ON
./build.sh build --build-directory build/hot-reload --config Release \
    -t test_hot_reload_lifecycle
build/hot-reload/Release/hot_reload \
    "$(pwd)/build/hot-reload/Release/ide" tools/hot_replace_probe/counter.c
```

The target is off by default and excluded from the default build. The small host
always compiles split; the compiler retains the configured unity/split setting.
Section garbage collection discards unused object-conversion/debug producers
from the reused loader sources. No application module is attached to `ide`.

Enter `s` then Return: the module updates the host's counter. Edit
`PILOT_STEP` in `counter.c` from `1ULL` to `2ULL`, save, and enter `r`.
Enter `s` again: total advances by two while calls continues increasing in the
same process. Enter `q`, or EOF, to release both slots and the private workspace.
Use one completed command per line; there is no automatic filesystem watcher.

`r` performs a whole-module compilation in a child process, followed by
candidate-only read/validation/loading and quiescent publication. The host
passes bounded argv directly through `os_process_spawn`, without a shell or PATH
search, with an explicit empty child environment. The compiler path identifies
the already-built Buster compiler; source edits must be stable for each build.
The compiler child has a 60-second deadline and the platform's bounded capture.
Every attempt removes the private old object output before compilation, so
compiler failure cannot accidentally reload stale bytes.

## ABI, symbols, imports and persistent ownership

`pilot_contract.h` specifies
`unsigned long long pilot_step(PilotState *)`. Host-owned `PilotState` contains
only two unsigned 64-bit counters. The function returns the updated total.
Its argument is borrowed for the entire call and must not escape.

The read-only, pointer-free `pilot_descriptor` contains a magic, ABI version,
state-schema version, state size/alignment and both field offsets. Preparation
requires exactly one descriptor, exact values and size, no overlapping
relocations, and exactly one defined nonempty FUNCTION symbol `pilot_step` in
TEXT. A different ABI/schema/layout is rejected before candidate execution.
No migration is supplied; every incompatible change requires a fresh session.
Changing the shared header changes the contract and requires rebuilding the
host as well as the module.

The descriptor is an assertion by the trusted module source; neither it nor
symbol names infer the C signature, calling convention, semantics, or memory
safety of arbitrary native code. Modules deliberately lying about their ABI,
corrupting state, trapping or failing to return are outside the contract.
Execution is native and unsandboxed.

Only the process-lifetime function `pilot_host_delta` is bound. There is no
implicit symbol lookup, libc binding, cross-module import, or old-generation
import. All other imports retain the loader's actual failure. Every candidate
uses the existing target, relocation, section and W^X validation unchanged.
Code is patched writable, then finalized RX; descriptors are read-only.

Nonempty writable DATA/ZERO and constructor/destructor sections are refused.
TLS reaches the existing loader's explicit refusal. Modules must not create
threads, register callbacks/signals/exit handlers, retain state pointers, expose
code or module-data addresses, perform nonlocal exits, or leave asynchronous
work. No debugger or unwind metadata registration is provided.

## Safe points and code lifetime

Two stable slots own their own object arenas and noncopied `JitProgram`.
Input bytes, section/symbol/name storage and the ObjectFile header remain alive
until code is released and no lookup can reach them. Internal relocations retain
the generation they were loaded into. Symbol identity is name plus generation;
the host resolves a fresh entry for every candidate and never patches old code.

| Operation | Preconditions and ownership |
|---|---|
| Prepare | Only EMPTY/RELEASED slots; old selector and persistent state untouched. |
| Acquire/call/drop | One typed, noncopyable lease pins the selected version until the entire call returns; drop clears the callable before removing its pin. |
| Publish | Candidate READY and incumbent has zero pins; selector changes once between commands. |
| Reclaim | Slot is absent from the active selector and has zero pins; clear entry, release code, then destroy its arena. |
| Failed arena release | Keep RELEASE_PENDING owner; refuse prepare/publication, permit cleanup retry. |
| Shutdown | Refuse with an active lease; otherwise clear selector and use the same reclamation checks. |

A module inside a host import is still active because its return address points
into the old mapping. The lifecycle test attempts publication and reclamation
from that import and requires refusal. The protocol is serial; pins are not
atomics and provide no concurrent-reader promise. Raw entries/leases must not
be copied or retained outside this application boundary.

Compile, read, object, descriptor, entry, import, TLS or protection failure
affects only the candidate; the previous version stays selected and callable.
Failures do not roll back effects of executing faulty native code. Resource
exhaustion can still terminate through the existing arena API. If compiler
process-tree cleanup/ownership cannot be proven, further compiler admission
stops, the private workspace is retained for inspection, and shutdown fails
explicitly; ordinary counter stepping can still use the incumbent.

## Tests and limits

`test_hot_reload_lifecycle` runs the standalone host with `--self-test` and the
just-built compiler. It generates first-party C fixtures in an owned private
directory, compiles them using strict mir-stack with canonical/codegen
verification, and observes actual module execution against independent counter
arithmetic. It covers repeated edit/build/reload, malformed C and objects,
missing compiler/object/import/entry, ABI/schema/size/field-order changes,
writable globals, TLS, pinned replacement/shutdown refusal, the live import
return address, double-drop, release failure/retry, and idempotent shutdown.

Linux `/proc/self/maps` independently verifies RX code, read-only descriptor
storage, and unmapping after replacement. Internal counters enforce a peak of
two code images and zero live slot-owned images/bytes at shutdown. Each input
object is bounded to 1 MiB, each arena reserves 16 MiB, with no version-arena
pooling. These are slot bounds, not whole-process RSS: shared compiler/metadata
caches and thread scratch have separate owners. Recoverable loader failures
retain existing validation; this is not arbitrary malicious-object isolation.

Hosted checks exercise Release with a unity compiler and sanitized Debug with
a split compiler. Sanitizers instrument the host/loader/compiler, not generated
module instructions. Only `probe_call` excludes Clang's `function` check:
that check probes a host-compiler metadata prefix absent from Buster JIT code.
ASan and the remaining UBSan checks stay enabled.

Other operating systems, architectures, host toolchains and arbitrary
multithreaded programs are unqualified for this application. Repeated macOS
MAP_JIT use is excluded by the existing one-region contract. This slice changes
no compiler self-host or native-retirement acceptance rules. Hosted correctness
results and diagnostic run duration are not hardware/performance acceptance;
no 9700X measurement is needed or claimed.

## Provenance and technical references

Implementation lineage is first-party Buster: main inspected at
`6fc08ec475694aeb3b1062c1d390483a69668508` and the prior probe at
`b6bc250d9705f158c332a4881a19988aa78bf500`. The [repository README](../../README.md)
and [license inventory](../../LICENSES/README.md) do not grant a first-party
license; that remains unresolved under #621. No third-party code or test assets
were incorporated, and no dependencies were added.

The sanitizer boundary was checked against LLVM's
[CGExpr.cpp](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/lib/CodeGen/CGExpr.cpp)
at `3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff` (llvmorg-18.1.8):
indirect-call checks load a prefix before the entry address. Its verified
[Clang license](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/LICENSE.TXT)
is **Apache-2.0 WITH LLVM-exception**. This is technical-contract research only;
no LLVM implementation was copied.

Exact current checks, failed attempts, revisions and remaining gates live on
[#2239](https://github.com/buster14a/buster/pull/2239), not in this support contract.
