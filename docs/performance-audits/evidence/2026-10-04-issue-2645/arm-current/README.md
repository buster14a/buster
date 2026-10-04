# Current combined-source ARM qualification

PASS: [hosted run 37232523526](https://github.com/buster14a/buster/actions/runs/37232523526).
Exact tested source: `86f26a1c7338c89ebe11f7fe60bc2904a7686526`, whose parents are main
`0388ec6f56a96da2232119db6fb2d0050823cfe4` and implementation head
`974ebf00522f1a7e480e4b4964d0f1760b930005`.
This evidence-only branch descends from the tested source, preserving that exact commit.
Workflow/harness source: `dbbad7644f68e909ad15d2b1554616ec3f334e7f` on the separately owned ARM research branch.
The C smoke harness is unchanged; its exact file hashes are in harness.sha256.

The existing C driver source-size check and git diff --check both PASS.
Actual opt-in Wild ide configuration/build/runtime, no-GC full DWARF, ELF inspection,
source breakpoints and stack unwind PASS. The same Buster-produced ARM objects
externally link/run/debug with both linkers. Archive/shared/executable fixtures PASS.
Expected missing-linker/GCC-selector negative controls exit nonzero.
No ARM performance, sanitizer or LTO ranking is asserted.

The original artifact is 11314556672, 973467 bytes, SHA256
`85104211155cd7c639894eca0aa3b2647a370799ee97ce5cf597d0bbd0b5cc45`, retained by Actions for 30 days.
This permanent complete-text-logs.zip is 35929 bytes, SHA256
`46ab81a2ce03cabc3e6ab2a2a6352d96dd0d41a9b9dcfe643eaaa0620f3bbd5b`.
It preserves every original UTF-8 txt/log/csv/tsv/sha256/C file byte for byte;
built binaries and the redundant source tar are excluded.
Git-visible text views only trim trailing end-of-line spaces/tabs; original text is retained in the ZIP.
The raw timing rows are correctness/control process observations, not an ARM performance experiment.

Tool versions/host/package/executable identities are in host.txt, toolchains.txt and upstream-archives.sha256.
Wild 0.10.0 is MIT OR Apache-2.0 ([authoritative metadata](https://github.com/wild-linker/wild/blob/e9a9b0cb6fd63a23cc7b93aafc931aadd5746be6/Cargo.toml)).
mold 2.42.1 is MIT ([LICENSE](https://github.com/rui314/mold/blob/9b376bc6a9899d4a16b41777de1f013989459fbc/LICENSE)).
Buster's first-party license selection remains unresolved per LICENSES/README.md and #621.
No upstream source/binaries are vendored. Frozen x86 benchmark inputs/results are unchanged;
see the implementation's source-pinned performance report for those separate measurements.
