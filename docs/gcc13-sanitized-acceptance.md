# GCC 13 sanitized runtime acceptance

The ordinary desktop combination matrix intentionally keeps its GCC row
compile-only. That row proves compiler portability, but it does not execute the
registered suite or the sanitizer canaries with GCC. The opt-in
`GCC 13 sanitized runtime acceptance` workflow closes that evidence gap without
adding a second GCC matrix to every pull request.

The workflow runs on Ubuntu 24.04 with `/usr/bin/gcc-13` selected through
`BUSTER_GCC`. A Clang-built `build.c` driver is the documented hosted bootstrap
exception; GCC 13 remains the configured application compiler. The workflow
uses the repository-owned fatal ASan/UBSan/LSan policy and does not set a
competing workflow-wide sanitizer environment.

## Reproduction

With an idle build directory and GCC 13 installed:

```sh
export BUSTER_GCC=/usr/bin/gcc-13
export BUSTER_TEST_JOBS=1
export CMAKE_BUILD_PARALLEL_LEVEL=4

clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable \
  -g build.c -o /tmp/buster-build
/tmp/buster-build generate --sanitize --cc gcc --ci --linker DEFAULT -- \
  -DBUSTER_UNITY_BUILD=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
/tmp/buster-build build --config Debug -t test_all -- -j4
```

A successful run means the complete GCC 13 Debug application builds and links
with warnings as errors, then the full registered suite executes. This includes
the sanitizer module's clean controls, fatal undefined-behavior child and the
supported LeakSanitizer children. A compile-only GCC row or a successful focused
object build is not a substitute.

## ELF data alias control

The workflow also retains the failure-first boundary behind #594. Its generated
shared library gives `pair_0` and `data_0` one aliased definition. First, the
old GCC 13 `-fPIE` object plus `-no-pie` link must fail the identity/value
program, demonstrating that the control can still distinguish the two-copy
relocation layout. Then GCC 13 and Clang each compile the reference as
`-fPIC -pie`; both binaries must observe one address and the mutation through
`pair_0` through `data_0` and `read_0()`.

This standalone check supplements rather than replaces
`compiler_driver_test_elf_data_scaling`. The registered GCC suite must remain
green with every existing shape, count and Buster-side `reference=0` assertion.

## Workflow boundary

The workflow has read-only repository permissions, retains its exact checkout
SHA, compiler versions, complete suite log and alias-control files, and never
cancels an active acceptance attempt. It runs automatically only for the
issue-scoped implementation branch; after integration it is available by
manual dispatch. Ordinary `CI complete`, self-host and review requirements
remain independent merge gates.
