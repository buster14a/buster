# x86-64 disassembler

`src/buster/compiler/x86_64_disassembler.c` decodes bounded raw machine-code
slices into the encoder's semantic instruction records and retains each input
instruction's offset and encoded length. Its AVX-512 path and scalar reference
share the form catalogue; the scalar reference retains the full operand validator
as an independent check on optimized validation. It does not execute input.

```sh
./build/compiler disassemble input.text
./build/compiler disassemble --scalar input.text
./build/compiler disassemble --assembly input.text
./build/compiler test-x86-disassembler --objdump
./build/compiler benchmark-x86-disassembler
./build/compiler benchmark-x86-disassembler --vector
./build/compiler benchmark-x86-disassembler --vector --after-scalar 200
```

Run builds, tests, corpus compilation and measurements through the approved
9700X lock described in [AGENTS.md](../AGENTS.md). The only production executable
is `build/compiler`. Benchmarks are linked into it, and CI correctness checks
use the same implementation as the command.

## Supported input

This is a catalogue-based 64-bit decoder, not a complete x86 disassembler.
Supported rows are the encoder catalogue's 64-bit legacy, x87, MMX, SSE, VEX,
XOP and EVEX maps 1–3, with 64-bit or 32-bit addresses, FS/GS, immediates,
relative branches, SIB/VSIB, masks, zeroing, broadcasts and embedded rounding.
APX/REX2, EVEX maps 4 and above, and 16/32-bit execution modes are excluded.
An instruction's presence does not imply that the Zen 5 host can execute it.
APX BMI/NF encodings in EVEX maps 1–3 are also excluded. UD0 and UD1 use the
ModRM interpretation; their operand-less two-byte catalogue aliases are not
decoded as complete instructions. A two-byte `0f ff` or `0f b9` is truncated.

Legacy prefixes must match a catalogue sequence, followed by at most one REX.
FS/GS and address-size overrides are extracted separately. Repeated overrides,
arbitrary legacy prefix reordering and REX before a later legacy prefix are
unsupported even when hardware would accept them. One bounded exception accepts
Clang's long alignment NOPs: repeated `66` and ignored `2e` prefixes before
`0f 1f`, with a total instruction length of at most 15 bytes. A failure can therefore mean
valid machine code outside this supported subset. No byte-skipping recovery is
attempted; `consumed` identifies the first undecodable instruction.
Ignored W/L settings without a matching catalogue row are also outside the subset.
An incomplete supported encoding is `TRUNCATED`; required bytes beyond x86's
15-byte limit are `UNSUPPORTED`. Output exhaustion returns `OUTPUT_FULL` after
the complete prefix of records. Null pointers for nonempty slices, a source larger
than `UINT32_MAX` or decoding before initialization return `BAD_ARGUMENT`.

The decoder accepts non-shortest disp32 addressing and VEX3 encodings. It
validates decoded operands without using byte-for-byte re-encoding as a validity
test. `X86Decoded.length` is the actual input length; canonical re-encoding may
have another length. The instruction record carries semantic displacement and
immediate fields, not a promise to reproduce the original encoding choice.
`address32` selects the additional address-size encoder mode; catalogue forms
that already contain `67` carry that fact in their form number instead.

The command accepts raw bytes, not ELF containers. To examine code from an
object, first extract the intended section. Symbols, relocations, embedded data,
control-flow traversal and source lines are outside this interface. For example,
the independent C corpus uses ordinary loop control, AVX2, AVX-512 and BMI:

```sh
clang -O3 -march=znver5 -c examples/disassembler/corpus.c -o /tmp/disassembler.o
objcopy -O binary -j .text /tmp/disassembler.o /tmp/disassembler.text
./build/compiler disassemble /tmp/disassembler.text
./build/compiler test-x86-disassembler --file /tmp/disassembler.text
./build/compiler benchmark-x86-disassembler --file /tmp/disassembler.text
```

Intel formatting is bounded and reports insufficient output capacity. Memory
sizes, address-size overrides, masks and broadcasting are explicit. Relative
targets use `. + encoded_length + displacement`, rather than a bare numeric
displacement that GNU as would interpret as an absolute target. Formatting
preserves the decoded instruction's semantics; it is not an exact byte dump or
a claim that the assembly frontend implements every printed catalogue form.

## Data layout and vector work

Initialization groups catalogue rows by encoding family, opcode map and opcode.
Each bucket occupies contiguous arrays of 64-bit signature keys and masks and
16-bit form numbers. Buckets are padded to eight entries with impossible keys.
The keys include mandatory legacy prefixes, W, vector length, EVEX.b, fixed
post-opcode bytes, ModRM `/digit` and memory/register admission where applicable.
Embedded-rounding forms mask the length bits used for rounding. Missing trailing
bytes are excluded from signature comparison so an incomplete candidate still
receives the correct truncation result.

The basic AVX-512 matcher classifies 64 input bytes at once using two nibble-table byte
shuffles and a mask intersection. It reuses the prefix mask across instructions
and matches eight candidate signatures in one vector comparison. Scalar lookup
walks the same bucket and exits on the first complete match. Both paths produce
the same records, counts, status and consumed offset, including malformed input.
For ordinary operand classes, initialization packs register/decorator masks and
memory admission into a small descriptor. Parsing establishes consistent addressing
modes, base/index bounds, immediate widths and high-byte/REX exclusion; the compact
checks validate the remaining constraints. Special classes, VSIB, grouped registers
and other exceptional relationships retain the full validator. The benchmark's
`compact` control uses these same checks with scalar classification and matching,
separating validation specialization from SIMD's contribution.

The default `batched` AVX-512 path computes speculative instruction lengths at
64 byte positions in parallel. A 256-byte opcode metadata table is derived
from unprefixed one-byte catalogue forms whose layouts agree. Two 128-byte
in-register lookups select ModRM/immediate metadata; byte masks derive optional
REX, SIB and displacement lengths. A short dependency walk chooses up to sixteen
reachable starts. An independently derived direct-admission table selects only
unambiguous ordinary legacy forms. `VPERMB` extracts fields at those starts;
registers, SIB modes, signed displacements and immediates are constructed in lanes.
Scalar loads select table entries and scalar stores publish the 24-byte records;
costly gather/scatter instructions are avoided. Unknown or ambiguous rows use the
ordinary matcher, and a disagreement resumes at the actual decoded boundary.
The same classification/length window is reused across fallbacks. Hints alone
never authorize an unchecked read, record or skipped input byte.

Every vector source load is masked to the logical slice, including overlapping
windows and zero-length tails. Neither input padding nor readable bytes after
the slice are required. Output stores publish one complete record at a time
within capacity. Initialization must finish before concurrent readers begin;
the public initializer also initializes the encoder's validation prerequisites.
Tables are read-only thereafter and decode calls allocate nothing. Source and
output record storage must not overlap; in-place expansion is unsupported.
Formatting accepts a successfully decoded record and a separate bounded output
buffer, returns its NUL-terminated text length, or `-1` when capacity is insufficient.

The design follows uops.info's measured Zen 5 costs:
[VPSHUFB](https://uops.info/html-instr/VPSHUFB_ZMM_ZMM_ZMM.html#ZEN5) is one
executed uop with one-cycle measured latency and 0.5-cycle throughput;
[VPERMI2B](https://uops.info/html-instr/VPERMI2B_ZMM_ZMM_ZMM.html#ZEN5) is one
uop with four-cycle latency and 0.5-cycle throughput.
[VPERMB](https://uops.info/html-instr/VPERMB_ZMM_ZMM_ZMM.html#ZEN5), used to extract
fields at known offsets, has the same four-cycle latency and 0.5-cycle throughput.
The register form of
[VPERMT2B](https://uops.info/html-instr/VPERMT2B_ZMM_ZMM_ZMM.html#ZEN5)
has the same cost. These permutations use FP1/FP2.
[VPGATHERDD](https://uops.info/html-instr/VPGATHERDD_ZMM_K_VSIB_ZMM.html#ZEN5)
costs 64 executed uops and about 12.6 cycles per instruction. The decoder uses
contiguous loads and in-register tables, with no gather. These measurements
explain candidates; actual 9700X measurements select the implementation.

## Validation and measurement

Tests cover the supported catalogue with both address sizes, random encoded
instructions, 4,096 randomized dense ordinary instruction streams, random bytes,
all 64 alignments of independent semantic fixtures,
truncation, bounded formatting and input/output guard pages. The fixtures include
non-shortest displacements and VEX prefixes, high EVEX registers, masks, signed
relative targets and immediates, broadcasts and rounding. GNU objdump supplies
an independent boundary/mnemonic oracle; GNU as reassembles formatted operands
for a generated stream whose encoding choices are pinned independently of the
decoder. Scalar/vector agreement alone cannot validate shared catalogue facts.

The benchmark reports decode, a standalone prefix-classification diagnostic,
and decode plus formatting separately. The classification diagnostic scans
nonoverlapping 64-byte chunks; it is not a measurement of all header/boundary
work inside the decoder. The ordinary stream mixes arithmetic, loads/stores, addressing,
branches, stack operations and SSE. The vector stream adds masked AVX-512
arithmetic, broadcasts, FMA, compares, permutations, compression, AVX2 and BMI
with ordinary loop instructions. `--file` uses an actual raw-byte corpus and
fails on incomplete coverage instead of timing only a supported prefix.

Each result is a median of 21 interleaved passes with retained batches of at
least 2 ms, reporting ns, hardware cycles and instructions per instruction
(per byte for classification). The min/max are observed samples, not confidence
intervals. `--only scalar|compact|avx512|batched` isolates an implementation.
`--phase decode|classify|format` isolates work for profiling; `--iterations N`
fixes the number of stream decodes per sample instead of calibrating to 2 ms.
`--after-scalar MS` runs the scalar decoder as warm work before each sample;
this is a transition-sensitive burst measurement, distinct from sustained
back-to-back runs. Hardware counters are zero when unavailable. Initialization,
input generation and file I/O are excluded; the decode-plus-format phase
includes formatting but excludes terminal I/O.

## Recorded validation

The controlled host used Clang 23.1.1 and GNU binutils 2.47. The optimized
decoder harness at `0d547ff` passed seeds 1, 7 and 99. Each seed checks 11,609
catalogue/address-size instances, 100,000 generated instructions, 100,000 random
byte inputs, truncations and mutations, and 4,096 streams of 32 ordinary
instructions drawn from 368 directly admitted forms. Independent fixtures cover
all 64 alignments; readable slices and output capacities are surrounded by
guard pages. The generated oracle reassembled 10,000 formatted instructions
per seed to identical bytes and checked every objdump boundary and mnemonic:
30,000 independent operand roundtrips in total. Mnemonic aliases are normalized
using the decoded predicate or immediate where necessary.

The original C corpus compiled with `clang -O3 -march=znver5` produces 662 bytes
and 152 instructions on this toolchain. All three paths decode the whole slice
and match objdump, including its original long alignment NOPs. This is a small
compiler-generated corpus, not a claim of complete ELF or arbitrary x86 coverage.
An `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer` harness with the
unchanged `d2c7561` decoder also passed seed 7, dense streams, guards and the raw
Clang corpus without sanitizer findings.

## Zen 5 measurements

These results use the production `build/compiler` built at `d2c7561` on the
Ryzen 7 9700X with Clang 23.1.1, `-O3 -march=native`, pinned to CPU 2 and taking
the shared exclusive lock separately for every process. Its SHA-256 is
`c6dd637855ef4f24ee9a1ae8b34e8bed6036d40f0b66aa64ee77da34f10d982b`.
Subsequent changes before submission normalize test-oracle spellings, remove a
redundant raw-path comparison, and add comments/documentation/provenance; decoder
and benchmark executable statements are unchanged. PR CI builds the submitted
head, including its integration with newer main changes.

The table is the median of three process medians, each containing 21 interleaved
passes. The two synthetic streams contain 16,384 instructions: 55,852 ordinary
bytes and 91,105 vector-mix bytes. The Clang corpus has 662 bytes/152 instructions.
Time and counters exclude initialization and I/O. All four implementations
produce identical complete records before timing begins.

| Stream | Implementation | Decode ns/insn | Cycles/insn | Retired instructions/insn | Decode + format ns/insn |
| --- | --- | ---: | ---: | ---: | ---: |
| Ordinary | Full scalar reference | 31.931 | 177.009 | 929.864 | 66.305 |
| Ordinary | Compact scalar control | 23.217 | 128.646 | 597.769 | 57.935 |
| Ordinary | Basic AVX-512 matcher | 23.548 | 130.442 | 554.677 | 58.286 |
| Ordinary | Default batched AVX-512 | 7.579 | 42.012 | 135.500 | 42.056 |
| Vector mix | Full scalar reference | 37.502 | 207.874 | 1051.187 | 85.678 |
| Vector mix | Compact scalar control | 27.290 | 151.304 | 679.222 | 75.494 |
| Vector mix | Basic AVX-512 matcher | 26.420 | 146.484 | 602.634 | 74.908 |
| Vector mix | Default batched AVX-512 | 28.468 | 157.817 | 651.553 | 76.902 |
| Clang corpus | Full scalar reference | 27.643 | 152.999 | 975.627 | 55.168 |
| Clang corpus | Compact scalar control | 17.459 | 96.696 | 629.318 | 45.017 |
| Clang corpus | Basic AVX-512 matcher | 17.242 | 95.447 | 574.528 | 44.751 |
| Clang corpus | Default batched AVX-512 | 14.681 | 81.313 | 407.996 | 41.946 |

Compact validation explains much of the improvement before batching. The SIMD
matcher alone is slightly slower than the compact scalar control on ordinary
code, and slightly faster on the other two streams. Lane construction then
reduces ordinary decode time by another factor of 3.06 versus compact scalar.
The selected default is 4.21 times as fast as the full scalar reference on the
ordinary mix, and 1.88 times on the Clang corpus. Its vector-heavy result is
7.8% slower than the basic SIMD matcher and 4.3% slower than compact scalar;
this is an explicit workload tradeoff, not a universal batching win.

Formatting remains a scalar bounded writer. Adding it changes ordinary default
time from 7.579 to 42.056 ns/instruction, so decode-only speedups do not describe
the full command. Replacing repeated formatted-print calls with bounded text and
integer writers reduced that cost during development. Signature comparison and
speculative lengths without lane construction did not improve full decode;
reusing each window across unsupported hints was also necessary on vector code.

Initialization took 1.365–1.644 ms in these 27 processes, including both encoder
validation-template prerequisites. Each instantiated decoder reserves 2,467,968
bytes of tables, excluding the encoder's tables, with 12,224 active padded
signature entries. The command and benchmark translation units each instantiate
their own tables; only the invoked path initializes its decoder instance.
Initialization is excluded from every throughput number.

### Back-to-back and after scalar work

These are separate processes using `--only batched --phase decode`. Sustained
samples use `--iterations 512`, keeping each sample active for roughly 63 ms
(ordinary) or 238 ms (vector). The warm-batch samples use `--after-scalar 100`
and the automatic minimum 2 ms batch. Warm-burst samples also use
`--iterations 1`: one complete stream immediately after 100 ms of scalar decode,
roughly 125 microseconds or 470 microseconds. Each cell lists all three process
medians, in run order; these are observations, not confidence intervals.

| Stream and state | ns/instruction, three processes | Cycles/instruction, three processes |
| --- | --- | --- |
| Ordinary, sustained | 7.543 / 7.557 / 7.545 | 41.804 / 41.891 / 41.812 |
| Ordinary, warm batch | 8.371 / 7.588 / 7.570 | 46.394 / 42.080 / 41.968 |
| Ordinary, warm burst | 7.648 / 7.642 / 8.050 | 42.429 / 42.400 / 44.673 |
| Vector mix, sustained | 28.367 / 28.408 / 28.436 | 157.198 / 157.466 / 157.658 |
| Vector mix, warm batch | 28.397 / 28.389 / 28.398 | 156.964 / 157.350 / 157.349 |
| Vector mix, warm burst | 28.625 / 28.637 / 28.845 | 158.620 / 158.562 / 159.840 |

The ordinary warm-batch process at 8.371 ns is retained, rather than discarded;
it has a corresponding cycle increase. These measurements do not establish a
power-cap mechanism or a universal transition penalty. Warm-burst medians are
slightly above sustained medians in both streams, with greater process variation
on ordinary code.

### Emitted code and profile

The production assembly retains the complete 24-byte record stores in the
decode-only benchmark. The default loop has five `VPERMB` field extractions and
two `VPERMT2B` metadata lookups, masked source loads, scalar direct-table loads
and scalar record stores unrolled four at a time. It has no gather/scatter.
Clang keeps some lookup halves and lane arrays on the stack; register-only
permutation costs do not account for these additional loads and stores.

A CPU-2 cycles profile of ordinary default decode (`--iterations 512`, 2,687
samples) places 62.59% in the default wrapper, including the inlined length and
lane work, 23.30% in `x86_decode_one`, and 13.91% in candidate construction and
validation. This is function-level sampling, not a precise decomposition of
individual instruction latency. The retained scalar boundary walk, admission
loads, stores, complex-form fallback and formatting are measured limitations;
no llvm-mca prediction is used as acceptance evidence.
