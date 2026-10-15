Evidence for the interned size/alignment predicate slice of #1593.
The summaries bind baseline and candidate binary hashes, exact compiler arguments,
CPU/kernel/tool identity, alternating order and raw per-run observations.
The measured source revisions are in identities.json. Audit-only later commits do
not modify compiler source. Both Clang subjects were built serially in one path.

Measurements ran from the frozen source root recorded in identities.json. Its
manifest covers baseline src/ and build/generated/; generated headers are retained.
Recreate src/ from the baseline revision. Each workload uses exactly the arguments
in its summary, with the two frozen binaries named by their SHA-256 hashes.
CPU affinity was taskset -c 2. Work-count comparisons used perf-stat instructions,
six pairs, one discarded warmup pair and a predeclared 0.1% minimum effect.
Native time A/A and A/B used ten pairs, one warmup and the default 1% effect.
The separate work-count runs overlapped correctness work; only retired user
instructions are claimed from them. Native timing began after this session's
correctness work ended. An unrelated session was building with Clang, and normal
desktop applications remained active. No native speedup is established.

The two retained Python wrappers preserve the exact artifact/differential checks
used in the original session layout (this file's parent at work/evidence; wrappers
at work/, checkout at work/buster, binaries at work/bin). They are diagnostic
receipts, not new maintained tools. The differential wrapper independently rejects
missing/empty successful objects, covering the known oracle gap recorded on #1516.

baseline-selfhost.log and candidate-selfhost-final.log validate each subject's own
source fixed point. artifact-identity.json compares both subjects on the identical
frozen baseline source, with executable -g and object -g/-g0 cells. The superseded
candidate's failed internal fallback test and repair are recorded on issue #1593;
only final-candidate logs are included here.

Buster has no selected first-party license or asserted first-party SPDX ID; see
LICENSES/README.md at the baseline revision. No outside project/code was imported.
