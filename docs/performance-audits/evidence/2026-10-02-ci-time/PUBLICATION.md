# GitHub preservation and reproduction notes

This is the durable publication of the 2 October 2026 CI time audit under #709.
`report.md`, the two historical helpers, all 27 raw ZIPs and all derived files
are preserved byte-for-byte from the original ChatGPT evidence bundle. The
original bundle SHA-256 was
`422a4cebdd04c3d27d80cb7a23c144b3ade681017caafe7023ba74c334a9d5b0`.

`evidence.zip` is a normalized container, not a claim of identical outer ZIP
bytes. It contains all 50 original members plus this note, `replay.py`, and
`expected-original.json`. The publication receipt binds its actual hash and
size. Download and extract it to obtain the original raw artifacts. The Git
archive branch preserves the ZIP and browsable derived ledgers independently
of GitHub Actions artifact expiration; it is not a production implementation
branch and is not intended to change main's CI policy.

## Complete offline reproduction

Run `python3 -B replay.py` from an extracted bundle, without `-O` or
`PYTHONOPTIMIZE`. It verifies the 27 raw archive hashes, runs the two original
read-only helpers, reconstructs phase totals, copies the three analyzer
extracts, and verifies all 50 original member hashes. Only ZIP/log/JSON data
is read; no Buster compiler, test, benchmark or captured executable is run.

Correction to the initial handoff: the two original helpers alone did not
regenerate `phase_totals.csv/json` or the three analyzer extracts. The added
`replay.py` supplies those missing steps. Original files and measurements were
not rewritten. This checks diagnostic reproduction, not independent semantic
qualification or a complete phase CPU census.

The original report's no-publication/no-CI-dispatch wording describes its
analysis stage. The subsequent user-authorized preservation uses one isolated
GitHub-hosted archival job, which downloads existing evidence, checks hashes,
replays data extraction and commits the bundle/ledgers only to its archive
branch. No laptop or 9700X is used, and no production or acceptance setting is
changed.

## Analyzer qualification refinement

The eight C4 shard durations sum to 914.461155 seconds. At two workers the
fixed-duration work lower bound is 457.2305775 seconds, versus the observed
468.247562 seconds: at most 11.0169845 seconds (2.35%) of that observation lies
above this lower bound. Shard-size extremes alone do not support a large
rebalancing benefit. Preserve existing #2033 ownership and its staged worker
budget/partition investigation; the original report's shard-balancing proposal
is a candidate, not an accepted priority or measured saving. This bound assumes
fixed costs and does not predict changed contention or worker counts.

License: Buster first-party licensing remains unselected/unresolved at the
inspected source, per LICENSES/README.md/#621. No external source or dependency
was imported. Python standard-library data processing only.
