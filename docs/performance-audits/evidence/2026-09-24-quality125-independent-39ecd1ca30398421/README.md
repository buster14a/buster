# QUALITY #125: preserved experiment and independent-validation packets

This is evidence archival, not a new allocator implementation or performance
acceptance. Both reports are preserved byte-for-byte, together with their original
ZIP packets. No original evidence has been rerun, updated or normalized here.

## Reading order and reconciliation

1. **region-selection-report.md** and **region-selection-experiment.zip** contain
   the selector-only experiment: scalar, compact-list, lazy complete heap and
   tiled-winner alternatives. The generated test/sanitizer results and local
   selector timings are not complete allocator/compiler or 9700X measurements.
2. **independent-review.md** and **independent-validation.zip** contain the later
   independent replay of the active owner's hosted diagnostic capture and the
   extracted ordering-helper tests. This later packet supplies population
   evidence that the first packet did not have. The first report's missing-census
   statement is a historical limitation of that experiment, not a claim that the
   later capture is absent.

The first report's negative activation results apply to its standalone algorithms;
they are not a performance verdict on the owner's selective sparse constructor.
The independent review's sparse gate is a typed-storage bound, not an empirically
accepted timing crossover. The capture omits U (candidate memory-edit count), so
51 definitely qualifying tables and 588 ambiguous tables must stay distinct.
Neither packet establishes complete production placement/artifact equivalence or
a whole-compiler performance result for the owner's implementation.

## Immutable references

- Parent: https://github.com/buster14a/buster/issues/125
- Ownership: https://github.com/buster14a/buster/issues/125#issuecomment-5813893082
- Inspected baseline: 5d1c314a8ee3c5f9fb713f6afb85ab1e795f44a0
- Earlier experiment base: 2e942e80a87666409cf29d3e24a68d322b9e71fd
- Owner's implementation inspected: 6644f7a9dd9c9b360091c5c9d7a6eda75183f293
- Hosted diagnostic capture: https://github.com/buster14a/buster/actions/runs/36000528004
- Capture attempt: 1; original artifact: 10807458255
- Original capture SHA-256: 085ce2e62f963059d6f6dafdedc999ba914aff5ee26c04e23026b6c62ab1bc04

These are report-time identities, not assertions about the latest main or PR
status when this record is published. The original reports' statements that they
were unpublished describe their original sessions; this wrapper records later
archival only. Leave implementation ownership, parent issue state, policies,
workflows and integration-owned files unchanged.

See evidence-manifest.json for SHA-256 and byte sizes of the four originals.
Unpack each ZIP separately to access its original manifest and reproduction
instructions. No build or benchmark is required merely to publish this record.
