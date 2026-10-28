# Correctness tests and Zen 5 benchmarks

The workflow in `.github/workflows/9700x.yml` runs correctness tests on
GitHub-hosted `ubuntu-26.04` runners. Its matrix builds the triggering revision
with `BUSTER_HOST_ARCH=x86-64` and `BUSTER_HOST_ARCH=native`. Both use the current
packages from LLVM's Clang 23 release branch, including the matching C++ pragma
oracle, LLVM object/DWARF tools and lld. The installer runs only on disposable
hosted runners.

The baseline build runs on x86-64 without AVX-512. Native builds select the
existing scalar twins when the compiler's host target lacks the full SIMD
feature set. Intrinsic-only shared helpers have scalar definitions for that
build. Generated-code ISA and ABI remain controlled by the existing target
options. Benchmark commands require a vector host build. Both hosted variants
have a required path: the baseline leg must report `scalar`, and the native leg
must report `vector` (`BUSTER_HOST_VECTOR=1`, the full native feature predicate
in `host_isa.h`). Each run logs its CPU, target features and selected path before
checking this requirement. A native runner that selects the scalar fallback
fails eligibility before building or running the suites, so two scalar-only
passes cannot satisfy the required correctness gate. Product native builds
still select the scalar fallback on CPUs without the complete feature set.

Standard `ubuntu-26.04` runners do not promise a particular CPU model or this
feature set. A future unsuitable VM can therefore block correctness qualification
until an eligible hosted runner or a separately qualified vector-algorithm
correctness variant is provided. The capability check establishes eligibility;
the current revision must still pass the full native correctness suites.
Baseline memory boundary tests also run with undefined-behavior checks.
The baseline job additionally runs Buster under QEMU's `qemu64` CPU to preprocess
and compile a C fixture and assemble a multi-instruction fixture, then links and
executes their results on that same emulated CPU.
That leg links generic Ubuntu startup objects, libc and compiler support routines
from checksum-pinned packages in a private directory (`BUSTER_HOST_RUNTIME`).
It uses their loader explicitly for emulation: a hosted VM's installed startup
objects and libraries can require a newer ISA independently of `-march=x86-64`.

The hosted tests fetch the frozen corpus from the
[input release](https://github.com/buster14a/buster-performance/releases/tag/ci-corpora-preprocessor-391e729af658).
`tools/ci/fetch-corpus.sh` checks the archive digest, original manifest digests,
every input byte, file counts and file kinds. The archive retains source/header
notices, package provenance and component license texts. Historical paths and
the old Clang binary hash remain provenance records; the active oracle is the
installed Clang 23 toolchain. A missing or changed input fails setup.

The `benchmarks` job has `needs: [changes, test]` and explicitly requires
`needs.test.result == 'success'`. GitHub applies that result to the entire test
matrix. A failure, cancellation or skipped suite prevents the physical job from
being queued, including scheduled and manual runs whose change selector is
skipped. The benchmark job checks out the same `github.sha`, builds with
`BUSTER_HOST_ARCH=znver5`, verifies the Ryzen 7 9700X identity and retains the
existing host lock, CPU placements and benchmark selection. It stages the current
official stable Clang 23.1.4 release in the runner's private cache, verifying the
archive SHA-256 and preserving its licenses. It does not modify system packages.

The hosted `qualification` job retains the ruleset's existing required context,
`Build and execute on Ryzen 7 9700X`. It requires successful hosted tests and
benchmarks, so a skipped benchmark after test failure cannot accidentally satisfy
the merge gate. Documentation-only changes retain their explicit no-code path.

To check CI edits locally, run the selector's self-test and validate the shell
and workflow syntax. To exercise fallback code on a capable machine, set
`BUSTER_HOST_ARCH=x86-64` for `build.sh`; keep Zen 5 performance measurements on
Benchpress.
