# Source-size acknowledgements

Each `*.txt` file here acknowledges one change's growth of a ratcheted
source-size category past the 32 KiB per-change limit. The policy, the file
format and how the check uses them are in
[the source-size policy](../../source-size.md#acknowledging-growth).

Add a new file with your change. Only files the measured change adds count,
and the records are append-only: the check fails a change that removes,
edits, renames or copies one already on `main`, including a revert.
