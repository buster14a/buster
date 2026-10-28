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
options. Benchmark commands require a vector host build.

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
existing host lock, CPU placements and benchmark selection.

The hosted `qualification` job retains the ruleset's existing required context,
`Build and execute on Ryzen 7 9700X`. It requires successful hosted tests and
benchmarks, so a skipped benchmark after test failure cannot accidentally satisfy
the merge gate. Documentation-only changes retain their explicit no-code path.

To check CI edits locally, run the selector's self-test and validate the shell
and workflow syntax. To exercise fallback code on a capable machine, set
`BUSTER_HOST_ARCH=x86-64` for `build.sh`; keep Zen 5 performance measurements on
Benchpress.
