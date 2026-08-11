# Apple-M1 A64 system semantics

`aarch64-system-semantics.generated.h` is generated only from the checked-in
Arm A-profile 2026-06 canonical JSONL.  The denominator is the 18 canonical
system rows with variable fields (`kind=canonical`, `system=true`,
`apple_m1=true`, and non-zero `field_mask`).  The 15 fixed canonical system
rows are owned by `arm-a64-m1-fixed.generated.h`; aliases and named system
register dictionaries are deliberately outside this API.  Thus the canonical
system census is 18 + 15 = 33, with no dropped, unresolved, or colliding rows.

The generated table is deterministic and pointer-free.  Regeneration/checking
is performed from the repository root with:

```sh
python3 tools/generate_aarch64_system_semantics.py
python3 tools/generate_aarch64_system_semantics.py --check
```

The encoder/decoder is form-directed and target-gated to the explicit
Apple-M1 profile (or a native target detected as Apple M1); a generic AArch64
target is not a member of this denominator.  Explicit feature removal remains
observable, so a PAuth-dependent HINT value is rejected when PAuth is disabled.

LLVM 22.1.8 (`llvm-mc -triple=aarch64 -show-encoding`) is an independent
oracle only.  The following representative words were accepted by LLVM and
are used as audit fixtures (bytes are shown in architectural little-endian
word order):

| Arm canonical row | Representative source form | Word |
| --- | --- | --- |
| `BRK_EX_exception` | `brk #0` | `0xd4200000` |
| `CLREX_BN_barriers` | `clrex #15` | `0xd5033f5f` |
| `DCPS1_DC_exception` | `dcps1 #0` | `0xd4a00001` |
| `DCPS2_DC_exception` | `dcps2 #0` | `0xd4a00002` |
| `DCPS3_DC_exception` | `dcps3 #0` | `0xd4a00003` |
| `DMB_BO_barriers` | `dmb #15` (`dmb sy`) | `0xd5033fbf` |
| `DSB_BO_barriers` | `dsb #15` (`dsb sy`) | `0xd5033f9f` |
| `HINT_HM_hints` | `hint #0` (`nop`) | `0xd503201f` |
| `HLT_EX_exception` | `hlt #0` | `0xd4400000` |
| `HVC_EX_exception` | `hvc #0` | `0xd4000002` |
| `ISB_BI_barriers` | `isb #15` (`isb`) | `0xd5033fdf` |
| `MRS_RS_systemmove` | `mrs x0, S2_0_C0_C0_0` | `0xd5300000` |
| `MSR_SI_pstate` | `msr SPSel, #0` | `0xd50040bf` |
| `MSR_SR_systemmove` | `msr S2_0_C0_C0_0, x0` | `0xd5100000` |
| `SMC_EX_exception` | `smc #0` | `0xd4000003` |
| `SVC_EX_exception` | `svc #0` | `0xd4000001` |
| `SYSL_RC_systeminstrs` | `sysl x0, #0, c0, c0, #0` | `0xd5280000` |
| `SYS_CR_systeminstrs` | `sys #0, c0, c0, #0` | `0xd508001f` |

There is no oracle gap for these representative words.  LLVM accepts generic
`hint #imm` spellings even for reserved or feature-gated values, so it is not
used to bless those values: the Arm canonical inventory and the pinned M1
feature set reject reserved HINT values and unsupported DGH/BTI/SPE/GCS/etc.
encodings.  Likewise LLVM's named system-register aliases are not imported;
raw `o0/op1/CRn/CRm/op2` fields remain the architectural operand contract and
the separate registry owns names.

HINT words deliberately overlap nine fixed canonical system spellings:
`NOP` (#0), `YIELD` (#1), `WFE` (#2), `WFI` (#3), `SEV` (#4), `SEVL` (#5),
`ESB` (#16), `TSB CSYNC` (#18), and `CSDB` (#20).  The fixed spelling catalog
owns those exact names/words.  The word-first
`buster_aarch64_system_semantic_decode` delegates to the Arm canonical decoder,
whose specificity ranking selects those fixed rows; it consequently does not
misclaim an overlap as HINT.  The explicit
`buster_aarch64_system_semantic_decode_form(..., HINT, ...)` path owns the raw
`HINT #imm` row and round-trips the same words.  Integration therefore uses
word-first decoding for canonical ownership and the form-directed path when it
has already selected HINT—no overlap row is dropped from the 18-row table.  The
test suite proves canonical digest selection, fixed-catalog lookup, and both
decode paths for all nine overlaps.

The remaining allocated M1 HINT immediates (XPACLRI and the PAuth forms) also
have fixed canonical spellings, but those spellings are non-system rows in the
broader fixed catalog.  Consequently every allocated HINT word is intentionally
owned by canonical word-first decoding; `decode_form` is the explicit raw-HINT
escape hatch for all of them.  The other 17 system rows have no such overlap,
with fixed canonical rows (DSB's SSBB/PSSBB aliases remain outside this
canonical word-first table), and their form-directed and word-first decodes are
compared in the tests.

Barrier options are retained as raw Arm fields, with the pinned architectural
allocation masks emitted in the generated metadata and enforced by both
encode and form-directed decode:

* DMB permits `CRm` `{1,2,3,5,6,7,9,10,11,13,14,15}`; `{0,4,8,12}` is reserved.
* DSB permits `CRm` `{0,1,2,3,4,5,6,7,9,10,11,13,14,15}`; `{8,12}` is reserved.
  `#0`/`ssbb` and `#4`/`pssbb` are allocated aliases whose exact words remain
  fixed spellings in canonical word-first decoding.
* ISB's assembly syntax permits `#imm`, but the architectural explanation
  allocates only `CRm=15` (`sy`); `#0` through `#14` are rejected.  The
  optional operand therefore defaults to `#15`.

CLREX remains fully defined for `CRm=0..15` with an optional `#15` default.
These barrier constraints are distinct from HINT's unallocated immediate
values, which are rejected even when LLVM happens to assemble a generic
spelling.
