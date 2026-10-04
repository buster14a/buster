# Buster machine-instruction encoding audit

**Repository:** buster14a/buster  
**Pinned commit:** `f34cc3a56296be1b1244647775e1a40a67dae3ef`  
**Audit date:** 2026-10-04  
**Finding:** [GitHub issue #2625](https://github.com/buster14a/buster/issues/2625)

## Result and evidence status

This is a bounded audit, not a completed legality census of Buster's x86-64 and AArch64 encoders. One defect family is confirmed by a deterministic source argument and an independently executed reference experiment: the typed AArch64 ADD/ADDS/SUB/SUBS extended-register encoder rejects legal 32-bit UXTX and SXTX operands, and its typed decoder repeats the error. Existing tests explicitly expect that incorrect rejection.

The external C reference checks ran on the cloud x86-64 Linux environment using preinstalled Clang/LLVM 17.0.0. No generated AArch64 instruction was executed. No laptop or Ryzen 9700X was used, no package was installed, and no dependency or production source was added to Buster.

**The existing Buster C harness was not compiled or run.** The GitHub connector provided pinned source, but a full checkout was not retrievable by the execution environment. Consequently, the supplied patch is a candidate, not an integration-ready, tested fix. The reference results below are not Buster runtime results. No PR was opened or merged.

## 1. Pinned implementation and source references

All Buster links below are immutable revision links.

| ID | Source | Purpose |
|---|---|---|
| S1 | [Encoder, lines 1198–1209](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/lib/compiler/assembly/aarch64_encoding.c#L1198-L1209) | Incorrect width/extension predicate and field insertion |
| S2 | [Typed decoder, lines 3505–3520](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/lib/compiler/assembly/aarch64_encoding.c#L3505-L3520) | Same incorrect predicate on externally supplied words |
| S3 | [Existing boundary tests, lines 2146–2180](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/tests/compiler/assembly/aarch64_encoding_test.c#L2146-L2180) | Wrong source-width construction and expected rejection |
| S4 | [Implemented scalar form descriptors](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/lib/compiler/assembly/generated/arm-a64-m1-scalar-integer.generated.h) | Eight existing extended-register forms within a 72-form typed slice |
| S5 | [Typed operand API, lines 378–509](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/lib/compiler/assembly/aarch64_encoding.h#L378-L509) | Operand width, index, role, and modifier representation |
| S6 | [x86 physical API, lines 590–725](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/lib/compiler/assembly/x86_64_metadata.h#L590-L725) | Representation inventory, not a proved x86 validator defect |
| S7 | [Generated manifest](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/lib/compiler/assembly/generated/manifest.json) | Distinct metadata populations and provenance limitations |
| S8 | [A64 existing tests, lines 1450–1640](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/tests/compiler/assembly/aarch64_encoding_test.c#L1450-L1640) | Implemented scalar unsigned-offset memory forms and 51-form production-plan inventory |
| S9 | [Other scalar recipe checks, lines 1170–1285](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/lib/compiler/assembly/aarch64_encoding.c#L1170-L1285) | Existing immediate and shift checks; no new measured defect claimed |
| S10 | [Existing branch/address tests, lines 850–984](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/src/buster/tests/compiler/assembly/aarch64_encoding_test.c#L850-L984) | Implemented relocation/address forms; not rerun here |

Relevant blob identities:

- `aarch64_encoding.c`: `3a132bfaeadff008699e8ac6c129028621c4f18b`
- `aarch64_encoding.h`: `00695a7e13a6eb1cb9f427dbec02e4fdd3aae724`
- scalar generated header: `f1d5c0854af393997b132601a25d5a8d38e575d9`
- `aarch64_encoding_test.c`: `a9d8cff1f5b3810ed810d13e3d85032817ad1e51`
- `x86_64_metadata.h`: `3c0f09e93c03f6ee8553ff67e6927df620f7a73a`

The relevant implementation has separate fixed-row, GPR, scalar-integer, raw metadata, canonical decoding, and production-plan interfaces. These overlap; a wrapper or catalog row is not automatically another supported instruction form.

## 2. Confirmed defect: source width depends on sf AND option

### Minimized requests

```asm
add w0, w1, w2, uxtx
add w0, w1, w2, sxtx
```

| Request | Required word | Little-endian bytes |
|---|---|---|
| `add w0, w1, w2, uxtx` | `0x0b226020` | `20 60 22 0b` |
| `add w0, w1, w2, sxtx` | `0x0b22e020` | `20 e0 22 0b` |

For the first request, all three typed register operands have width 32 and indices 0, 1, 2. The present modifier is EXTEND/UXTX with amount zero. It reaches the explicit 32-bit rejection at S1:1203. No opcode packing or target feature ambiguity is needed to explain the rejection.

The generated 32-bit rows are `ADD_32_addsub_ext`, `ADDS_32S_addsub_ext`, `SUB_32_addsub_ext`, and `SUBS_32S_addsub_ext`. They already exist and require no additional feature. This is not a request to implement an unsupported extension. The four corresponding 64-bit rows are controls. [S4]

The decoder's `source_x` is also determined solely by extension option; S2:3512 rejects the legal W words before producing operands. A change confined to the encoder would therefore leave decoding broken. The old boundary test makes the same mistake when constructing Rm, then explicitly expects W/UXTX and W/SXTX rejection. [S1–S3]

### Specification-derived constraint

The primary ISA content used is Arm's ADD/ADDS/SUB/SUBS **(extended register)** entry, specifically its field diagram, **Decode (all encodings)**, and **Assembler Symbols**:

- [ADD](https://arm.jonpalmisc.com/latest_aarch64/add_addsub_ext)
- [ADDS](https://arm.jonpalmisc.com/latest_aarch64/adds_addsub_ext)
- [SUBS](https://arm.jonpalmisc.com/latest_aarch64/subs_addsub_ext)
- [Older Arm-authored ADD rendering](https://www.df.lth.se/~getz/ARM/A64/add_addsub_ext.html)

These are community-hosted renderings of Arm-authored documentation, not official-host downloads independently authenticated against Buster's 2026-06 archive. The official archive could not be retrieved. Its digest in S7 is declared provenance, not a digest verified by this audit. The agreement with the older rendering also avoids treating this baseline form as a newly introduced feature.

The resulting width rule is:

| Operation width | option 011 / UXTX | option 111 / SXTX | Other six options |
|---|---|---|---|
| 32 bits (`sf=0`) | Wm | Wm | Wm |
| 64 bits (`sf=1`) | Xm | Xm | Wm |

The option field has three bits, but its spelling does not alone determine the encoded operand's width. All eight option values are allocated for W operations. The imm3 field also has three bits, but only amounts 0–4 are legal; values 5–7 fail architectural decoding.

Register fields Rd, Rn, and Rm each have five bits. Their interpretation at value 31 is not interchangeable: Rn denotes SP/WSP, Rm denotes ZR, and Rd denotes SP/WSP for ADD/SUB but ZR for ADDS/SUBS. These constraints come from the separate flag-setting and non-flag-setting entries, not from treating “register 31” as one generic operand class.

For this family the independently written word formula is:

```text
word = 0x0b200000
     | sf << 31 | subtract << 30 | set_flags << 29
     | Rm << 16 | option << 13 | amount << 10 | Rn << 5 | Rd
```

This is a fixed 32-bit A64 form. There are no x86-style prefix choices, memory displacement fields, or PC-relative fields in this family; adding those axes would invent coverage rather than broaden this family's test denominator.

### Smallest candidate correction

Use this predicate in both S1 and S2:

```c
bool source_x = width == 64 &&
    (option == A64_SCALAR_INT_EXTEND_UXTX ||
     option == A64_SCALAR_INT_EXTEND_SXTX);
```

Remove the separate 32-bit rejection. Retain the amount bound, operand width equality, register-role validation, and target checks. Correct the old test's Rm-width construction and rejection expectation.

This patch addresses the two demonstrated predicates. Whether every canonical/target/source-assembly route passes after the change remains a full-harness question, not something established by a reference assembler.

## 3. Executed independent experiment

### Toolchain and machine

`tool-versions.txt` retains the actual version output. Clang identifies itself as 17.0.0, with source revision `10999b6d034fe318f3d56c83bddb6572593a8bb0`; llvm-objdump reports 17.0.0. The host was cloud x86-64 Linux. This is deliberately not a native AArch64 execution or performance experiment.

The independently written `extension_oracle.c` includes no Buster source, headers, or tables. It generates text for Clang's assembler and checks extracted `.text` against the formula above. LLVM's assembler and disassembler share a project and are not counted as two unrelated reference implementations. The ISA-derived arithmetic is the separate check; neither LLVM route uses Buster's metadata.

### Positive family

The generator enumerates:

```text
4 operations × 2 widths × 8 options × 5 legal amounts × 3 register triples
= 960 legal input tuples
```

The triples are `(Rd,Rn,Rm) = (0,1,2), (30,31,31), (31,30,0)`, with the appropriate SP/ZR spellings.

Observed:

```text
external_assembler_words=960 mismatches=0
source_only_predicted_Buster_rejections=120 (NOT a Buster execution result)
```

All 960 words disassemble without `<unknown>`. The byte comparison is exact; acceptance or mnemonic spelling alone was not the pass criterion.

### Reserved fields and a reference-tool caveat

The negative driver separately generates 192 reserved-amount cases: eight forms × eight options × amounts 5, 6, 7, with one register triple. Clang rejects every source request. All 192 directly supplied raw words decode as `<unknown>`.

It also generates 64 requests with the wrong Rm width. LLVM rejects 60 and accepts four spellings:

```asm
adds x0, x1, w2, uxtx #0
adds x0, x1, w2, sxtx #0
subs x0, x1, w2, uxtx #0
subs x0, x1, w2, sxtx #0
```

Their decoded operands are Xm, not Wm. The words are `ab226020`, `ab22e020`, `eb226020`, and `eb22e020`. This is source syntax permissiveness or normalization in the reference assembler, not evidence that the typed width mismatch is architecturally meaningful or should be accepted by Buster.

The initial negative driver's assumption that every source request would be rejected was therefore false:

```text
external_assembler_negative_cases=256 rejected=252 infrastructure_errors=0
exit status = 1
```

That failure, all diagnostics, the four accepted cases, and their disassembly are retained. They are not silently removed from the denominator.

### What this establishes—and what it does not

The experiment independently validates expected words for the sampled legal combinations and confirms the reserved shift boundary in the reference tools. The source argument establishes Buster's false rejection without depending on a Buster round trip.

It does not establish that the Buster patch builds, that the complete existing test suite passes, that those requests are emitted by normal C code generation, or that an AArch64 CPU was used to execute them. A legal encoding and a chosen assembler spelling are separate claims from execution behavior.

## 4. C regressions and patch status

`buster_extension_regression.c` is a block for insertion into the **existing** `aarch64_encoding_test.c` test body immediately after pinned line 2175. It uses the existing `arguments`, `gpr_target`, types, functions, and BUSTER_TEST macro. It needs no new test framework, module registration, library, or dependency.

`candidate-fix-and-regression.patch` changes only the two existing C files: the encoder/decoder source and its test source. It fixes the old expectation and adds the regression block.

The authored regression exercises:

| Input category | Unique input tuples/requests |
|---|---:|
| Legal form/option/amount/register-role combinations | 960 |
| Reserved amounts with all three register triples | 576 |
| Opposite Rm width for each legal tuple | 960 |
| `UINT64_MAX` modifier amount | 8 |
| Register index 32 or 255 at each operand position | 48 |
| Incorrect SP/ZR role or SP tag on a non-31 index | 48 |
| **Total distinct authored test inputs** | **2,600** |

These are **unrun regression inputs**, not measured passes. The first 1,536 tuples are exercised through three encoding entry points and directly through typed form decoding; call count is not unique tuple count.

Positive decoder inputs are computed independently, never taken from Buster encoding output. Rejected encodes must preserve a word canary. Rejected decodes must preserve nonzero-filled operand/modifier buffers and both count canaries. The test allows the decoder to omit an architecturally default modifier rather than treating canonical syntax normalization as a defect.

`git apply --numstat` successfully parsed the patch:
- encoder source: 5 additions, 4 deletions;
- test source: 161 additions, 3 deletions.

This is a **patch syntax check only**. It is not `git apply --check` against a full checkout, and not a compiler or test result.

## 5. Representation findings, without inventing additional defects

### Confirmed: a cross-field invariant was duplicated incorrectly

The A64 API carries width, index, SP role, and modifier separately. The generated descriptor's dynamic Rm width is zero; its actual width is resolved in handwritten recipe logic. This makes the combination `operation width × extension option × source width` an invariant outside the descriptor. Its duplicated derivation in encoding, decoding, and tests allowed all three to agree on the same wrong rule. [S1–S5]

The narrow repair does not need another opcode table. A small explicitly specified width predicate is sufficient; independent byte fixtures must remain outside it. Sharing one production helper between encode and decode can reduce drift, but it cannot replace an independent test oracle.

### Risks inspected, not confirmed truncation bugs

`A64ScalarIntOperand.index` is an eight-bit field even though the instruction register field is five bits. Values 32–255 are representable and should be rejected; the authored tests cover both ends. A caller that narrows an out-of-range integer before constructing the API value can lose information before the checked boundary. This audit did not trace such a caller or confirm silent truncation in production. [S5]

The A64 immediate and modifier amount are 64-bit quantities. The reviewed scalar recipes test bounds before narrower insertion for the forms covered by those checks. Their mere use of casts is not evidence of truncation. [S5, S9]

The x86 physical API is more explicit than an untyped integer interface: register index and width are 16-bit; signed mathematical immediates and full-width unsigned bit patterns are distinct; source memory width is separate from address size. Those are useful protections. Nevertheless, independent flags still permit representable contradictions: multiple immediate value states, high-byte tags with incompatible register identity, or unrelated memory-address mode flags. Proving that a particular checked encoder accepts one of them requires its validator and selected form; this audit has not done that. [S6]

Raw bitfield packing also needs a separate contract. An API documented to insert raw fields is not automatically a semantic assembler. A reserved combination accepted by a raw packer is not, by itself, a defect in the checked instruction API.

## 6. Boundary program for other implemented families

The following is **test design and source inventory, not additional executed coverage**.

For an n-bit signed displacement scaled by q, test:

```text
minimum = -2^(n-1) × q
maximum = (2^(n-1)-1) × q
values = {minimum-q, minimum, minimum+q, -q, 0, q,
          maximum-q, maximum, maximum+q}
```

For q greater than one, add non-divisible neighbors. Preserve every non-displacement bit when patching. Never shorten a symbolic unresolved value merely because its placeholder equals zero.

Examples limited to forms present in S8/S10:

- **B:** imm26 scaled by four, so the exact byte limits are −134,217,728 and +134,217,724. Reference: Arm [B, Decode and Assembler Symbols](https://arm.jonpalmisc.com/latest_aarch64/b_uncond), rendered version 2026.06. This was not newly exercised against Buster.
- **ADR:** signed 21-bit immediate split into immhi/immlo, unscaled: −1,048,576 through +1,048,575. Reference: Arm [ADR](https://arm.jonpalmisc.com/latest_aarch64/adr), rendering reports 2025.09.
- **ADRP:** signed 21-bit quantity scaled by 4096, relative to the instruction's page. Test the page bases, not merely the numerical difference of unaligned target and PC values. Reference: Arm [ADRP](https://arm.jonpalmisc.com/latest_aarch64/adrp), rendered version 2026.06.
- **Unsigned-offset LDR W/X:** imm12 is multiplied by four/eight, giving maxima 16,380/32,760. A byte offset of four is encodable for the W form but not for the X form. The base remains Xn/SP even for a W destination. Reference: Arm [LDR (immediate), Unsigned offset and Assembler Symbols](https://arm.jonpalmisc.com/latest_aarch64/ldr_imm_gen), rendered version 2026.06. Keep these separate from signed unscaled/writeback forms; their ranges and semantic constraints differ.

For the other scalar recipes at S9, the highest-value next tests cross operation width with shift amount, legal modifier kinds, immediate boundary values, and role-tagged register 31. In particular, shifted ADD and logical operations must not share an assumed allowed-shift set just because their fields look similar; MOV-wide width interacts with halfword position. These are source-identified test targets, not new findings.

For x86, the present deliverable stops at representation inventory. A completed prefix/address audit still requires an exact implemented-form census and primary Intel/AMD entry checks for each chosen form, followed by independent byte tests. Candidate axes include byte-register identity versus required prefix, address size versus data width, base/index roles versus SIB representation, and compressed displacement versus a selected form's tuple. No x86 prefix or displacement validation is claimed to have passed here.

## 7. Coverage denominator

The extended-register family has:

```text
8 forms × 8 options × 5 legal amounts × 32^3 register bit patterns
= 10,485,760 legal instruction words.
```

The wrong W predicate excludes, by source reasoning:

```text
4 W forms × 2 options × 5 amounts × 32^3
= 1,310,720 words = 12.5% of this family.
```

Only 960 legal words were externally checked, using three of 32,768 register triples. They cover every selected form, extension option, and legal shift value, not every register tuple. Of those 960, 120 are necessarily rejected by the pinned Buster guard; this remains a source prediction rather than a measured Buster count.

Broader source populations must remain separate:

| Population | Count | What it is not |
|---|---:|---|
| A64 scalar typed forms | 72 | Not all A64 instructions |
| A64 GPR typed forms | 80 | Not disjoint from other catalogs |
| A64 production-plan forms | 51 | Not 51 independent encoders |
| Arm canonical decoder rows | 1,523 | Not proof of typed operand support or external validation |
| LLVM-derived A64 candidate rows | 7,491 | Not an implemented-form coverage denominator |
| x86 metadata rows | 11,013 | Not a count of supported and verified forms |

S7 identifies generation as audit mode and acceptance as blocked; its provisional catalogs are not a completion certificate. A Buster encode/decode round trip proves consistency only over the attempted input set. This finding is a concrete case where consistency between implementation and tests conceals a wrong architectural predicate.

## 8. Reproduction

Run the reference checks only on a cloud or GitHub-hosted machine with the existing Clang/LLVM tools. The negative reference driver resolves `clang` through PATH; set PATH to the intended existing toolchain and record its version. In the captured environment, Clang was installed under `/usr/local/swift/usr/bin`.

```sh
clang -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
  -fwrapv -fno-strict-aliasing -funsigned-char \
  extension_oracle.c -o extension_oracle
./extension_oracle generate > extension-family.s
clang --target=aarch64-unknown-linux-gnu -c extension-family.s -o extension-family.o
llvm-objcopy --dump-section .text=extension-family.bin extension-family.o
./extension_oracle verify extension-family.bin
llvm-objdump -d extension-family.o > extension-family.disassembly.txt

clang -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
  -fwrapv -fno-strict-aliasing -funsigned-char \
  extension_negative_oracle.c -o extension_negative_oracle
: > extension-negative-diagnostics.txt
./extension_negative_oracle
# The captured Clang 17 run returns 1: four width spellings were accepted.
# Retain and inspect this discrepancy; do not silently treat it as a pass.

clang --target=aarch64-unknown-linux-gnu -c extension-reserved.s -o extension-reserved.o
llvm-objdump -d extension-reserved.o > extension-reserved.disassembly.txt
grep -c '<unknown>' extension-reserved.disassembly.txt
```

For Buster, obtain the exact pinned checkout, check/apply the candidate patch, and use its existing build/test infrastructure and repository instructions. The documented `test_all` target and required normal CI remain the closure gate. **No Buster invocation above or in this report is presented as executed.**

## 9. Licensing, restrictions, and unresolved work

The pinned [THIRD_PARTY_NOTICES.md](https://github.com/buster14a/buster/blob/f34cc3a56296be1b1244647775e1a40a67dae3ef/THIRD_PARTY_NOTICES.md) says it does not select a first-party Buster license; that decision remains unresolved there. It records XED's Apache-2.0 terms and LLVM's Apache-2.0 WITH LLVM-exception terms separately, and flags Arm-derived redistribution review as unresolved.

For the actually used reference toolchain, the official [LLVM LICENSE.TXT at the reported revision](https://github.com/swiftlang/llvm-project/blob/10999b6d034fe318f3d56c83bddb6572593a8bb0/llvm/LICENSE.TXT) was read; its header identifies Apache-2.0 with LLVM exceptions. License blob: `fa6ac540007032cbd0ec772a1c72e6cb5527a4fe`. The binary's reported revision is recorded, not independently rebuilt or attested.

The C reference tools and added tests are independently written. This package contains no Arm XML, upstream encoder implementation, or new repository dependency. License notices do not independently verify the provenance of generated catalogs.

Outstanding: run Buster's existing harness; establish patch applicability and full-route correctness; inspect production codegen reachability; complete the x86 form/prefix audit and remaining A64 families; independently retrieve/authenticate the vendor-hosted ISA release when available. No claim of complete ISA, instruction-site, or execution-semantics coverage is made.
