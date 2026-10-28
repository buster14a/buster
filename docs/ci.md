# Hosted correctness, native vector correctness and Zen 5 benchmarks

The workflow in `.github/workflows/9700x.yml` runs correctness tests on
GitHub-hosted `ubuntu-26.04` runners. Its matrix builds the triggering revision
with `BUSTER_HOST_ARCH=x86-64` and `BUSTER_HOST_ARCH=native`. Both use the current
official stable LLVM 23.1.4 release, including its matching C++ pragma oracle,
LLVM object/DWARF tools and lld. The installer runs only on disposable hosted
runners. GCC remains the `cc` oracle for fixtures that require GNU C extensions.

The baseline build runs on x86-64 without AVX-512. Native builds select the
existing scalar twins when the compiler's host target lacks the full SIMD
feature set. Intrinsic-only shared helpers have scalar definitions for that
build. Generated-code ISA and ABI remain controlled by the existing target
options. Both hosted variants log their CPU, target features and selected path.
The baseline leg must report `scalar`; the native leg uses vector kernels when
the full predicate in `host_isa.h` holds, and scalar kernels otherwise. A hosted
CPU without AVX-512 therefore still runs the complete correctness suite.

Standard `ubuntu-26.04` runners do not promise a particular CPU model or this
feature set. Required vector coverage comes from the real Ryzen 7 9700X, after
both hosted variants pass on the same triggering revision. The hardware job
asserts `BUSTER_HOST_VECTOR=1`, builds with `BUSTER_HOST_ARCH=znver5` and runs
the complete suite before any benchmarks. Hosted and hardware jobs read one
suite inventory, `tools/ci/correctness.tests`, with the same counts, seeds and
external cross-checks. There is no vector emulation or randomized runner pool.
Baseline memory boundary tests also run with undefined-behavior checks.
The baseline job additionally runs Buster under QEMU's `qemu64` CPU to preprocess
and compile a C fixture and assemble a multi-instruction fixture, then links and
executes their results on that same emulated CPU.
That leg links generic Ubuntu startup objects, libc and compiler support routines
from checksum-pinned packages in a private directory (`BUSTER_HOST_RUNTIME`).
It uses their loader explicitly for emulation: a hosted VM's installed startup
objects and libraries can require a newer ISA independently of `-march=x86-64`.
`BUSTER_CLANG_RESOURCE_DIR` selects the matching Clang builtin headers, preserving
their paths in line markers even when Clang is staged outside `/usr/lib/clang`.

The hosted tests fetch the frozen corpus from the
[input release](https://github.com/buster14a/buster-performance/releases/tag/ci-corpora-preprocessor-391e729af658).
`tools/ci/fetch-corpus.sh` checks the archive digest, original manifest digests,
every input byte, file counts and file kinds. The archive retains source/header
notices, package provenance and component license texts. Historical paths and
the old Clang binary hash remain provenance records; the active oracle is the
installed Clang 23 toolchain. A missing or changed input fails setup.

The `benchmarks` job runs native correctness followed by benchmarks. It has
`needs: [changes, test]` and explicitly requires
`needs.test.result == 'success'`. GitHub applies that result to the entire test
matrix. A failure, cancellation or skipped suite prevents the physical job from
being queued, including scheduled and manual runs whose change selector is
skipped. The hardware job checks out the same `github.sha`, verifies the Ryzen
7 9700X identity and vector eligibility, and retains the existing host lock,
CPU placements and benchmark selection. Native tests use eight CPUs under the
lock, and benchmarks reuse that tested executable. A native test failure stops
the job before measurements.

The job stages the current official stable Clang 23.1.4 release and matching
Clang API, linker and object/DWARF tools in the runner's private cache, verifying
the archive SHA-256 and preserving its licenses. A new completeness marker
upgrades older compiler-only caches. The pragma oracle links to the official
self-contained `libclang-cpp`, avoiding the release's Ubuntu-specific static
system-library paths on the Arch host. Frozen inputs use the same verified
archive as hosted tests. No system packages are modified.

The hosted `qualification` job retains the ruleset's existing required context,
`Build and execute on Ryzen 7 9700X`. It requires successful hosted tests and
the hardware job, including native correctness, so a skipped hardware job after
hosted failure cannot accidentally satisfy the merge gate. Documentation-only
changes retain their explicit no-code path.

To check CI edits locally, run the selector's self-test and validate the shell
and workflow syntax. To exercise fallback code on a capable machine, set
`BUSTER_HOST_ARCH=x86-64` for `build.sh`; keep Zen 5 performance measurements on
Benchpress.
