# PR139 SIMD kernel validation

Opt-in Linux x86-64 validation of the three kernels introduced by PR139:
metadata base64 decode, narrow quoted-literal decode, and identifier selection
in `c_symbols_intern_tokens`. Requires Python 3, Clang, binutils `objdump`,
`taskset`, and an AVX-512 F/BW/VBMI/VBMI2 host. No dependencies are downloaded.

Build **the current source tree** in Debug through the canonical driver first;
its non-unity objects supply the production lexer:

```sh
./build.sh build --config Debug -t ide
```

Then run correctness and assembly generation, choosing a private output directory:

```sh
python3 tools/bench_pr139_simd/run.py --out /tmp/buster-pr139-kernels
```

For seven alternating scalar/SIMD timing pairs, including short/dense regimes:

```sh
python3 tools/bench_pr139_simd/run.py --out /tmp/buster-pr139-kernels --time --regimes --cpu 0
```

Select an allowed CPU with `--cpu`; its default is the first allowed CPU.
`--repo` selects the source/build tree and `--clang` selects the host compiler.
Run timed work without concurrent builds, tests, or other benchmarks. Results
are `CLOCK_MONOTONIC_RAW` elapsed time on a pinned thread, **not hardware
instructions, cycles, branch misses, or end-to-end compiler throughput**.
Under KVM they remain provisional because vCPU scheduling and host contention
are outside this harness's control.

`prepare.py` extracts the production function bodies and the existing scalar
references by name; the tested algorithms are not reimplemented. It preserves
`simd.h` in the literal/identifier paths. The existing base64 kernel still uses
raw intrinsics: its `vpmultishiftqb` operation is absent from `simd.h`. This
harness validates that existing implementation without changing the vocabulary.
The callable decoder entry points are marked `noinline` to retain repeated
direct calls. Their helper bodies remain eligible for normal optimization.

The corpus includes all numeric base64 chunks in the x86 metadata header and
the literal spellings and token shapes produced by **Buster's `c_lex`** over
every `.c`/`.h` file under `src/buster/lib`. This is a physical-source corpus,
not a captured stage-1 preprocessing trace: inactive text remains present,
include multiplicity is absent, and driver/builtin-header inputs are not
included. Per-file source hashes and counts are in `manifest.json`.

Two intentional boundaries keep the microbenchmarks interpretable:

- The quoted decoders reuse an output buffer. Arena allocation and parser or
  lowering overhead are excluded, while the complete decoder bodies are run.
- The exact identifier loop calls a direct recorder instead of the symbol
  hash table. It records selected token indices in order and writes symbol
  fields. The differential gate compares both arrays with the scalar row loop.
  These timings measure selection plus that recorder, not complete interning.

The differential gate covers real bytes, full-byte random base64, missing
characters/extra groups, input alignment phases, narrow literals with escapes
slid across offsets 0–200, valid/invalid escapes, count-only agreement, tails
around 64-byte boundaries, a 70 KB literal, 10,000 random literal bodies,
identifier tails/densities, output canaries, and inaccessible pages immediately
after the valid input. Corpus read/allocation or guard-page setup failure stops
the process before any timing is reported.

Synthetic timing regimes intentionally include populations that can regress:
4–128-character base64 chunks; 0–128-byte plain literal bodies; a 128-byte body
made only of `\n` escapes; and identifier densities of 0%, 1%, 20%, and 100%.
The positive aggregate result must not hide short-input or dense-escape losses.

Outputs stay under `--out`: generated extraction/corpus, binaries, source and
binary provenance, check log, raw timing/checksum logs, median/min/max summaries,
and assembly dumps. Generated corpora and binaries should not be committed.
Inspect the assembly before interpreting width gains: the base64 path uses two
multishifts and two permutes per 64 characters, the quoted path has a scalar
escape arm, and identifier selection retains one scalar visit per set bit.
