# Native fixed-frame unwind information

`unwind_tier0.c` derives call-frame transitions from successful Tier0 instruction
records after branch sizing. `unwind.c` turns those descriptors into native
`.eh_frame` contributions. This is an opt-in consumer: it does not change
`tier0.c`, rerun its generic validators, or recover source rows. The companion
[debug emitter](debug.md) still needs caller-supplied source positions.

## Producer contract

Initialize the encoder, semantics, Tier0 and unwind tables once before concurrent
use. Pass a valid baseline `Tier0Code`, the `count` final offsets from successful
branch sizing, and the sized byte length. Freeze those records through encoding.
The offsets have no trailing sentinel. The consumer does not decode bytes,
remeasure lengths, or independently verify branch displacements and symbolic
references supplied by the producer.

Both `unwind_tier0` and `unwind_tier0_scalar` write one `UnwindFunction`, only on
success. They recognize an ordered subset of pushes of RBX, RBP, R12–R15, an
optional fixed positive stack allocation, and the matching final release,
reverse pops and return. The body must keep that frame. Every direct branch must
appear exactly once in the ordered branch columns, and its target must be in the
body or at the beginning of the common epilogue. Branches into a partially
restored frame, unlisted branches, extra returns and unsupported control flow
are refused. Early source returns are supported through Tier0's jumps to its
common epilogue.

The vector path resolves body effects eight records at a time, with a scalar
twin. It rejects unknown effects, body writes to RSP or unsaved nonvolatile
registers, and implicit stack operations except the producer's marked direct
call. Calls use the normal returning SysV convention; their fallthrough frame
is unchanged. Explicit RSP-relative stores must have a known fixed interval
outside the saved-register and return-address slots. Compiler-private slots
must also remain intact through ordinary pointer stores and callees. This is a
contract for valid generated code, not a proof of arbitrary memory aliases or a
validator for hostile machine instructions. Raw process entry, special calling
conventions, inline assembly and post-sizing transformations are outside it.

The descriptor's `saved` bits follow RBX, RBP, R12, R13, R14, R15. Its at most 14
offsets are instruction-end PCs: pushes, optional allocation, optional release,
then reverse pops. Unused offsets and `reserved` are zero. `unwind_check` is a
separate diagnostic boundary. The trusted writer does not repeat that proof;
it checks representability and output capacity before stores.

## Format and linker handoff

Every function gets an original eight-byte-aligned CIE/FDE pair. The CIE uses
version 1, `zR`, code alignment 1, data alignment −8, RIP as the return register,
and PC-relative signed four-byte initial addresses. The function range is
absolute. These fields follow the
[LSB exception-frame format](https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html).

The initial row defines CFA = RSP + 8, return address at CFA − 8, and explicit
same-value rules for the six nonvolatile registers. Every transition follows
its machine instruction. Pops restore the CIE's initial rule; this matters
because `DW_CFA_restore` restores that rule, not necessarily a register's prior
stack location. CFA offsets, advances and register rules follow
[DWARF 5, section 6.4](https://dwarfstd.org/doc/DWARF5.pdf).

`unwind_write(functions, count, output, exact)` fills caller-owned byte and
location slices. A capacity result reports the required byte size while
preserving both slices. Other errors preserve the output descriptor too. Inputs
and the two output slices must be disjoint. Empty input needs no storage.
Function sizes and frame allocations are limited to `INT32_MAX`; the complete
batch must fit `UINT32_MAX` bytes.

`output.locations[i]` names the zero initial-address field for function `i`.
Attach a Link PC32 fixup there, targeting that function's symbol with addend 0.
The corresponding pair begins 48 bytes before the location and ends at the
next pair, or `output.size` for the final pair. Use a separate EH_FRAME atom per
pair, aligned to eight bytes, so function ownership and section GC stay
explicit and the executable writer's bounded CIE walk stays small. No final
terminator is emitted. Late layout resolves these fixups and builds the existing
native unwind index; the emitter does not need final text addresses.

The scalar writer appends the CIE and instructions independently. The vector
writer classifies eight descriptors, forms CFA LEB bytes and expands original
templates with byte permutations and bounded stores. Its 384 shapes cover 64
save masks and no allocation or one of five CFA LEB widths. Each pair occupies
64–184 bytes. Initialization tables are shared and immutable afterward; frame
analysis needs only bounded stack scratch and a 68-byte descriptor per function.

## Checks and measurements

`test-unwind` checks scalar/vector identity, an independent CFI interpreter,
all 64 save masks across CFA LEB boundaries, capacity refusal, signed limits,
and protected descriptor/byte/location tails from zero through 65 functions.
The interpreter checks every row transition, including stack release and pops,
and the constant state between transitions. Corrupted return rules and ranges
are negative controls for the interpreter. Separate analyzer tests use real
generated typed functions and deliberate frame, control and store mutations.

`test-unwind --cross-check` constructs two actual Tier0 functions, including a
loop with a stack local and all six nonvolatile saves. Their instruction bytes
come from the native encoder and their CFI comes from this emitter. GNU as
supplies only an ELF container and the special process-entry wrapper's code and
CFI. The native executable writer attaches fixture source rows and checks lane
and scalar/vector byte identity. GDB stops at entry and every stack transition,
checking the three-frame chain and the caller's six register values. Perf must
recover a sampled leaf → outer → process-entry chain with source locations.
Those explicit fixture rows do not establish automatic source-row production.

`benchmark-unwind` uses frozen typed programs with integer, floating, call,
loop and pointer work. It compares the same vector Tier0 producer, branch sizing
and encoding with no CFI, scalar analysis/emission, and vector analysis/emission.
Per-function allocation and cleanup are inside timing. Program generation,
signature assignment, initialization and byte-identity preflight are outside.
It reports 21-pass median and range, nanoseconds and user cycles per typed entry,
whole-unit time, code/CFI bytes and temporary arena use. Options are
`--functions N`, `--only baseline|scalar|vector` and `--after-scalar MS`.
The latter runs the scalar CFI emitter alone before every timed path, with the
same duration for each; it does not condition with the vector backend chain.

All builds, tests and measurements use the approved 9700X through the shared
lock; measurements use CPU 2. Repeated independent processes and inspection of
generated assembly are required before a performance conclusion. File I/O,
source-map production, parsing and compilation/output overlap are excluded from
this bounded backend comparison. At this source checkpoint, target-host
validation and measured performance remain pending.

All implementation, templates, fixtures and test oracles here are original.
The format documents supply facts; GNU as, GDB and perf run externally. No
external implementation or fixture corpus is incorporated.
