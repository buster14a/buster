# foreign-strings

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign_strings.c` builds a deterministic pool and
  original-offset map for one-, two- and four-byte `SHF_MERGE | SHF_STRINGS`
  entries; a one-byte entry also takes the end of its block head, the
  earliest entry of its group that ends with it (#3044). Only groups whose
  one-byte entries have several lengths are considered: a bitmap prefilter
  flags the contents that have a proper extension, together with their
  blocks, and only those are sorted by reversed content (an MSD radix on
  8-byte keys, ties refined from a work list, each level past its members'
  common tail) before a stack pass writes the heads.
  `benchmark-foreign-strings` has six shapes: `unique` and `duplicate` (one
  length, nothing sorted), `suffix` (every second literal the end of the one
  before), `lengths` (several lengths, nothing to share), `repeats` (the
  suffix pairs twice, duplicates in a group of several lengths) and `tie`
  (the unique literals and `qr`, one tie of every literal); `--only SHAPE`
  picks one. Its bounded vector scans and hashes have exact scalar twins.
  `foreign_strings_link.c` groups compatible foreign sections and remaps
  symbols and supported address expressions before layout, transactionally;
  unsupported expressions retain the original unmerged Link. The kernel,
  consumer and executable cross-checks run under `test-foreign`. See
  [foreign string merging](../../docs/foreign-string-merging.md) for the contract.
