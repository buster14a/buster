# x86-encoder

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/x86_64_encoder.c` is the x86-64 encoder (the form table
  of legacy, VEX and EVEX forms, the scalar reference and the AVX-512 encoder,
  and the fixup interface: `BUSTER_X86_FORM_FIXUP` marks an instruction with a
  symbolic operand, `x86_encode` reports each marked instruction's end, and
  `x86_fixup_distance` gives its fields' offsets and addends from that; and
  `x86_encode_address32` and `x86_encode_apx`, the same for 32-bit addresses and
  for APX's general registers 16 to 31 (REX2, and EVEX's B4 and X4), each with
  templates of its own; and `x86_encode_mode32` and `x86_encode_mode16` (and
  `x86_encode_scalar_32` and `x86_encode_scalar_16`), the same for 32-bit and
  16-bit modes, with their operand and address sizes, segment overrides and own
  instructions, and templates of their own). `x86_measure` and
  `x86_measure_scalar` return the offsets and total length for valid ordinary
  64-bit-mode records without a machine-code buffer. They require initialized
  templates and room for `count` u32 offsets, disjoint from the records, with no
  padding, permit null pointers for zero count, and reject counts past
  `BUSTER_X86_ENCODER_MAX_INSTRUCTIONS` before touching either array. The
  scalar twin uses the independent scalar form rules. There are no measure
  variants for the other modes or fixup-end output.
  `docs/x86-64-encoder.md` explains its design and measurements.
  `x86_64_encoder_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-x86-encoder` runs them) and
  `x86_64_encoder_benchmark.c` provides `benchmark-x86-encoder`.
## x86-64 encoder tests

`./build/compiler test-x86-encoder` (about 1.8 s) checks that every form fits a
template (and that building each, in every mode, at a `PROT_NONE` page writes
nothing past it), both encoders on 761 known encodings from GNU as (each also
with the fixup flag, whose end they must report), a digest of the scalar
encoder's output for 100,000 random instructions, every batch width and variant
of `x86_encode` against the scalar encoder (bytes, offsets and fixup ends, in
each of the four loops: with neither, either or both of offsets and fixup ends)
on every count from 0 to 199 and on 300,000 random instructions of every form
(legacy with x87 and MMX, VEX and EVEX, with opmasks, broadcasts, rounding,
registers 16-31, gathers and scatters, FS and GS segment overrides, and one in
eight marked with the fixup flag), the same between `PROT_NONE` guard pages, and
that `x86_validate` finds every invalid operand and the scalar encoder rejects
it (memory operands in the 232 FS and GS forms without room for the override
among them). `x86_encode` checks nothing: it takes what `x86_validate` accepts,
as the code generator makes it (an immediate or rel8 outside its field's range,
for one, gets its low bytes). Then it runs those checks again with 32-bit
addresses, but the known fixups: 34 known encodings, a digest of their own,
`x86_encode_16_address32` and `x86_encode_64_address32` against the scalar
encoder on the random streams and at page edges, and invalid operands, among
them MPX memory operands and memory operands in the 312 FS and GS forms and the
116 forms without room for the `67`. And again for APX, but the known fixups:
153 known encodings, 31 known invalid instructions (as GNU as rejects them), a
digest of their own, `x86_encode_16_apx` and `x86_encode_64_apx` against the
scalar encoder on random streams in which half the forms have REX2 and a quarter
are EVEX ones, and at page edges, and invalid operands, among them a high byte
register with REX2, registers 16 to 31 in forms without REX2 or EVEX and XMM16
to XMM31 in SSE's forms. And in 32-bit and 16-bit modes: 159 and 150 known
encodings, 13 and 16 known fixups, a digest of each, the modes' loops at 16 and
64 instructions per iteration (`x86_encode_16_mode32` and the rest) against the
scalar encoders on random streams with both address sizes and segment overrides
in the decorations, and at page edges, instructions longer than 15 bytes (16-bit
mode has them), which `x86_validate_16` must reject, and invalid operands, among
them registers that would need REX, 16-bit addresses that aren't ModRM's, forms
of 64-bit mode alone, instructions past 15 bytes and segment overrides where an
instruction takes none. `--address32`, `--apx`, `--mode32` and `--mode16` run
only their pass, and `--count N` and `--seed N` change the random streams.

The ordinary 64-bit pass also compares `x86_measure` and `x86_measure_scalar`
with both encoders and the known encodings: total length and every start
offset, all admitted forms with valid FS/GS overrides and fixup markings,
random streams, vector and former chunk boundaries, and rebased partitions.
It checks empty and excessive counts without accessible arrays, and bounds
with guard pages and offset canaries at every dword alignment.

The digests were recorded when GNU as encoded the same streams to the same
bytes. After changing the form table, the scalar encoder or the fixup interface,
cross-check with GNU as (binutils 2.47 for the recorded digests) for every entry
point, then record the digests the test prints:

```sh
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 > build/x86.s
as build/x86.s -o build/x86.o
./build/compiler test-x86-encoder --compare-object build/x86.o 400000 --seed 1
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --address32 > build/x86_32.s
as build/x86_32.s -o build/x86_32.o
./build/compiler test-x86-encoder --compare-object build/x86_32.o 400000 --seed 1 --address32
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --apx > build/x86_apx.s
as build/x86_apx.s -o build/x86_apx.o
./build/compiler test-x86-encoder --compare-object build/x86_apx.o 400000 --seed 1 --apx
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --mode32 > build/x86_mode32.s
as build/x86_mode32.s -o build/x86_mode32.o
./build/compiler test-x86-encoder --compare-object build/x86_mode32.o 400000 --seed 1 --mode32
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --mode16 > build/x86_mode16.s
as build/x86_mode16.s -o build/x86_mode16.o
./build/compiler test-x86-encoder --compare-object build/x86_mode16.o 400000 --seed 1 --mode16
```

(`build.sh` deletes `build/`, so run these after building.) The printed assembly
uses only operands GNU as encodes with the same form (`{load}`/`{store}`,
`{vex}` and `{evex}`, immediates outside the imm8 range for imm32 forms, `{nf}`
for APX's NF forms, `{dfv=...}` for `ccmp`'s and `ctest`'s default flags value,
and so on), so any difference is a bug in one of them. A quarter of the fields
that can be symbolic (32-bit displacements, rel32s, `loop`'s and `jrcxz`'s
rel8s, imm32s and 64-bit immediates) are printed as symbols plus their values;
GNU as writes zeros and a relocation for them, and `--compare-object` reads
`.text`, `.lengths` and `.rela.text` from its object and compares the bytes
(with those fields zeroed where the reported fixup ends put them) and each
relocation's offset, type, addend and symbol with the ones the fixup ends and
`x86_fixup_distance` give, and the mode's vectorized encoder's bytes and fixup
ends with the scalar encoder's. With `--address32`, the printer adds `addr32`
where no general register gives the address size, and the relocations of
displacements other than RIP-relative ones are R_X86_64_32; with `--apx`, it
names registers 16 to 31 `r16` to `r31` (`r16d`, `r16w`, `r16b`); and with
`--mode32` and `--mode16`, the printed assembly is in `.code32` or `.code16`,
and the printer writes the mode's mnemonics and prefixes (`addr16`, `data32`,
`jcxz`, `lcall`, segment overrides as prefixes, which GNU as keeps on a default
segment, and `.arch i386` around a test register's move), whose relocations are
never PLT32 or 32S. Cross-check more than one seed after changing forms (1.7
million instructions over seeds 1, 7 and 99 were last checked for each 64-bit
entry point, with 198,573, 197,926 and 197,735 relocations, and 1.2 million for
each of 32-bit and 16-bit mode, with 135,563 and 136,322).

## x86-64 encoder benchmark

`./build/compiler benchmark-x86-encoder [--count N] [--section N] [--uniform |
--vector] [--mode32 | --mode16] [--offsets] [--fixups] [--after-scalar MS]
[--only NAME]` checks every implementation against the scalar loop, then
reports 21 interleaved-pass medians per encoded instruction (time, cycles,
instructions, IPC, and the throttle: PMCx1A0's undocumented umask 0x08, raw
event `0x1000008a0`, which #199 reads as the dispatch slots Zen 5's power cap
takes). The stream is
a compiler-like mix of 16,384 legacy instructions by default (comparable with
older measurements), a mix like vectorized code (AVX-512, AVX2, BMI) with
`--vector`, or random instructions of every form with `--uniform`; `--section N`
encodes it N instructions per call, `--offsets` stores every instruction's
offset, and `--fixups` marks the calls and RIP-relative operands and reports
their ends. The variants are the batch widths (`x86_encode_4` to
`x86_encode_64`, instructions per loop iteration), the template gather, output
store and fixup store alternatives that lost, `x86_encode_64_offsets_then_pick`,
the other way to have fixup ends (every offset, then the caller picks the marked
ones), and `x86_encode_64_apx` and `x86_encode_16_apx`, APX's loops (the streams
have no registers 16 to 31, so their bytes are the same). `--after-scalar MS`
runs the scalar loop (256 instructions per call) for MS ms before each timed
pass of the other implementations: the state a compiler runs its vector phases
in. Lighter code lets the 9700X's clock boost over 30 to 300 ms, and at that
clock a heavy AVX-512 loop runs under the core's power cap for 5 to 20 ms
([#199's report](https://github.com/buster14a/buster/issues/199#issuecomment-6297314071)
has the controls), longer than a pass. Use 100 to 300: from a settled clock
30 ms barely boosts it, and 100 ms reaches the state whatever the passes before
did. Compare
time as well as cycles, since the clock differs by 2% to 5% between the states.
Measure the back-to-back state with `--only NAME`, which runs one loop alone:
in full runs without the option, the loops after the scalar loop's own passes
(54 ms of the compiler mix) run partly throttled, the more so the nearer they
follow it. The entry points use 16 per iteration: after scalar code it is 0.3%
to 7% faster than 64 on every stream, while back to back 64 is up to 6% faster
on some (`docs/x86-64-encoder.md`'s "Zen 5's costs" and "Both states" have the
numbers). `--mode32` and `--mode16`
time the mode's loops at 64 and 16 per iteration (`x86_encode_64_mode32` and
`x86_encode_16_mode32`, or the 16-bit ones) and its scalar loop instead, on a
mix like 32-bit compiler output or like real-mode code (or with `--uniform`, the
mode's forms). Decide them, and any other performance question, with
measurements on the 9700X (not Zen 4, and not llvm-mca, whose predictions are
well off), using uops.info's Zen 5 port data to explain them;
`docs/x86-64-encoder.md` has the current numbers. Clang's code for the loop
changes a lot with small source changes, so compare every width after changing
the encoder, and profile which loop the samples land in. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the lexer's, its
measurements are informational.
