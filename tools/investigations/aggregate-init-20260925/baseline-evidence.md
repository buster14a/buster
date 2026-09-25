# Aggregate initializer investigation: executed baseline

This branch contains investigation artifacts only. No production source or registered expectation is changed. Do not merge its branch-scoped workflow as a required check.

## Exact provenance

Production main: `dce1b8caa3d368dede16ad4aa9da6760e26fe704`, tree `486099e282770e64b0a4e3844e2f39d91fd720d1`.

`src/buster/lib/compiler/frontend/c/c_gen.c` blob: `6d38c34fefb4a121a0f3c1e6e97358f57b6a5248`. The complete source was exported with `git archive` on the runner and this blob identity was independently checked after extraction.

Transport/source-of-probes commit: `40367ac6bcf660218b409f851e58873c658736ea`, tree `774a063f97f811215c942682ee101b1755d2f6f9`. The workflow verifies no difference from production main in `src`, `build.c`, `build.sh`, or `CMakeLists.txt` before compiling. This is a push-source head, not a PR test-merge commit.

[Run 36146689333](https://github.com/buster14a/buster/actions/runs/36146689333), job `108109511674`, standard GitHub-hosted Ubuntu 26.04.1 x86-64. Compiler built and executed on that same runner. Ubuntu Clang 21.1.8 (6ubuntu1), GCC 15.2.0 (15.2.0-16ubuntu1), CMake 4.4.3, Ninja 1.13.2; runner reports AMD EPYC 9V74. No desktop, SSH or benchmark-host execution.

Trusted Clang-built Release `ide` SHA-256: `5df3edf9143f3ae7bc9731be633bd33f9e7c3cce4cf0fd9e8fb8d7fb216d1f19`.

[Raw results artifact 10869707498](https://github.com/buster14a/buster/actions/runs/36146689333/artifacts/10869707498), ZIP SHA-256 `f69fbddb113a9d92b3ab889319708269b0042ec6385974829dd592b46aa82d4e`. ZIP digest and all 1,338 manifest-listed files were verified after download. Includes exact argv (one argument per line), separate stdout/stderr, compile/run statuses, binaries, versions, build logs, source/transport identity and original C inputs.

[Source artifact 10870231066](https://github.com/buster14a/buster/actions/runs/36146689333/artifacts/10870231066), ZIP SHA-256 `4d6b9da56c4977667748521caf6bdbc59d0441630790262913b301d9b6f45cde`. Inner source.tar.gz SHA-256 `d05867f8437950b29ff2500743ec8889ff139838c49ed9dab91184a21a01624c`.

Actions artifacts expire 2026-10-25. The sources, observer, workflow, matrix and decisive raw observations below remain in the repository independently of that retention period.

## Replay on an authorized standard correctness runner

Use a fresh checkout/output directory. Build and execute on the same runner because Release host tuning is native. This uses the documented hosted Clang bootstrap exception; it is not TCC/self-host evidence.

```sh
git checkout 40367ac6bcf660218b409f851e58873c658736ea
mkdir -p evidence
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o /tmp/buster-build
/tmp/buster-build generate --cc clang --ci --linker DEFAULT
/tmp/buster-build build --config Release -t ide
clang -std=c17 -Wall -Wextra -Werror tools/investigations/aggregate-init-20260925/probe.c -o /tmp/aggregate-observer
/tmp/aggregate-observer "$PWD/build/Release/ide" "$PWD/tools/investigations/aggregate-init-20260925/cases.c"
```

The observer returns nonzero on any failed compile or incorrect execution. The red result is retained, not converted into a passing expected diagnostic. It uses compile timeout 30 seconds / execution timeout 5 seconds, each with a two-second kill grace.

To reproduce one exact recorded row:

```sh
build/Release/ide cc -std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0 -O0 \
  -target x86_64-linux -fregister-allocator=fast -fverify-codegen -fno-machine-fallback \
  -ffrontend-ssa -DCASE=4 -DSTORAGE=0 \
  tools/investigations/aggregate-init-20260925/cases.c -o /tmp/aggregate-case
/tmp/aggregate-case
# Actual stdout: 1 2 0 0 ; exit 1. Expected stdout: 3 2 0 0 ; exit 0.
```

## Complete baseline disposition

16 cases x automatic/file-static storage x six profiles = 192 compilation attempts. Profiles 0/1 are Clang O0/O2 with `--target=x86_64-linux-gnu`; 2/3 are GCC O0/O2 with `-m64`; 4/5 are Buster FAST/O0 with direct-SSA/memory-form frontend respectively. Common flags: `-std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0`. Both Buster profiles also require `-fverify-codegen -fno-machine-fallback`.

All 128 independent-reference rows compile and exit 0. All 32 Buster file-static rows compile and exit 0. Each Buster frontend has 7 wrong-result automatic cases, 2 automatic compilation failures, and 23 passing rows overall. Total: 188 successful compilations, 4 compilation failures; 188 executions, of which 174 pass and 14 return a wrong-result mask. No timeout or runtime signal occurred.

The following automatic results are identical for profiles 4 and 5. `-1` means not executed after compilation failure. The corresponding static and all reference rows have compile/run `0/0` and expected stdout.

| CASE | Mechanism/control | Compile | Run | Actual stdout | Expected stdout |
|---:|---|---:|---:|---|---|
| 0 | `.a.x=1,2` continuation | 0 | 6 | `1 0 2 0` | `1 2 0 0` |
| 1 | `.a.y=2,3`, exhausted inner control | 0 | 0 | `0 2 3 0` | same |
| 2 | `.a.x=1,2,3,4` continuation | 1 | -1 | not run | `1 2 3 4` |
| 3 | `.a={1,2},.a={.x=3}` | 0 | 3 | `1 2 0 0` | `3 0 0 0` |
| 4 | `.a={1,2},.a.x=3` | 0 | 1 | `1 2 0 0` | `3 2 0 0` |
| 5 | reverse order control | 0 | 0 | `1 2 0 0` | same |
| 6 | disjoint nested/outer fields | 0 | 0 | `1 2 3 4` | same |
| 7 | fully explicit scalar designators | 0 | 0 | `1 2 3 4` | same |
| 8 | `.a[0][0]=1,2,3,4` | 1 | -1 | not run | `1 2 3 4` |
| 9 | nested array then leaf override | 0 | 1 | `1 2 0 0` | `3 2 0 0` |
| 10 | nested array then whole override | 0 | 3 | `1 2 0 0` | `3 0 0 0` |
| 11 | fully braced arrays control | 0 | 0 | `1 2 3 4` | same |
| 12 | pointer subaggregate then whole null override | 0 | 3 | `1 1 0 0` | `0 0 0 0` |
| 13 | pointer subaggregate then leaf null override | 0 | 1 | `1 1 0 0` | `0 1 0 0` |
| 14 | only scalar pointer designator overrides | 0 | 0 | `0 1 0 0` | same |
| 15 | flat positional brace elision | 0 | 0 | `1 2 3 4` | same |

Raw compilation diagnostic for cases 2 and 8, both frontend forms:

```text
cc: error: /home/runner/work/buster/buster/tools/investigations/aggregate-init-20260925/cases.c:99:16: in function 'main': could not lower initializer expression for local 'obj'
```

## Scope and interpretation

The cases use constant integers or addresses of a live static object, read named members rather than padding, and make no initializer-expression evaluation-order assumption. Pointer rows compare pointers; they do not dereference the incorrectly retained pointer. Clang/GCC agreement supports the expectations but is not their normative basis: see WG14 N1570, 6.7.9p17-21. N1570 is a C11 committee draft; actual command dialect is C17.

Baseline does not include reference sanitizers, other allocators, O2 Buster, compound-literal context, Debug/sanitized Buster, full registered suite, TCC bootstrap, self-host or other targets. The separate `minima.c`/`confirm.c` campaign owns any later expanded evidence; do not transfer results between campaigns. No performance measurement or claim.
