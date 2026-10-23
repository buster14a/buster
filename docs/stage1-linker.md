# Stage 1 host linker

Clang compiles the 30 translation units of Buster and links `build/compiler`.
The driver currently preprocesses and parses C; this is a stage 1 build by
Clang, not a self-hosted C compilation pipeline. Its built-in ELF linker,
object writer, output targets and external differential-test oracles follow
[the existing design](object-writer-and-linker.md) and are independent of
this host-executable link.

## Selection

`./build.sh` uses `BUSTER_LINKER=auto`: first `wild` on PATH, then `mold`,
then Clang's original system default. It prints the chosen executable.
Nothing is downloaded, installed, vendored or linked into Buster as a new
library dependency. Clang still supplies the startup files and runtime.

```sh
./build.sh
BUSTER_LINKER=system ./build.sh
BUSTER_LINKER=wild ./build.sh
BUSTER_LINKER=mold ./build.sh
BUSTER_LINKER=lld ./build.sh
BUSTER_LINKER=bfd ./build.sh
BUSTER_LINKER=/opt/toolchain/bin/wild ./build.sh
```

Named choices require the corresponding executable (`lld` uses `ld.lld`;
`bfd` uses `ld.bfd`). Absolute executable paths, including spaces, are
passed as one Clang `--ld-path=` argument. Empty/unknown settings and missing
explicit tools fail before clearing `build/`. Link errors are propagated;
auto does not retry a failed link with another tool. Missing optional tools
are the availability fallback, not a way to hide a correctness failure.

Only the final stage 1 link gets this option. The sources, object order,
`-O3 -g -march=native`, compilation flags, generated-code ISA policy and
existing build modes are preserved. `lexer-variant` uses the same selection;
IR capture and `lexer-variant-assembly` do not select a linker.

For repeatable builds across machines, select an explicit path and freeze
the toolchain and inputs. Auto intentionally depends on installed tools.
The same objects produced byte-identical repeated links with each tested
linker; outputs from different linkers are not expected to be byte-identical.
This is same-input determinism, not a claim that clean rebuilds in different
directories/toolchains are reproducible.

## Decision and evidence, 2026-10-23

Choose **Wild 0.10.0**, with **mold 3.0.0** as the optional second choice.
Both preserve the tested PIE, RELRO and non-executable-stack properties.
Wild provides the best stage 1 link latency in these measurements, with no
observed correctness or material frontend-runtime regression. The larger
output is an explicit tradeoff. There is no generated-code optimization or
runtime-speedup claim.

All compiler objects are frozen from
`829fc6d16342920f678b27ad443db3d91737bfef`, in the original source order,
with no LTO or new linker flags beyond tool selection. Timings include the
Clang driver, linker process, output write/close and fork/exec/wait, exclude
compilation, use warm filesystem caches and overwrite the same output path.
Five alternating-order batches of seven measured links follow one untimed
warm-up per batch. Tool thread defaults are retained within CPU affinity.

| Stage 1 link, pooled median (35 samples) | GNU ld | Wild 0.10.0 | mold 3.0.0 | lld 23 |
| --- | ---: | ---: | ---: | ---: |
| GitHub-hosted EPYC 7763, 4 vCPUs | 254.19 ms | 69.32 ms | 70.20 ms | 110.60 ms |
| Local Zen 4 Ryzen 9 7940HS, CPUs 0–7 | 171.35 ms | 37.15 ms | 57.36 ms | 69.46 ms |

The hosted Wild/mold difference is within their sample spread; it alone
does not establish a meaningful winner. The Zen 4 comparison favors Wild.
Wild's native median is 78.3% below GNU ld and 35.2% below mold. This saves
roughly 0.13 seconds on this stage 1 link, not that percentage of the full
compile-and-link build.

The hosted run used Ubuntu 24.04, Clang 23 from apt.llvm.org, GNU ld 2.42,
a diagnostic `-march=znver5 -D_GNU_SOURCE` build, and the upstream release
binaries below. Its CPU lacks Buster's required AVX-512: it proves linking,
ELF inspection and deterministic relinking, not native Buster execution.
[Hosted run and raw artifacts](https://github.com/buster14a/buster/actions/runs/39924777884).

After David explicitly authorized benchmarking on the local Zen 4 machine,
the native run used Clang/lld 23.1.3, GNU ld 2.47, the installed matching
Wild/mold releases, the unchanged `-march=native` build, four concurrent
compile processes, CPU affinity 0–7 for links, CPU 2 for runtime, nice 19
and idle I/O priority. This shared interactive machine is not an isolated
benchmark host; no 9700X build, test or benchmark was run.

Native fresh-process samples, five alternating batches, include the
measurement runner's fork/exec/wait and `taskset` overhead. Startup is an
unknown-option invocation returning 1. Frontend work preprocesses and parses
the frozen `main.c` with `-march=native -D_GNU_SOURCE -Isrc`, in scalar and
default vector modes; input validation and builtin correctness checks are
outside timing. Each startup batch has 31 samples; frontend batches have 11.

| Native pooled median | GNU ld | Wild | mold | lld |
| --- | ---: | ---: | ---: | ---: |
| Warm fresh-process startup (155 samples) | 4.74 ms | 4.78 ms | 5.04 ms | 4.76 ms |
| Scalar frontend (55 samples) | 139.76 ms | 138.79 ms | 139.38 ms | 139.36 ms |
| Vector frontend (55 samples) | 84.58 ms | 83.53 ms | 83.88 ms | 83.49 ms |
| Executable bytes | 56,471,128 | 56,671,247 | 56,518,336 | 56,529,392 |

Wild is 200,119 bytes (0.354%) larger than GNU ld. Startup ranges overlap
(about 3.9–7.2 ms across tools), as do frontend ranges; these data do not
establish a startup or frontend speedup. This measures Buster's currently
available frontend work, not a future complete C compilation or the runtime
of a program generated by Buster. Other host CPUs/tool versions may rank
differently; no Zen 5 performance acceptance is claimed.

Each native executable passed `test-link`, `test-object-writer`,
`test-x86-encoder` and `test-lexer`. Repeated links of identical frozen
objects were byte-identical for all four tools on both hosts. ELF inspection
confirmed PIE, GNU_RELRO and RW-only GNU_STACK; Wild also defaults to NOW.
Object hashes were checked after linking. Full production CI on the physical
9700X remains outside this request.

## Reproduce and validate

On a native Linux/x86-64 host with Buster's required instruction set:

```sh
BUSTER_LINK_CPU=2 ./build.sh benchmark-linkers
```

This mode builds once, retains the object files privately until exit,
compares the system linker and available Wild/mold/lld in rotating order,
checks repeat-link determinism and builtin tests, and reports link latency,
startup, scalar and vector frontend times. It preserves `build/compiler`
from the selected normal link and writes raw CSV, sizes, ELF metadata,
object hashes, version/source provenance and correctness receipts under
`build/stage1-linker-results/`. The CSV contains individual nanoseconds and
exit statuses. Pin a CPU allowed on your machine, or omit `BUSTER_LINK_CPU`.
Keep other work quiet; the tool does not reserve the machine, flush caches,
collect cold-start data or automatically declare a performance pass/fail.

Selection regressions run without compiling Buster or requiring AVX-512:

```sh
mkdir -p build
clang -O2 -Wall -Wextra -Werror tools/build_linker_test.c -o build/build-linker-test
build/build-linker-test "$PWD/build.sh"
```

The C test uses a hermetic recording Clang and executes the real build
script: absent tools, Wild/mold/system preference, each explicit named
choice, absolute paths with spaces, invalid/missing selections, propagation
of linker failure without retries, compile/link separation, lexer variants
and assembly-only mode. The hosted `Stage 1 linker selection` workflow
runs these checks and the external-process timer's exit-status checks.

## External tools and provenance

No upstream implementation or generated upstream data is imported. All new
harness code and regression fixtures are original repository material under
Apache-2.0 WITH LLVM-exception. Wild/mold are external tools used as oracles
and measured executables; their own distributions retain their licenses.

- [Wild 0.10.0 release](https://github.com/wild-linker/wild/releases/tag/0.10.0):
  `wild-linker-0.10.0-x86_64-unknown-linux-gnu.tar.gz`,
  SHA-256 `641265506a7c06cfb03181b8916ab663ec8407855db6d4db7f8450667d105283`.
- [mold 3.0.0 release](https://github.com/rui314/mold/releases/tag/v3.0.0):
  `mold-3.0.0-x86_64-linux.tar.gz`,
  SHA-256 `6c90d4a474c7c0409dfb575be03a5345878ac14fdba18de8b40fa58c60121189`.

The hosted setup checked those archives against the release digests before
extracting them. Installing either tool is a separate user/toolchain action,
never a side effect of building Buster.
