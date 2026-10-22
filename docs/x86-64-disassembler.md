# x86-64 disassembler

`src/buster/compiler/x86_64_disassembler.c` decodes bounded raw machine-code
slices into the encoder's semantic instruction records and retains each input
instruction's offset and encoded length. Its AVX-512 path and scalar reference
share the form catalogue; the scalar reference retains the full operand validator
as an independent check on optimized validation. It does not execute input.
`x86_64_disassembly.c` supplies planned Intel formatting and SIMD address/byte
listings, with the original bounded scalar formatter retained as a separate oracle.

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
The current catalogue has 5,807 admitted rows and 11,609 valid row/address-size
combinations. This pass completes verification of that contract; it does not
add APX, the other excluded EVEX maps, or another execution mode.

Legacy prefixes must match a catalogue sequence, followed by at most one REX.
FS/GS and address-size overrides are extracted separately. Repeated overrides,
arbitrary legacy prefix reordering and REX before a later legacy prefix are
unsupported even when hardware would accept them. One bounded exception accepts
Clang's long alignment NOPs: repeated `66` and ignored `2e` prefixes before
`0f 1f`, with a total instruction length of at most 15 bytes. A failure can therefore mean
valid machine code outside this supported subset. No byte-skipping recovery is
attempted; `consumed` identifies the first undecodable instruction.
Ignored W/L settings without a matching catalogue row are also outside the subset.
`9b` is a standalone FWAIT unless the following bytes complete a supported waiting
x87 form. For example, `9b 9b db e3` decodes as FWAIT followed by FINIT, not one
instruction with repeated waiting prefixes.
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
`--assembly` emits the GNU-as syntax and MPX/AMX feature directives used by the
reassembly oracle. The listing command buffers complete lines and explicitly
flushes stdout, so delayed output failures produce a failing exit status.

## Data layout and vector work

Initialization groups catalogue rows by encoding family, opcode map and opcode.
Each bucket occupies contiguous arrays of 64-bit signature keys and masks and
16-bit form numbers. Buckets are padded to eight entries with impossible keys.
The keys include mandatory legacy prefixes, W, vector length, EVEX.b, fixed
post-opcode bytes, ModRM `/digit` and memory/register admission where applicable.
Embedded-rounding forms mask the length bits used for rounding. Missing trailing
bytes are excluded from signature comparison so an incomplete candidate still
receives the correct truncation result.

The default path additionally compresses VEX/EVEX signature lookup. Each bucket
checks its common bits once; `PEXT` packs the remaining varying bits into an
exact-size cell table. A unique cell names a candidate, an empty cell rejects it,
and an ambiguous cell retains the ordered SIMD scan. Wildcard rows populate all
matching cells. Incomplete headers still take the ordinary matcher so missing
signature bytes do not turn truncation into unsupported input. Candidate construction
and operand validation remain mandatory after a direct lookup. The basic `avx512`
and `compact` benchmark controls retain the original bucket scan.

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
It allocates two retained regions: the exact padded signature arrays and compressed
index cells. Initialization returns false on allocation failure, releases a partial
allocation and permits a later retry. Storage lasts for the process; no reset or
destructor is provided. Tables are read-only thereafter and decode calls allocate nothing. Source and
output record storage must not overlap; in-place expansion is unsupported.
Formatting accepts a successfully decoded record and a separate bounded output
buffer, returns its NUL-terminated text length, or `-1` when capacity is insufficient.

Formatting plans resolve operand roles, register classes, prefixes, memory sizes
and a conservative output bound once per supported form. With enough capacity,
the writer copies packed register tokens, converts decimal digits in pairs and
uses an XMM nibble shuffle for hexadecimal immediates. Smaller buffers retain the
fully bounded scalar writer. A listing converts up to fifteen instruction bytes
with masked loads and in-register byte permutations, then stores the exact fixed
56-byte address/byte prefix. The scalar text and listing paths remain independent
references. Operand selection and decimal conversion are still scalar; formatting
is not described as entirely vectorized.

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
For the compressed index, [PEXT](https://uops.info/html-instr/PEXT_R64_R64_R64.html#ZEN5)
is one executed uop with three-cycle measured latency and 0.33-cycle throughput.
The formatter's [XMM VPSHUFB](https://uops.info/html-instr/VPSHUFB_XMM_XMM_XMM.html#ZEN5)
has two-cycle measured latency, unlike the one-cycle ZMM form above; both have
0.5-cycle throughput. These widths and operand forms are kept distinct when
explaining the emitted code.

## Validation and measurement

Tests cover the supported catalogue with both address sizes, random encoded
instructions, 4,096 randomized dense ordinary instruction streams, random bytes,
all 64 alignments of independent semantic fixtures,
truncation, bounded formatting and input/output guard pages. The fixtures include
non-shortest displacements and VEX prefixes, high EVEX registers, masks, signed
relative targets and immediates, broadcasts and rounding. GNU objdump supplies
an independent boundary/mnemonic oracle; GNU as reassembles formatted operands
for both the exhaustive supported catalogue and a random generated stream whose
encoding choices are pinned independently of the decoder. A NOP separates a
standalone FWAIT in generated streams so objdump cannot fold it into the next
instruction. Raw byte fixtures separately test waiting-instruction boundaries.
GNU objdump's PCLMUL immediate-02/03 display aliases are accepted only for the
mnemonic comparison; exact GNU-as operand roundtrips still check the original
immediate. Scalar/vector agreement alone cannot validate shared catalogue facts.

Every decoded record also compares the optimized and full scalar text and listing
writers. All supported forms exercise capacities from zero through their planned
bound at a guard page. Command tests compare empty, one-instruction and 3,073-instruction
files against the original scalar output convention in both modes and paths, and
check delayed `/dev/full` failures. The larger file crosses record and text-buffer
boundaries.

The benchmark reports decode, a standalone prefix-classification diagnostic,
decode plus Intel formatting, and decode plus address/byte listing separately. The classification diagnostic scans
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
`--phase decode|classify|format|listing` isolates work for profiling; `--iterations N`
fixes the number of stream decodes per sample instead of calibrating to 2 ms.
`--after-scalar MS` runs the scalar decoder as warm work before each sample;
this is a transition-sensitive burst measurement, distinct from sustained
back-to-back runs. Hardware counters are zero when unavailable. Initialization,
input generation and file I/O are excluded; the decode-plus-format phase
includes formatting but excludes terminal I/O. Compiler memory barriers make all
published records and all rendered text observable. Initialization reports the
normal and address32 encoder prerequisites separately from decoder/formatter setup;
table bytes include the formatting plans and compressed lookup cells.

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

## Initial implementation measurements (PR #240)

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

At this revision formatting used the original scalar bounded writer. Adding it changes ordinary default
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

## Completion and optimization measurements (#242)

The implementation was frozen as `77233e1` before integration with newer main.
All decoder, formatter, command, test and benchmark source is unchanged by that
integration. Production measurements use the 9700X-built `build/compiler`,
Clang 23.1.1, `-O3 -march=native`, CPU 2 and a separate exclusive lock for every
process. The candidate binary SHA-256 is
`d8232adabfbca012bbc325aefcde722b52327ba7c379ea6fc35e4eaf2872519b`.
The baseline is `073c1bc` with only the same full-text compiler memory barrier
added to its benchmark; its binary SHA-256 is
`6b1f9b2186bdb6515b2b34e5a0c1ac350d134debabbcb620c05b6d37cea7b14d`,
and its modified benchmark source SHA-256 is
`76b200e5d57d91cb80f144ba4ab5ab18e6d35a417e1bab102fb13c7457b82223`.
This controls output observability; the earlier PR #240 formatter timings above
are historical evidence rather than the baseline for these percentages.

The ordinary/vector streams remain 55,852/91,105 bytes and 16,384 instructions.
The larger real workload is the complete `.text` of the original production
`073c1bc` compiler: 4,895,631 bytes and 991,561 decoded instructions, SHA-256
`2a9979146ea5ffa72daafa9517aaef91cc03ea87e9c84e9514a8b16805fd1d82`.
The same raw file is used for both binaries; it includes compiler-generated
ordinary and vector code with its original padding. Every benchmark verifies
complete coverage and scalar/vector records before timing.

Production seeds 1 and 99 and the ASan/UBSan seed-7 build passed all supported
catalogue instances, 100,000 generated/mutated instructions and 100,000 random
byte inputs per seed, the 4,096 dense ordinary streams, input/output guards and
22 command output/error checks per seed. The three runs exercised 18,971,344
guarded formatter/listing capacities. GNU as independently reproduced all
34,833 catalogue-stream and 30,005 random-stream instructions to identical bytes;
the extra NOPs separate standalone FWAIT. The whole compiler-text objdump oracle
passed in production and under sanitizers. The sanitizer build used
`-O1 -fsanitize=address,undefined -fno-omit-frame-pointer`, leak detection and
halt-on-UB, with no findings. Its binary SHA-256 is
`57c5bb1aac816668e8b76dc56b0a84e67ef88129aecc3d8f76825ccfff24d331`.

### Complete-path comparison

Each cell below is ns/instruction (hardware cycles/instruction), the median of
three process medians with 21 interleaved passes per process. The listing phase
includes address/byte text but no I/O.

| Stream | Baseline decode | New decode | Baseline decode + format | New decode + format | New decode + listing |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ordinary | 7.574 (42.005) | 7.636 (42.344) | 42.366 (234.853) | 25.281 (140.161) | 26.051 (144.454) |
| Vector mix | 28.314 (156.917) | 25.623 (142.023) | 76.595 (424.553) | 46.751 (259.226) | 47.703 (264.389) |
| Compiler text | 15.061 (83.514) | 14.592 (80.890) | 57.066 (315.555) | 32.212 (178.574) | 33.054 (183.242) |

Decode plus formatting takes 40.3%, 39.0% and 43.6% less time respectively.
Retired instructions for that path fall from 797.883 to 398.201 per ordinary
instruction, 1,624.606 to 952.493 per vector-mix instruction, and 1,080.300 to
591.161 per compiler instruction. These reductions accompany the cycle savings.

Decode alone is **0.8% slower on the ordinary mix**, 9.5% faster on the vector
mix and 3.1% faster on compiler text. The ordinary increase is retained, not
rounded into a universal win. The new default's vector decode is 6.4% faster
than the new basic SIMD matcher, eliminating the previous batching regression.
The controls below keep the full-validator scalar reference and the compact
scalar check control visible. Formatting is the old independent scalar writer
only in the full-scalar row; the other controls use the new planned formatter.

| Stream | Control | Decode ns/insn | Cycles/insn | Decode + format ns/insn | Decode + listing ns/insn |
| --- | --- | ---: | ---: | ---: | ---: |
| Ordinary | Full scalar | 31.955 | 177.194 | 62.644 | 63.294 |
| Ordinary | Compact scalar | 23.070 | 127.877 | 40.842 | 41.589 |
| Ordinary | Basic AVX-512 | 23.337 | 129.276 | 41.164 | 42.030 |
| Vector mix | Full scalar | 38.084 | 211.078 | 80.904 | 81.831 |
| Vector mix | Compact scalar | 27.536 | 152.652 | 48.573 | 49.404 |
| Vector mix | Basic AVX-512 | 27.374 | 151.764 | 48.714 | 49.595 |
| Compiler text | Full scalar | 37.016 | 205.170 | 73.064 | 76.768 |
| Compiler text | Compact scalar | 25.565 | 141.694 | 43.133 | 43.984 |
| Compiler text | Basic AVX-512 | 25.844 | 143.328 | 43.351 | 44.225 |

### Sustained work and work after scalar decoding

These use the same production candidate and default path in separate processes.
Sustained samples decode each synthetic stream 512 times, or compiler text four
times, per sample. Warm samples decode one stream immediately after 100 ms of
active scalar decoding; they do not begin from an idle sleep. Each row retains
all three process medians in run order, including the 8.117 ns ordinary decode
sample. A stream-sized warm sample and sustained execution are different scopes;
the larger real stream also averages over a longer interval.

| Stream | State | Phase | ns/instruction, three processes | Cycles/instruction, three processes |
| --- | --- | --- | --- | --- |
| Ordinary | Sustained | decode | 7.567 / 7.569 / 7.575 | 41.936 / 41.933 / 41.982 |
| Ordinary | Sustained | decode+format | 25.102 / 25.149 / 25.055 | 139.201 / 139.463 / 138.935 |
| Ordinary | Sustained | decode+listing | 25.960 / 25.931 / 25.958 | 143.873 / 143.738 / 143.833 |
| Ordinary | After 100 ms scalar | decode | 7.618 / 8.117 / 7.691 | 42.291 / 45.037 / 42.686 |
| Ordinary | After 100 ms scalar | decode+format | 25.498 / 25.795 / 25.427 | 141.436 / 143.062 / 140.999 |
| Ordinary | After 100 ms scalar | decode+listing | 26.258 / 26.767 / 26.206 | 145.692 / 148.422 / 145.339 |
| Vector mix | Sustained | decode | 25.423 / 25.465 / 25.537 | 140.888 / 141.094 / 141.551 |
| Vector mix | Sustained | decode+format | 46.551 / 46.220 / 46.433 | 258.118 / 256.150 / 257.343 |
| Vector mix | Sustained | decode+listing | 47.216 / 47.028 / 47.252 | 261.759 / 260.720 / 261.970 |
| Vector mix | After 100 ms scalar | decode | 26.170 / 25.817 / 25.828 | 145.195 / 143.213 / 143.059 |
| Vector mix | After 100 ms scalar | decode+format | 47.381 / 46.947 / 46.835 | 262.786 / 260.276 / 259.562 |
| Vector mix | After 100 ms scalar | decode+listing | 48.026 / 47.912 / 47.822 | 266.355 / 265.813 / 265.013 |
| Compiler text | Sustained | decode | 14.498 / 14.537 / 14.561 | 80.378 / 80.591 / 80.654 |
| Compiler text | Sustained | decode+format | 31.816 / 31.805 / 31.621 | 176.390 / 176.313 / 175.270 |
| Compiler text | Sustained | decode+listing | 32.687 / 32.715 / 32.383 | 181.178 / 181.337 / 179.459 |
| Compiler text | After 100 ms scalar | decode | 14.588 / 14.537 / 14.540 | 80.875 / 80.595 / 80.597 |
| Compiler text | After 100 ms scalar | decode+format | 32.113 / 31.845 / 31.832 | 178.064 / 176.561 / 176.482 |
| Compiler text | After 100 ms scalar | decode+listing | 33.043 / 32.735 / 32.738 | 183.201 / 181.454 / 181.390 |

### Command, startup and storage costs

The complete default command, including initialization, mapped input, formatting,
buffered writes and final flush, was also measured on compiler text with stdout
directed to `/dev/null`. These are `perf stat` task-clock CPU times, not terminal
wall times. Each entry retains all three separate processes.

| Command | Baseline CPU ms | New CPU ms | Baseline median cycles | New median cycles |
| --- | --- | --- | ---: | ---: |
| Listing | 310.34 / 305.52 / 312.73 | 36.48 / 36.02 / 36.03 | 1,707,840,058 | 199,126,977 |
| Assembly | 60.97 / 61.59 / 60.74 | 34.70 / 34.65 / 34.89 | 336,074,376 | 191,730,387 |

Listing uses 88.4% less CPU time (an 8.61-fold throughput ratio) and assembly
43.1% less (1.76-fold), based on the process medians. Avoiding fifteen formatted-print calls per listed instruction
explains the larger command gain; it must not be attributed to decoding alone.

Initialization is a measured tradeoff. In the nine main comparison processes,
the baseline took 1.456–1.638 ms (median 1.530), while the candidate took
1.608–1.919 ms (median 1.668). New decoder/formatter setup was 0.524–0.575 ms;
the remaining startup time builds the two encoder validation prerequisites.
Precomputing the index and text plans adds startup work, amortized by long streams.

Declared/allocated decoder tables total 964,920 bytes, including 12,224 padded
signature entries, 10,780 compressed cells, 334,080 bytes of text plans and
8,192 bytes of register tokens. The previous 2,467,968-byte figure reserved
worst-case signature capacity; its active signature arrays plus other declared
tables accounted for 420,864 bytes. Exact allocation reduces worst-case reservation
by 60.9%, while the new indices/plans increase active data. This is not a measured
RSS reduction. Both translation-unit instances still have independent static
metadata; only the invoked command or benchmark initializes its allocated storage.

### Final emitted code and remaining costs

The production decoder retains all 24-byte record stores, including the ordinary
lane loop's four-record unrolling. The compressed lookup emits `PEXT` with a
memory mask followed by the 16-bit cell load and ambiguity check. The register
form's uops.info cost guides the design; it does not include this mask load or
the rest of the lookup. Formatting emits XMM `VPSHUFB` and masked byte stores.
The listing prefix uses a masked input load, YMM `VPSHUFB`, ZMM `VPERMI2B`
(`VPERMT2B` when inlined into the benchmark) and a masked 46-byte output store.
Clang combines the byte permutation with the space insertion. The bounded
formatter calls and complete record stores remain in the observed benchmark;
the compiler barriers themselves require no machine instruction.

Separate CPU-2 cycle profiles of default decode plus formatting retained
121,342 vector-mix samples and 162,784 compiler-text samples, with no lost samples.
On the vector mix, candidate construction/validation accounts for 50.34%, the
formatter 32.62%, and the batched decoder wrapper 4.24%. On compiler text these
are 31.02%, 36.11%, and 12.93%. These are function-level samples, including
inlined work, not individual-instruction latency measurements. Scalar operand
selection, decimal conversion, validation, record stores and library copies
remain costs; the final comparison measures their combined effect. Profiled
runs are excluded from the timing tables above.
