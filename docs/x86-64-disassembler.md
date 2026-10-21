# x86-64 disassembler

`src/buster/compiler/x86_64_disassembler.c` decodes bounded raw machine-code
slices into the encoder's semantic instruction records and retains each input
instruction's offset and encoded length. Its AVX-512 path and scalar reference
share the form catalogue and operand validation. It does not execute input.

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

The decoder accepts non-shortest disp32 addressing and VEX3 encodings. It
validates decoded operands without using byte-for-byte re-encoding as a validity
test. `X86Decoded.length` is the actual input length; canonical re-encoding may
have another length. The instruction record carries semantic displacement and
immediate fields, not a promise to reproduce the original encoding choice.

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
The keys include mandatory legacy prefixes, W, vector length and EVEX.b;
embedded-rounding forms mask the length bits used for rounding. A candidate
match must still pass fixed-opcode, ModRM and full operand validation.

The vector path classifies 64 input bytes at once using two nibble-table byte
shuffles and a mask intersection. It reuses the prefix mask across instructions
and matches eight candidate signatures in one vector comparison. Scalar lookup
walks the same bucket and exits on the first complete match. Both paths produce
the same records, counts, status and consumed offset, including malformed input.

The experimental `batched` path computes speculative instruction lengths at
64 byte positions in parallel. A 256-byte opcode metadata table is derived
from unprefixed one-byte catalogue forms whose layouts agree. Two 128-byte
in-register lookups select ModRM/immediate metadata; byte masks derive optional
REX, SIB and displacement lengths. A short dependency walk chooses up to sixteen
reachable starts, after which the ordinary decoder validates every record. An
unknown hint or a disagreement returns to the actual decoded boundary. Hints
never authorize an unchecked read, record or skipped input byte.

Every vector source load is masked to the logical slice, including overlapping
windows and zero-length tails. Neither input padding nor readable bytes after
the slice are required. Output stores publish one complete record at a time
within capacity. Initialization must finish before concurrent readers begin;
the tables are read-only thereafter and the decode calls allocate nothing.

The design follows uops.info's measured Zen 5 costs:
[VPSHUFB](https://uops.info/html-instr/VPSHUFB_ZMM_ZMM_ZMM.html#ZEN5) is one
executed uop with one-cycle measured latency and 0.5-cycle throughput;
[VPERMI2B](https://uops.info/html-instr/VPERMI2B_ZMM_ZMM_ZMM.html#ZEN5) is one
uop with four-cycle latency and 0.5-cycle throughput.
[VPERMB](https://uops.info/html-instr/VPERMB_ZMM_ZMM_ZMM.html#ZEN5), used to extract
fields at known offsets, has the same four-cycle latency and 0.5-cycle throughput.
All three use FP1/FP2.
[VPGATHERDD](https://uops.info/html-instr/VPGATHERDD_ZMM_K_VSIB_ZMM.html#ZEN5)
costs 64 executed uops and about 12.6 cycles per instruction. The decoder uses
contiguous loads and in-register tables, with no gather. These measurements
explain candidates; actual 9700X measurements select the implementation.

## Validation and measurement

Tests cover the supported catalogue with both address sizes, random encoded
instructions, random bytes, all 64 alignments of independent semantic fixtures,
truncation, bounded formatting and input/output guard pages. The fixtures include
non-shortest displacements and VEX prefixes, high EVEX registers, masks, signed
relative targets and immediates, broadcasts and rounding. GNU objdump supplies
an independent boundary/mnemonic oracle; GNU as reassembles formatted operands
for a generated stream whose encoding choices are pinned independently of the
decoder. Scalar/vector agreement alone cannot validate shared catalogue facts.

The benchmark reports decode, prefix classification, and decode plus formatting
separately. The ordinary stream mixes arithmetic, loads/stores, addressing,
branches, stack operations and SSE. The vector stream adds masked AVX-512
arithmetic, broadcasts, FMA, compares, permutations, compression, AVX2 and BMI
with ordinary loop instructions. `--file` uses an actual raw-byte corpus and
fails on incomplete coverage instead of timing only a supported prefix.

Each result is a median of 21 interleaved passes with retained batches of at
least 2 ms, reporting ns, hardware cycles and instructions per instruction
(per byte for classification). The min/max are observed samples, not confidence
intervals. `--only scalar|avx512|batched` isolates an implementation.
`--after-scalar MS` runs the scalar decoder as warm work before each sample;
this is a transition-sensitive burst measurement, distinct from sustained
back-to-back runs. Hardware counters are zero when unavailable. Initialization,
input generation and file I/O are excluded; the decode-plus-format phase
includes formatting but excludes terminal I/O.
