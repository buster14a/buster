# Pinned apt inputs for external CI consumers

`.github/apt-inputs.json` is the reviewed input lock for the existing
[GPU profiles](gpu-toolchain-validation.md) and real-source qualification
workflow. `tools/ci_apt.py` provisions packages and checks selection only;
builds, GPU artifact acceptance and Lua/throughput admission remain in the
existing native C harnesses. This is the same CI-provisioning-only Python
boundary used by the CUDA and Zig installers, not a second benchmark runner.

## Scope and trust boundary

The lock targets **Ubuntu 26.04 (resolute), amd64**, matching both workflows.
It uses the **2026-09-15 00:00:00 UTC** Ubuntu snapshot, never the unrelated
noble suite from the UEFI lane. Initial named versions are LLVM
`1:18.1.8-20ubuntu8`, SPIRV-Tools `2026.1-1`, readline `8.3-4`, and ncurses
`6.6+20251231-1`. See the individual entries for the exact binary package set.

The GPU group freezes clang/LLVM/LLD executables, the LLVM shared library and
Clang common resource headers/shared libraries, LLVM runtime/linker tools,
and SPIRV-Tools. The readline group freezes development headers/link inputs,
readline's runtime/common data, and the ncurses development/runtime/terminfo
packages consumed by that dependency. No recommended packages are requested.

**This does not freeze the whole hosted image.** The kernel, hosted Clang used
to build Buster in real-source qualification, glibc, GCC support libraries,
and other system dependencies satisfying apt requirements remain hosted-image
inputs. Their installed versions and the actual selected dynamic-library
paths, sizes and SHA-256 values are retained separately as observations.
Existing GPU binary evidence and throughput resource/sysroot/dependency/runtime
manifests are still required. Do not compare benchmark results as equivalent
merely because the named package lock matches; compare those broader identities
and the existing acceptance contract too. There is no performance claim here.

Provisioning creates fresh private apt lists and archive directories, uses only
the snapshot source and its three resolute pockets, and ignores the hosted
source/preference lists and binary apt caches. `Signed-By` requires Ubuntu's
archive keyring; signature, package digest and HTTPS verification stay enabled.
`Check-Valid-Until: no` applies only to the historical snapshot's freshness
window, not to signatures or package authentication. A failed index, missing
exact package build, dependency conflict or selection mismatch is fatal. There
is no retry against live repositories and no unauthenticated fallback.

For an HTTP 5xx response from the exact signed snapshot, provisioning retries
the whole failed apt update or install command twice, after 30 and 60 seconds.
Individual apt transfers also retain their three configured retries. Every
whole-command attempt has its own retained argv, exit status and diagnostics.
Signature errors, missing versions and unrelated failures stop immediately;
an unavailable snapshot still fails the gate after the bounded retries.

All named packages are reinstalled at their exact versions even when already
present. Named downgrades are permitted so newer hosted copies cannot defeat
the lock; removal of unrelated packages is forbidden. The helper verifies the
installed version, architecture and configured state of every named package.
Post-install hashes are **observed identities**, not replacement expected
hashes for authenticating a download; apt performs that authentication using
the signed repository metadata.

## Consumer binding and retained evidence

GPU provisioning checks PATH resolution against the package-owned versioned
executables, checks their dynamic LLVM/Clang dependencies, and retains the
remaining dynamic closure. The workflow supplies absolute native-harness
overrides for Clang, llc, readobj, objdump and spirv-val. LLD retains its existing
absolute `ld.lld` override. DXC archive and CUDA wheel authentication are unchanged.

Readline provisioning uses the hosted Clang to inspect actual `readline.h` and
`history.h` selection, trace the `-lreadline` link input, inspect the linked
probe's dynamic loader resolution for `libreadline.so.8` and `libtinfo.so.6`,
and execute a noninteractive version-symbol probe. All named paths must resolve
to the reinstalled package payload. The throughput workflow additionally checks
that its ldconfig-based runtime manifest selects those same system paths and
binds the lock and input-identity records into its existing Lua dependency
manifest. Its compiler/admission chain is gated on successful provisioning;
`!cancelled()` cannot carry an apt failure into admitted evidence.

Each evidence directory contains the lock, source stanza, signed InRelease
metadata, exact command argv/status/stdout/stderr, package versions, complete
named-package file identities, selected-consumer identities and observed host
runtime identities. Large apt indexes/archive caches are removed after either
success or failure. Existing artifact uploads retain the evidence, including
failure diagnostics. A pre-existing output directory is rejected, not reused.

## Validation and deliberate updates

Run the offline failure controls with:

```sh
python3 tests/ci_apt_test.py -v
python3 tools/ci_apt_retry_test.py -v
```

The path-filtered `Pinned apt input qualification` workflow runs both profiles
on separate fresh Ubuntu 26.04 jobs. Each provisions twice using separate apt
metadata/cache directories and requires identical named-package and selected
consumer identities. The readline job also runs the real header/link/loader
probe on each pass. This establishes repeatability within that runner; it does
not claim the entire hosted environment is immutable across image updates.

The existing GPU workflow must still pass all four Linux profiles. After both
apt profile jobs pass, the path-filtered workflow calls the existing
`Real-source throughput qualification` through a same-commit reusable workflow
with `qualify_pinned_inputs: true`. This reuses its source staging, native
oracles and admission checks rather than copying them into another harness.
The new input defaults to false, preserving direct PR/dispatch opt-in behavior;
only apt-related changes gain this additional automatic functional gate.
Require the native Lua oracle and workload admission receipts, not just the
provisioning probe. An explicit `workflow_dispatch` on the candidate branch
remains available for reproduction. A skipped or queued workflow is not validation.

For a security or correctness update, deliberately advance the UTC snapshot
and exact package builds together in a reviewed PR. Check the Ubuntu binary
package metadata and source-package dependency split, update the closed-set
regression when the split changes, inspect apt's proposed operations and the
new signed metadata, then run both repeatability jobs plus the real GPU/Lua
gates above. Retain the old and new selected identities with the exact commit
and runner image. Do not relax validation to work around an unavailable pin;
review an updated snapshot instead. Schedule such reviews with the repository's
normal dependency/security maintenance; this lock is not a recommendation to
keep stale tools indefinitely.

Upstream references: [Ubuntu Snapshot Service](https://snapshot.ubuntu.com/),
[Clang 18 in resolute](https://packages.ubuntu.com/resolute/amd64/clang-18),
[SPIRV-Tools](https://packages.ubuntu.com/resolute/spirv-tools),
[readline development package](https://packages.ubuntu.com/resolute/amd64/libreadline-dev),
and [ncurses development package](https://packages.ubuntu.com/resolute/libncurses-dev).
Package pages describe the intended builds; successful signed snapshot
provisioning is the runtime availability check.
