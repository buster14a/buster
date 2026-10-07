# Native lowering investigation

This optional `ide` command answers one bounded question: which retained
canonical source anchor accompanies an emitted MIR sequence at an object-file
offset? It snapshots one named C function, then reads that snapshot together
with the exact completed object. It does not read source files to reconstruct
discarded provenance.

The first slice supports x86-64 Linux ELF `-c` with FAST allocation.
It rejects other targets, linking, library/framework inputs, assembly input/output, multiple inputs
and QUALITY. There are no new dependencies or
frontend objects in canonical IR. Capture-disabled compilation creates no
capture sink, hashes no extra input and resolves no extra source anchors.

## Capture and inspect

Use a clean, unchanged checkout and the optional compiler identity documented
in [the build guide](agents/build.md#investigation-capture-compiler-identity).
For example, after building `build/investigation/Release/ide`:

```sh
build/investigation/Release/ide cc --target=x86_64-linux -march=baseline -nostdinc -g0 -fregister-allocator=fast -fno-machine-fallback -finvestigation=build/cast.bcap -finvestigation-function=cast_unsigned -c src/buster/tests/compiler/driver/fixtures/investigation.c -o build/cast.o
build/investigation/Release/ide investigate build/cast.bcap build/cast.o --expect-revision="$revision"
build/investigation/Release/ide investigate build/cast.bcap build/cast.o --offset=123 --expect-revision="$revision"
```

Replace `123` with a decimal **file offset** reported by the first command.
The optional offset selects its containing emitted MIR interval. Every query
checks the entire artifact SHA-256 and the captured function bytes at the
recorded final location. `--expect-revision` rejects a different or unavailable
revision. Ordinary builds report an unavailable compiler revision explicitly;
numeric IDs in such captures remain opaque. The reader never joins captured
IDs with an unrelated compiler's tables.

The object is published before the sidecar. A missing function, canonical
fallback, unsupported mapping or sidecar write failure fails the capture
invocation and can leave the completed object on disk. An older sidecar is not
a successful new capture; the artifact identity check prevents accidental
reuse with a different object. Do not choose the input or object as the capture
path. Exact and existing resolved aliases are rejected.

## Worked question: unsigned floating conversion

The original first-party fixture has two functions in
`.buster_investigation`; `cast_unsigned` starts after the first function and
casts `double value` to `unsigned long long` on line 11. The question is why a
single cast can produce a comparison, subtraction and branch rather than one
signed hardware conversion.

The retained CAST anchor accompanies the selected
`MACHINE_X64_CVT_F64_TO_U64` interval. The existing encoder implements unsigned
conversion with a `2^63` threshold, a signed conversion on the appropriate
path and high-bit correction. For defined inputs `0 <= value < 2^64`, that
sequence implements the unsigned conversion's range. Its extra instructions
alone are not evidence of redundant lowering. The investigation command shows
the captured canonical anchor, numeric MIR identity, emitted interval, native
bytes and partial assembly through the existing native assembly printer.
Unsupported decoding remains bytes; branch/relocation information is relative
to the displayed sequence, not a relocated linked address.

Previously, this investigation required compiling the same input with
bootstrap token/IR/MIR dumps, identifying the function and CAST in those dumps,
following selector and encoder code, inspecting the ELF symbol and section
layout, disassembling the function, and manually correlating the offsets.
The command replaces the dump-ID and file-offset correlation with a checked
capture and offset query. Reading lowering semantics is still needed to judge
whether the sequence is justified. No elapsed developer-time or productivity
improvement has been measured.

The feature's hosted workflow records the actual command transcript and raw
paired capture-on/off timings. Its PR logs/artifacts are the execution evidence;
this semantic explanation is also supported by the inspected encoder. Pending
or failed runs do not establish a successful worked capture.

The executed demonstration is [run 36990736192, job 110786090843](https://github.com/buster14a/buster/actions/runs/36990736192/job/110786090843),
implementation head `2c71f72f95caf19bcb272aa9d3ba99e381fd7d18`, compiled PR merge
`d9d92a898feedbe9568f6e18723c1ca0744b576a`. It passed 1,027 focused assertions.
`cast_unsigned` occupies 85 bytes at section offset 16 and file offset 176.
Its conversion is MIR row 1, function bytes `[17,79)`, file bytes `[193,255)`:

```text
MIR row=1 opcode=31 (MACHINE_X64_CVT_F64_TO_U64) function_bytes=[17,79) file_bytes=[193,255)
canonical instruction=0 opcode=0 (numeric ID) selection_start_row=1
  source anchor .../investigation.c:9:41
canonical instruction=1 opcode=27 (CAST) selection_start_row=1
  source anchor .../investigation.c:11:12
```

The parameter and CAST share the same row start; both remain visible. The
CAST's expanded range length is one token byte, not the entire expression.
The partial decoder displays the floating conversion portion as `.byte`; it
does not establish individual native instruction boundaries there. The retained
MIR opcode and exact byte interval answer the lowering question, while the
encoder supplies its semantics. The smoke capture is 1,541 bytes; the timing
tests' 1,636-byte captures differ because their paths/configuration differ.
See [the diagnostic audit](performance-audits/2026-10-02T091939Z.md) for exact
binary/input/artifact identities, raw timings and validation boundaries.

## What the mappings mean

| Captured relation | Contract and limits |
| --- | --- |
| Root input and translation | SHA-256 of exact root bytes, plus SHA-256 of length-framed preprocessed token kinds/spellings. Included headers' raw bytes, whitespace and live on-disk source contents are not authenticated. |
| Compiler and configuration | Schema version, optional verified source revision, expanded length-framed argv, effective target/feature words and allocator. Revision verification occurs at configure time, not through an authenticated build receipt. |
| Canonical instruction to source | Existing `instruction_canonical_sources` and source-map observations; resolved expanded and original positions are copied before TU destruction. Original positions are anchors, not a claimed raw source extent. Missing ranges stay missing. |
| Canonical instruction to MIR | Existing selection-start `MachineLineMark` records. Duplicate, many-to-one and terminal anchors stay explicit. Canonical passes and fused lowering can transform the relationship. |
| MIR to native bytes | Existing encoder row starts bound sequence intervals, including allocator edits. One row can emit several instructions or zero bytes. Prologue and synthetic code may lack source ownership. |
| Function to artifact | Object function symbol, actual named ELF section, section-relative symbol value, serialized section file offset and matching bytes. No `.text` or module-base shortcut is assumed. |

A preceding selection anchor is not proof that it owns all following bytes.
Synthetic block-parameter stores can precede the next block's first anchor;
compare/branch fusion can discard the comparison's source identity. The tool
prints the retained relationship rather than synthesizing an exact explanation.
QUALITY is excluded because scheduling remaps starts without preserving a
general per-row ownership map. Linked addresses, inlining history, operand-level
provenance and source text are outside this capture.
Functions whose assembly linkage spelling differs from the requested C name
can fail object binding explicitly. Late changes to the captured encoded bytes
also fail binding rather than retaining a stale interval map. The transformation
flag identifies post-preparation/selection mappings, not an individual change
history.

## Bounded schema and validation

Schema 1 is explicit little-endian data, headed by `BSTRINV1`, a version and
length-prefixed NUL-free byte strings. The manifest holds function, revision,
configuration, input/artifact paths, section and target; three 64-byte hex
SHA-256 identities follow. It records file/section offsets, diagnostic capture
time, canonical function/code base, counts, allocator/target numeric fields and
a transformation flag. Each MIR row contains start, end and numeric opcode;
each mark contains MIR row, canonical instruction/opcode, expanded range,
expanded/original positions and their paths. Function bytes and `END1` finish
the capture. Unknown future schemas and trailing bytes are rejected.

Limits are 8,192 rows, 8,192 marks, 4,096 bytes per text field, 1 MiB of selected
function bytes, 4 MiB per capture and 64 MiB per artifact. These are diagnostic
bounds, not compiler capacity limits. Oversized captures fail rather than
silently truncate provenance. Empty MIR intervals and terminal marks are valid;
intervals must be ordered and bounded and marks nondecreasing within the row
count. The native reader checks lengths before allocating arrays.

`investigation_tests` independently authors a schema fixture rather than relying
only on serializer round trips. Negative controls cover truncated data,
malformed lengths/counts/intervals/marks/schema/trailer, missing manifest paths,
artifact changes and revision mismatches. Real C tests independently inspect
ELF section headers and compare complete capture-on/off object bytes for FAST.
An existing inline-assembly operand-limit shape checks that MIR-only codegen
rejects the unsupported function, leaving the object and sidecar untouched. File-based driver/consumer tests are excluded on
mobile; portable format/argument coverage remains registered.

The focused fixture uses one warm pair and three alternating FAST timing pairs.
The timed driver execution includes input
hashing, recording, binding, serialization and publication; argument parsing and
configuration framing occur before the timer. `capture_ns` counts only selected
snapshot and artifact-binding work. Raw deltas and sidecar size are
diagnostics on the observed hosted runner, with no threshold or qualified
performance verdict. Dedicated hardware gates remain pending when unavailable;
the workflow does not use benchpress or 9700X.

## Provenance

The inspected baseline is `6fc08ec475694aeb3b1062c1d390483a69668508`. Existing
bootstrap dumps, canonical source columns/maps, machine marks, encoder offsets,
object symbols and native assembly printing are extended rather than replaced.
No external implementation, vendored code or custom-language material is added.

Buster's first-party license is unselected at that revision per
[LICENSES/README.md](../LICENSES/README.md), tracked by #621. Retained LLVM notice
blob `fa6ac540007032cbd0ec772a1c72e6cb5527a4fe` matches upstream
`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`; its relevant SPDX identifier is
`Apache-2.0 WITH LLVM-exception`, with legacy/component terms retained.
The workflow's [checkout license](https://github.com/actions/checkout/blob/11bd71901bbe5b1630ceea73d27597364c9af683/LICENSE)
and [upload license](https://github.com/actions/upload-artifact/blob/ea165f8d65b6e75b540449e92b4886f43607fa02/LICENSE)
are MIT at those exact pins; bundled dependency notices are not fully audited
here. The [official ELF v4.2 section contract](https://gabi.xinuos.com/v42/elf/03-sheader.html)
is a primary technical reference. Its reuse license/immutable source revision
was not established; it is reference-only and no specification text is copied.

Owning issue: [#2236](https://github.com/buster14a/buster/issues/2236). Exact
submitted revisions, actual cloud validation, diagnostic samples, unresolved
gates and the next owner/action belong on that issue and its PR.
