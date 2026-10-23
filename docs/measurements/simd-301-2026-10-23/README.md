# SIMD loop optimization receipts (#301)

These are original measurements for
[#301](https://github.com/buster14a/buster/issues/301), made on October 22 UTC /
October 23 Europe/Madrid, 2026. They compare the retained implementation with
`1f1e116ad358bed8d5a97824b916908d7cb44e96`. The
[source and executable hashes](source-and-binary.sha256) identify the final
production source, regression tests and complete compiler executables.

## Scope and host

The changes remove constant radix passes in the binding batch, replace
repeated builtin operation classification with one register-table lookup,
and combine the parser K6 role refinements into a joint lookup table.
The scalar implementations remain independent oracles. No preprocessor,
parser-output selection, postorder or traversal changes are included.

The host was a cloud AMD EPYC 9V74 with AVX-512, running Ubuntu Clang 18.1.3.
Binding and parser measurements were pinned to CPU 2; builtin measurements
were pinned to CPU 4. Every timed process acquired the same
`/tmp/buster-simd-bench.lock` exclusively. This prevents overlap between this
session's experiments; it cannot exclude unrelated virtual-machine noise.
Hardware performance counters were unavailable. Nanoseconds are the timing
evidence; zero cycle or IPC fields printed by the existing parser harness
must not be interpreted as measurements.

This is cloud screening, not Ryzen 7 9700X qualification. The normal PR
workflow supplies target-host validation separately. No target-host sustained
or after-scalar A/B measurements are claimed here. Binding and builtin batches
are synthetic kernel interfaces; their gains do not establish complete C
compiler throughput improvements.

## Final complete executable comparison

[final-executables.txt](final-executables.txt) contains three independently
started baseline/candidate process pairs for each benchmark. Each binding
process runs 21 interleaved passes over the default synthetic stream: 262,144
generated events, 246,065 retained identifier events and 8,192 possible names.
Each builtin process runs its existing 21 calibrated samples over 16,384 rows
with `--only vector`. Preparation is outside timing, result stores are inside,
and each result is checked against the existing scalar reference.

Values are the median of the three process medians, with their minimum and
maximum in brackets. These ranges are observations, not confidence intervals.

| Workload | Unit | Baseline | Candidate | Median time reduction |
| --- | --- | ---: | ---: | ---: |
| Binding key packing and sort | ns/identifier | 10.859 [10.846, 11.846] | 7.986 [7.589, 8.727] | 26.5% |
| Complete binding batch | ns/identifier | 29.978 [27.539, 31.490] | 24.100 [22.442, 27.630] | 19.6% |
| Builtin bit operations | ns/row | 1.426 [1.423, 1.759] | 1.077 [1.038, 1.102] | 24.5% |
| Builtin mixed operations | ns/row | 2.361 [2.329, 2.416] | 2.037 [2.008, 2.238] | 13.7% |
| Builtin overflow operations | ns/row | 2.828 [2.772, 2.868] | 2.875 [2.750, 3.055] | -1.7% |

All three paired binding and bit/mixed builtin processes improve. The complete
binding-batch ranges overlap, and the separate earlier runs below show a
smaller batch gain. Overflow does not use the new descriptor path, its
process ranges overlap, and its direction varies with configuration; no
overflow improvement is claimed.

The final complete executables were built with the repository's build script,
through an uncommitted copy adding `-Wno-bitwise-instead-of-logical -mmwaitx`
after `-Wall -Werror`. Clang 18 otherwise rejects an existing lexer warning
and existing MWAITX intrinsics on this cloud host. Production build scripts
and target flags are unchanged. The effective compilation flags are:

```text
-O3 -g -march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing
-Wall -Werror -Wno-bitwise-instead-of-logical -mmwaitx
```

To build in each separate baseline/candidate checkout on this host:

```sh
sed 's/-Wall -Werror)/-Wall -Werror -Wno-bitwise-instead-of-logical -mmwaitx)/' \
    build.sh > /tmp/buster-simd-cloud-build.sh
PATH=/usr/lib/llvm-18/bin:$PATH bash /tmp/buster-simd-cloud-build.sh
```

Copy each executable outside `build/` before the next build. The retained
run order was baseline bindings, baseline builtins, candidate bindings,
candidate builtins, repeated three times. For each invocation, using those
saved executables:

```sh
flock /tmp/buster-simd-bench.lock taskset -c 2 ./compiler-simd-baseline benchmark-bindings
flock /tmp/buster-simd-bench.lock taskset -c 4 ./compiler-simd-baseline benchmark-builtins --only vector
flock /tmp/buster-simd-bench.lock taskset -c 2 ./compiler-simd-candidate benchmark-bindings
flock /tmp/buster-simd-bench.lock taskset -c 4 ./compiler-simd-candidate benchmark-builtins --only vector
```

## Isolated kernels and distributions

### Binding batch

[bindings.txt](bindings.txt) retains the earlier standalone runs through the
unchanged benchmark. Names `old` and `new` denote baseline and candidate.
The wrapper is [bindings-benchmark.c](bindings-benchmark.c); select the
implementation using the desired checkout's `src` include directory:

```sh
clang -O3 -g -march=native -mmwaitx -fwrapv -funsigned-char \
    -fno-strict-aliasing -Wall -Werror -I/path/to/checkout/src \
    docs/measurements/simd-301-2026-10-23/bindings-benchmark.c \
    -o /tmp/bindings-benchmark
flock /tmp/buster-simd-bench.lock taskset -c 2 /tmp/bindings-benchmark --only sort
flock /tmp/buster-simd-bench.lock taskset -c 2 /tmp/bindings-benchmark --only batch
```

Three processes of each version per isolated path gave sort medians of
10.403 to 7.865 ns/identifier (24.4% less time), and batch medians of
26.156 to 23.645 (9.6%). Every paired process improved, with overlapping
batch ranges. The generator and all fallback statistics are unchanged.

Three default/interleaved processes per version also ran each of
`--names 2`, `--names 256` and `--names 1000000`. The first has a single
ordinary identifier and no varying key bytes; the others exercise one and
three varying bytes. Sort medians are 7.220 to 1.053, 9.737 to 4.660 and
12.927 to 10.600 ns/identifier, respectively. The protected-page tests cover
all subsets of zero through four varying bytes, including nonzero constant
middle bytes and odd-pass results in the temporary buffer.

Single-process `--events 16 --names 8` and `--only batch --after-scalar 100`
checks are also retained. Their short or noisy timings are not independent
evidence for a claimed percentage improvement.

### Builtin integer actions

[builtins.txt](builtins.txt) contains 18 standalone processes: three of each
version in each of default/interleaved, `--only vector`, and `--only vector
--after-scalar 100`. See [the component discussion](../../c-builtins.md)
for all medians and ranges. Bit-operation time falls by 23–25%; mixed time
falls by 11–14%; overflow remains near parity with overlapping ranges.

[builtin-benchmark.c](builtin-benchmark.c) calls the unchanged benchmark;
[builtin-tiny.c](builtin-tiny.c) and [builtin-tiny.txt](builtin-tiny.txt)
preserve the checks of counts 1, 7, 8, 9 and 16 across 64 independent calls.
Each small-count case compares full outputs with the scalar twin. Three
processes per version gave throughput ratios of 1.31–1.46 for bit operations
and 1.08–1.31 for mixed valid/invalid inputs. No small-count dispatch rule
was added.

The optimized standalone `cbuiltin_fold` text shrinks from 1,417 to 1,307
bytes, and contains the intended `vpermi2d`. There are no hardware gathers,
scatters or vector spills. Both versions retain the same two scalar input
pointer spill/reload slots in the overflow branch, with none in the bit
branch. The full compiler's two cloud compatibility flags produce
byte-identical standalone `.text` for both versions.

### Parser K6

[parser-k6.txt](parser-k6.txt) retains the ablation runs; the kept candidate
is the refinement-only variant. Three fresh processes per version, each
with `--only vector-k6 --passes 7 --repeat 8 -nostdinc`, give medians of
3.228 [3.211, 3.804] to 3.050 [3.039, 3.202] ns/token: 5.5% lower time.
Each process checks the vector outcome and tree against the scalar parser.
Fallback remains exactly 55 declarations and 31,386 tokens.

The frozen Clang 18-preprocessed compiler source has 2,155,660 parser tokens
and SHA-256
`24bbf3dd765eb8e1d84a9d9ba23304b6d1846b902214c6e28c0f2cb85d706270`.
It was produced once from the baseline with `clang -E -P -march=native
-mmwaitx -I<baseline>/src <baseline>/src/buster/compiler/main.c`. The
baseline includes its normal headers; no external implementation or corpus
is committed with these receipts.

[parser-whole.txt](parser-whole.txt) retains two complete whole-vector
process pairs (`--only vector --passes 11 -nostdinc`): 11.044 to 10.833
and 10.455 to 10.391 ns/token. The third pair's output was not fully retained,
so it is excluded. No whole-parser throughput gain is established.
[parser-driver.txt](parser-driver.txt) retains seven processes per version
with a matched refinement-only syntax driver, including initialization:
all 14 exit successfully, but elapsed-time ranges overlap and paired
directions vary. It establishes no end-to-end speed change.

K6's common untyped refinement uses three table permutations instead of
seven, and two blends instead of seven. Static tables shrink by 640 bytes.
Its new exhaustive test compares every valid role, all 128 kind values and
three typed-lane patterns with the original scalar classification rules.

## Validation and rejected experiments

[validation.txt](validation.txt) preserves the final executable's output:

- `test-bindings`: 4,255,777 checks, zero failures.
- `test-builtins`: guarded columns and tails 0–33; 8,224 full-ID alias checks;
  partitions 1–17; 221,184 boundary and 1,019,519 generated Clang-oracle rows.
- `test-parser`: compact-table checks, 230 snippets, four hand-checked trees,
  2,000 random streams, 14,720 alignments, 100,000-level nesting cases and
  driver/allocation-failure cleanup checks.
- The repository call-graph check: 30 translation units, 15,316 functions,
  83,571 calls, zero prohibited cycles and three existing allowed cycles.
- Binding tests also pass AddressSanitizer and UndefinedBehaviorSanitizer
  using Clang 18 at `-O1 -g -fsanitize=address,undefined
  -fno-omit-frame-pointer`, with `BUSTER_BINDINGS_TESTS=1`.
  `ASAN_OPTIONS=detect_leaks=0` is necessary because this sandbox blocks
  LeakSanitizer's process inspection; no leak-sanitizer pass is claimed.
- The standalone builtin suite also passes at `-O0`.

The full cloud build and call-graph check use the temporary compatibility
script described above. The parser tests here omit the external `--clang`
corpus gate; the target workflow runs the repository's required toolchain.
No MWAITX-dependent multi-lane test was run on this cloud host.

Four investigated alternatives were discarded: multiple refinement-scan
accumulators mostly moved timings within noise; deferred/masked ABI stack
gathers sometimes regressed; compacting the parser's `ends` table did not
improve the combined candidate consistently; and rewriting nearest-smaller
byte arithmetic to masks produced identical Clang 18 assembly. None of
those experimental changes is in the production diff.

All changed implementation, regression tests, harnesses and measurement
records are original project work. The joint parser table is generated at
initialization from the existing scalar rules. Clang is used as compiler
and semantic oracle; no Clang implementation or generated compiler source
is imported into this change.
