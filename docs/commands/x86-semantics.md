# x86-semantics

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/x86_semantics.c` is what the forms a code generator
  emits mean (#221), in tables keyed by `X86Form` beside the encoder's:
  - each covered form's effects (`x86_effects`): its operands' reads and
    writes by encoding role (tied ones both), its fixed registers, what a
    narrower write does to the rest of a register, its flags read, defined
    and left undefined, its memory, control, faults and MXCSR, the x87's
    state and control word, and how its immediate's bits become a value.
    Every form Tier 0's emitter writes is covered (its x87, `shld`/`shrd`
    and AVX forms among them), and so is every VEX and EVEX form of the front
    end's X86 rows, with their families (#760: AVX-512's masked moves,
    compares into opmasks, conversions, FMA, the AVX-512 extensions' forms,
    gathers and scatters, the opmask registers' own forms, BMI's;
    AVX512-FP16's by rule, unexecuted on hosts without it), but the rows'
    legacy ones (#148's list). An uncovered form gets a conservative record,
    and a call has only its architectural effects (its callee's clobbers are
    #137's);
  - the immediate's contract (`x86_immediate_encodes`, `x86_immediate_field`
    and `x86_immediate_value`): a selector's value against a field's bits;
  - each record's resources read and written, its operands resolved
    (`x86_instructions_effects`, AVX-512, which leaves the rare records, the
    x87's, vzeroupper's and those with an operand no role names, an /is4
    register, a VSIB index or an opmask pair, to its scalar twin), and with
    `x86_instructions_effects_opmasks` each record's opmask registers in a
    column of their own (#760: without it, a record that touches one reads and
    writes everything);
  - value-preserving form choice for an operation with an immediate
    (`x86_choice_row`, `x86_choose_forms`, AVX-512, its scalar twin
    `x86_choose_forms_scalar`, and `x86_choice_instruction`): the
    sign-extended imm8, the accumulator's form, constant materialization, and
    other instructions (xor, test r, r, a narrower test, opt-in inc and dec)
    only where the live flags allow, which initialization works out from the
    effects.

  Its consumers read it rather than keep copies of its rules: Tier 0 chooses
  its immediates' forms by it (its emitter's computed constants by
  `x86_immediate_encodes`) and asks it which VEX and EVEX forms a row may have
  (`tier0_operation_form_valid`, #760), Tier 1's selector by its rows, the scheduler and
  the unwind analyzer by the resolved effects, and the assembler's matcher
  (`x86_match.c`) takes value immediates by `x86_immediate_encodes`: so
  `main.c` includes `x86_semantics.c` before it, and `x86_match_initialize`
  needs `x86_semantics_initialize` first (`x86_semantics_initialized`), which
  `tier0_initialize` runs where it hasn't run.
  `x86_semantics_test.c` holds its tests (`main.c` includes it after the
  assembler's, taking the encoder tests' printer and branch sizing tests'
  runner; `./build/compiler test-x86-semantics` runs them) and
  `x86_semantics_benchmark.c` provides `benchmark-x86-semantics`.
  `docs/x86-semantics.md` has the contract, the covered subset, how to add a
  form and the measurements.
## x86 semantics tests

`./build/compiler test-x86-semantics [--seed N] [--runs N] [--cross-check]
[--keep]` (about 0.3 s on the 9700X) checks `x86_semantics.c`. The processor is
the oracle: a harness runs records in the test's process, loading every
general register, the flags, ZMM0 to ZMM31, K0 to K7 and the x87's state
(FXRSTOR64 of a random one, every exception masked; the C code's saved around
the run) from a state, then storing them back; a host without AVX512F and
AVX512BW (`kmovq`'s) gets XMM0 to XMM15 alone, and its VEX and EVEX forms aren't
run. Memory operands are RIP-relative into a data
page, RSP is its own stack, and a fault names the record. The checks:

- the effects table: every covered form but those that branch or trap, from
  `--runs` random states (12), with register and memory operands in turn, and
  where an EVEX form takes them XMM16 to XMM31 and an opmask half the time
  (zeroing half of those), a gather's or scatter's elements in the data page
  through a base and a vector index (#760).
  Each run checks that:
  - what the form doesn't write stays as it was: registers, vector registers
    to bit 511, opmask registers, flags, the x87's state and control word, and
    the memory around its operand and below the stack;
  - a narrower write keeps or zeroes the rest as its record says (a VEX or
    EVEX one zeroes it to bit 511), and vzeroupper zeroes ZMM0 to ZMM15 above
    bit 127;
  - defined flags and results don't change when everything it doesn't read
    changes, every register compared whole (vector ones to bit 511, the x87's
    state too);
  - a destination that only merges doesn't read the bytes it defines;
  - every write shows a change in some run: one that no run showed gets up
    to 256 more runs, with register operands, and one that never shows is
    listed as missing coverage, not failed.
- cmov and setcc of all 16 conditions on all 64 combinations of the status
  flags, against a model of the conditions;
- shifts' CF: SAR, SHR and SHL at every width, by an immediate and by CL,
  around their widths, against a model (SAR's CF by the width or more is the
  sign bit, defined), with `x86_instruction_flags` defining CF where the model
  does; and pairs of records through `x86_instruction_flags_substitutable`, run
  from every state of the status flags: a live flag it takes must come out
  alike, and `or ebx, 0` for `shl ebx, 0`, refused with CF live, must show CF
  apart;
- fixtures:
  - division's, mul's and the sign extensions' fixed registers, variable
    shifts' CL, partial writes and 32-bit zero extension, inc's CF, the SSE
    merges and legacy SSE's upper lanes;
  - a call's architectural effects, register and memory operands, zero
    idioms (vpxord's of its sources), shift counts (shld's and shrd's), the
    x87's state and control word, vzeroupper and AVX moves; and opmasks (#760),
    by both paths: a masked move that merges and one that zeroes, a masked
    store, a compare into an opmask, kand, kmov both ways, a gather and a
    scatter (EVEX's opmask, VEX's vector mask), vpblendvb's /is4 register, a
    masked zero idiom and vp2intersect's pair, each also everything without
    the opmask column;
  - the substitutions' legality for every set of live flags;
  - uncovered forms, which must get the conservative record;
- the immediate's contract on every covered form with an immediate and about
  60 boundary values: a field that takes a value gives it back, and one that
  doesn't can't (an imm8 of `0xc8` is −56, not 200);
- form choice:
  - fixtures of the values −129 to 256 at each width, constant
    materialization (INT32_MAX, 0x80000000, UINT32_MAX, negative 64-bit
    values), shift counts and the substitutions by their live flags;
  - every row with the boundary values and random ones, six live-flag
    contexts and three destinations. Each chosen record is validated and run
    against a reference sequence that holds the value in R11 and uses the
    operation's register form, and must leave the same registers, memory and
    live flags;
  - every candidate chosen by some request;
- `x86_choose_forms` and `x86_instructions_effects` against their scalar
  twins on random inputs, and with every column ending at a `PROT_NONE` page at
  every count to 100;
- #180's check: every row's forms allowed at the baseline target
  (`x86_target_forms`), every chosen record valid there
  (`x86_target_validate`);
- `x86_match.c`'s immediates against the shared rule on the forms the
  assembler's syntax supports with a value immediate: its scalar rule
  (`x86_match_immediate`) and its vector path's intervals (`x86_match_range`)
  must take exactly the values `x86_immediate_encodes` takes, the unsigned
  spellings of sign-extended patterns among them (`add eax, 0xffffffff` is an
  imm8's −1), and no `__int128` past the width; and every form it reads as
  sign-extending must be such a form.

`--cross-check` also prints every chosen record with the encoder tests'
printer: GNU as must assemble them to the chosen forms' bytes. `--keep` leaves
its directory under `$TMPDIR` (or `/tmp`). The 9700X job's test step runs it
with `--cross-check`.

Its consumers' tests check them against it: `test-tier0` that every record Tier 0
emits has a covered form, an X86 entry's too (#760), `test-assembler --cross-check` the
matcher's value immediates against GNU as (the unsigned spellings among them),
and test-checker's folds section (`c_check_program_folds_test.c`) where Tier 0
folds loads, from its records' resolved effects: a volatile object read as often
as the source reads it, a load through a pointer kept on its side of a store, an
atomic increment's write locked.

## x86 semantics benchmark

`./build/compiler benchmark-x86-semantics [--requests N] [--functions N]
[--seed N] [--after-scalar MS] [--only NAME]` (about 3 s) checks that the
vector paths agree with their scalar twins, then reports:

- the tables' bytes, and `x86_semantics_initialize`'s time;
- form choice per request: `--requests` requests (65,536) shaped like a code
  generator's constants, by `choose-scalar` and `choose-avx512`. Also the
  chosen forms' bytes against an imm32 for every value;
- operand-resolved effects per record, on `--functions` functions (2,048) of
  96 records shaped like clang's -O0 and -O2 code (branch sizing's generator),
  by `effects-scalar`, `effects-avx512` and `effects-opmasks` (the AVX-512
  path with the opmask registers' column, #760), beside `encode` (`x86_encode`
  of the same records);
- `code`: the run time and size of the choices that change the instruction
  sequence or its encoding (an imm8 against an imm32, inc, xor, test r, r, a
  narrower test, `mov r32` against movabs), in blocks of 64 over 8 registers.

Each case reports 21 interleaved-pass medians: nanoseconds, cycles,
instructions, IPC, and the throttle (PMCx1A0's umask 0x08, as in #199).
`--only NAME` times one path alone, back to back. `--after-scalar MS` runs the
case's scalar twin for MS ms (100 to 300) before every AVX-512 sample. Run it
under the 9700X's lock with `taskset -c 2`. `docs/x86-semantics.md` has the
numbers in both states. The default 9700X job runs it after the tests; like
the others, its measurements are informational.
