# foreign-strings

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign_strings.c` builds a deterministic pool and
  original-offset map for one-, two- and four-byte `SHF_MERGE | SHF_STRINGS`
  entries. Its bounded vector scans and hashes have exact scalar twins.
  `foreign_strings_link.c` groups compatible foreign sections and remaps
  symbols and supported address expressions before layout, transactionally;
  unsupported expressions retain the original unmerged Link. The kernel,
  consumer and executable cross-checks run under `test-foreign`. See
  [foreign string merging](../../docs/foreign-string-merging.md) for the contract.
