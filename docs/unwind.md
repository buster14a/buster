# Native unwind information

`unwind_tier0.c` derives call-frame transitions from successful Tier0 instruction
records after branch sizing, or reads them from the frame Tier 0 published
(`unwind_tier0_frame`, #395). `unwind.c` turns those descriptors into native
`.eh_frame` contributions. The C driver writes them for every function it
compiles, as clang has unwind tables ("The C driver's unwind tables" below); the
analyzer stays an opt-in consumer, which recovers no source rows. The companion
[debug emitter](debug.md) gives the source rows.

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
registers, and implicit stack operations except the producer's calls: its marked
direct call and its indirect one through r11. Calls use the normal
returning SysV convention; their fallthrough frame is unchanged. Explicit
RSP-relative stores must have a known fixed interval outside the saved-register
and return-address slots. Compiler-private slots must also remain intact
through ordinary pointer stores and callees. This is a contract for valid
generated code, not a proof of arbitrary memory aliases or a
validator for hostile machine instructions. Raw process entry, special calling
conventions, inline assembly and post-sizing transformations are outside it.

The descriptor's `saved` bits follow RBX, RBP, R12, R13, R14, R15. Its at most 14
offsets are instruction-end PCs: pushes, optional allocation, optional release,
then reverse pops. Unused offsets and `reserved` are zero. `unwind_check` is a
separate diagnostic boundary. The trusted writer does not repeat that proof;
it checks representability and output capacity before stores.

A function with a frame pointer (`UnwindFunction.pointer`) pushes RBP and points
it at the push first, then pushes an ordered subset of the other five (`saved`
without RBP's bit); its reservation, and whatever its body allocates on the stack
as it runs (a variable-length array's storage), are below the pushes, so its CFA
is RBP + 16 from the mov until RBP's pop, wherever RSP goes, and `frame_size` is
0. Its 2n + 3 offsets are RBP's push, the mov, the other pushes, the other pops
and RBP's pop: the release before the pops (`lea rsp, [rbp - 8n]`, or
`mov rsp, rbp` without other pushes) moves no CFA and has none.

### The published frame

`unwind_tier0_frame(code, offsets, size, output)` takes the frame Tier 0
published with its code (`Tier0Code.frame_saved`, `frame_size` and
`frame_pointer`: what `tier0_emit_prologue` pushed and reserved) instead of
proving one. Tier 0's prologue is the code's first records and its epilogue's
release, pops and ret its last, so each transition is the end of a record the
frame names by its index: the reader checks that each of those records is the
instruction it must be (and that the last is the ret) and takes each end from
the final offsets, with no pass over the body. It takes the producer's word that
the body keeps the frame, which `unwind_tier0` proves for the fixed frames it
takes: the tests compare the two on every function both take
(`unwind_tier0_test_generated`), check every frame pointer's body against the
semantics tables' effects (no write to RBP), and run both kinds under GDB.
Its work is constant per function and scalar: there is no column of many items to
take in lanes. It refuses a frame its records aren't (`UNSUPPORTED`), invalid
inputs (`INVALID`) and sizes past the format's (`LIMIT`), each leaving the
descriptor alone.

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

With `output.shared` (#395), the batch is one CIE and the FDEs after it, each
pointing back to it, as a compiler's `.eh_frame` is: the CIE first, each FDE 8
bytes before its location, to the next FDE or `output.size`. Keep the whole batch
as one EH_FRAME atom: an object's `.eh_frame` holds its atoms as one section,
which a linker's CIE walk would otherwise count by the function (the executable
writer's indexes at most eight CIEs per atom, and `link-foreign` refused the
pairs of more functions), and an executable's index takes one CIE and its FDEs
in its vector path. A shared batch is the pairs' less 40 bytes per function but
the first.

The scalar writer appends the CIE and instructions independently. The vector
writer classifies eight descriptors, forms CFA LEB bytes and expands original
templates of FDEs with byte permutations and bounded stores, the CIE copied
before each (or once). Its 384 shapes cover 64 save masks and no allocation or
one of five CFA LEB widths; the CIE pointer is a hole, as the range and the CFA
bytes are. A frame pointer's function, the rare shape, it leaves to the exact
writer, lane by lane, having sized its FDE in its lane (42 + 13n bytes to a
multiple of 8). Each FDE occupies 24–144 bytes, and each pair 40 more.
Initialization tables are shared and immutable afterward; frame analysis needs
only bounded stack scratch and a 68-byte descriptor per function.

## The C driver's unwind tables

The C driver (`run_check_command`) writes unwind tables when clang's driver would
have them (`predefines_unwind_tables`, the rule `__GCC_HAVE_DWARF2_CFI_ASM`
follows): asynchronous ones by default, unless `-ffreestanding`, the last of
`-fasynchronous-unwind-tables` and `-fno-asynchronous-unwind-tables` saying
otherwise; synchronous ones with `-funwind-tables`; and with `-fexceptions`, `-g`
or `-gmodules`. Where clang's tables would be off, the driver's are a superset of
clang's call frame information: with debug information alone, clang writes it to
`.debug_frame`, which only debuggers read, and the driver to `.eh_frame`, which
debuggers read too, and so does the runtime's unwinder (`backtrace()`, a C++
exception or a thread's cancellation passing through the function); with
`-fexceptions` alone, clang gives an FDE only to a function that may unwind (one
that calls), and the driver to every function.

`check_program_emit` (with `unwind`) reads each function's published frame
after branch sizing, its bodies' then its helpers' (a frame that isn't its
records is an internal compiler error), and after the last function writes their
FDEs with one shared CIE into one EH_FRAME atom, each FDE's initial location a
PC32 fixup to its function's symbol. An executable's writer indexes them in
`.eh_frame_hdr` with the C runtime's; `-c`'s object has them as its `.eh_frame`,
with a relocation per function.

`test-check-debug --cross-check` (`c_check_unwind_test.c`) runs a fixture whose
function counts the frames above it with glibc's `backtrace()`, from `main`,
from a function with a variable-length array that a computed goto's label calls,
and from one with values live across the call: by default and with
`-funwind-tables` or `-g` after `-fno-asynchronous-unwind-tables`, the
unwinder walks through every function to libc's start (by each path, the same
bytes); with `-fno-asynchronous-unwind-tables` alone, it stops at the first.
The object has one CIE and an FDE per function, each relocated, and walks alike
linked by GNU ld, lld and `link-foreign`; without tables, it has no `.eh_frame`.
GDB's backtrace at every instruction of each function, stepping over calls,
gives the function and its callers.

## Checks and measurements

`test-unwind` checks scalar/vector identity, an independent CFI interpreter,
all 64 save masks across CFA LEB boundaries and the 32 a frame pointer leaves,
as pairs and with one CIE, capacity refusal, signed limits, each rule a frame
pointer's descriptor must keep, and protected descriptor/byte/location tails
from zero through 65 functions, a frame pointer's every third. The interpreter
checks every row transition, including stack release and pops, and the
constant state between transitions; a frame pointer's rows by the CFA's register
too. Corrupted return rules and ranges are negative controls for the
interpreter. Separate analyzer tests use real generated typed functions and
deliberate frame, control and store mutations. The published frame's reader
gets the analyzer's descriptor on every generated function the analyzer takes
(fixed frames, among programs with records by value, deep values and symbols
through the GOT and TLS), and on those with variable-length arrays, which have a
frame pointer and which the analyzer refuses, its prologue's and epilogue's
records' ends, their bodies leaving RBP alone; then every mask's fixture with
and without a frame pointer at every kind of reservation, 34 changes to
prologues, epilogues, frames and inputs (33 refused with the descriptor left
alone; a frame pointer's reservation the producer doesn't publish, which its CFA
doesn't follow, taken), and the guarded tails.

`test-unwind --cross-check` constructs two actual Tier0 functions, including a
loop with a stack local and all six nonvolatile saves, then again with a leaf
that allocates on the stack first, which has a frame pointer and its other five
saves, its CFI with one shared CIE. Their instruction bytes come from the native
encoder and their CFI comes from this emitter, each leaf's from the published
frame (and the fixed one's from the analyzer too, the same). GNU as supplies
only an ELF container and the special process-entry wrapper's code and CFI. The
native executable writer attaches fixture source rows and checks lane and
scalar/vector byte identity. GDB stops at every instruction of the leaf outside
its loop (its entry and every stack transition among them), checking the
three-frame chain and the caller's six register values. Perf must recover a
sampled leaf → outer → process-entry chain with source locations. The leaf that
allocates writes its allocation: perf's samples copy the stack from RSP, and the
kernel copies nothing of a page never touched (it can't fault one in at the
sample), so an untouched allocation's samples have no stack to unwind. Those
explicit fixture rows do not establish automatic source-row production.

`benchmark-unwind` uses frozen typed programs with integer, floating, call,
loop and pointer work. It compares the same vector Tier0 producer, branch sizing
and encoding with no CFI, scalar analysis/emission, vector analysis/emission,
and the C driver's: the published frame and the vector writer's batch with one
CIE (`published`), whose descriptors must be the analyzer's and whose bytes the
exact writer's.
Per-function allocation and cleanup are inside timing. Program generation,
signature assignment, initialization and byte-identity preflight are outside.
It reports 21-pass median and range, nanoseconds and user cycles per typed entry,
whole-unit time, code/CFI bytes and temporary arena use. Options are
`--functions N`, `--only baseline|scalar|vector|published` and `--after-scalar MS`.
The latter runs the scalar CFI emitter alone before every timed path, with the
same duration for each; it does not condition with the vector backend chain.

All builds, tests and measurements use the approved 9700X through the shared
lock; measurements use CPU 2. File I/O, source-map production, parsing and
compilation/output overlap are excluded from this bounded backend comparison.

### Qualification on 2026-10-23

[Required run 39959200295](https://github.com/buster14a/buster/actions/runs/39959200295)
passed all 57 test commands on merge result
`0324a8514398a5e38b5d4b0165724d9ae900d2cb`, combining submitted
`02f25658311a2385376af8f56a13885a85d7684e` with main
`de751b9996157d61d3a112f91a5d7d86e056811e`. It passed 704 emitter combinations,
protected tails, 3,631 analyzer checks across 79 actual functions, native writer
checks, GDB at entry and all 14 stack transitions, and the native perf chain.
The GDB fixture checks all six caller registers as well as the three frames.

The controlled measurements below use the separately frozen source checkpoint
`422d099500db10578d6722dfb2261589f48d2948`, whose six unwind modules and
Tier0/branch-sizing/semantics/encoder producers match that submitted head.
Other main changes are present in the hosted merge, so its evidence is kept
separate. The 155-file source/build archive has SHA256
`13fa05ee0f19780308880a633ae551797a7568e469a69dae61f7f24d1a16cca4`;
the resulting compiler has SHA256
`bf349d71d5c0267f570f5d33dd1e0812db78b7411bd2978b3b07aa15124d3cd0`.
Its independently locked `test-unwind --cross-check --keep` also passed all
checks, including native GDB and perf, on CPU 2.

Three independent processes ran each workload. Each command took the shared
lock separately; each process rotated the three paths over 21 samples after
the byte-identity preflight. The table gives the range of process medians in
nanoseconds per typed entry, with all analysis, emission and per-function arena
cleanup charged to the relevant path.

| Functions | Typed entries | Without CFI | Scalar CFI | Vector CFI |
|---:|---:|---:|---:|---:|
| 8 | 202 | 16.812–16.911 | 24.950–25.149 | 22.168–22.515 |
| 256 | 20,299 | 19.543–19.703 | 33.219–33.339 | 28.153–28.203 |
| 4,096 | 337,279 | 20.521–20.549 | 33.907–34.024 | 28.974–29.085 |

Using the median of each three-process set, vector CFI lowers the complete
backend time versus scalar CFI by 10.7%, 15.2% and 14.5%, respectively. It still
adds 32.6%, 44.2% and 41.5% versus producing no CFI. This is a material cost of
deriving frame information from records, and the consumer remains opt-in.
Publishing frame events directly from a future producer could avoid part of
that analysis; these results do not measure such an interface.

The 4,096-function unit emits 1,460,104 code bytes and 490,376 CFI bytes. Its
peak per-function temporary arena use is 21,373 bytes; that excludes retained
typed inputs, descriptors and caller output buffers. Each descriptor is 68
bytes and each fixup location is four bytes. These are bounded synthetic typed
units, not a corpus compilation or a claim of DRAM throughput.

Separate 256-function processes measured 19.398–19.450 ns/entry without CFI,
33.289–33.523 with scalar CFI, and 28.204–28.364 with vector CFI. Vector runs
after 100 ms of scalar-emitter work before every sample measured
30.252–30.412 ns/entry. Their corresponding user-cycle ranges were
107.334–107.735, 184.645–185.912, 156.413–157.274 and 167.754–168.475.
Conditioning changed the observed cost; it does not by itself identify a power
or frequency mechanism. The raw measurement report has SHA256
`ab1a0efe11957a1d674f450bad827f1292cd6c4170697f53fff6ea7a0fa8f365`.

The same binary's assembly inlines effects resolution and eliminates unused
read-set work. It still constructs the complete write-resource set before the
CFI GPR check, then performs the bounded per-record checks. The eight-result
write array is an intentional vector-to-scalar handoff. Template expansion uses
aligned template/index loads, `vpermi2b`, masked merging and masked stores,
without stack spills in that expansion loop. A separate locked user-cycle
profile of the 4,096-function vector process lost no samples and attributed
23.01% of self samples to frame analysis; the emitter was below its 0.5% display
threshold. That profile includes generation and all preflight paths, so those
shares are not fractions of just the timed vector chain. The disassembly report
has SHA256 `1f78a179ac56d426cb9d2db9b667bb3b4b9aaece39b11ff76e1f9061ba4af72c`.

All implementation, templates, fixtures and test oracles here are original.
The format documents supply facts; GNU as, GDB and perf run externally. No
external implementation or fixture corpus is incorporated.
