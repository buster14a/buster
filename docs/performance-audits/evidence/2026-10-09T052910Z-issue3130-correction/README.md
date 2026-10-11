# Issue 3130 corrective evidence bundle

This bundle retains the large embedded compile-database parser iterations and
final native self-test/source-size captures, the reviewed #3168 source
correction tests and earlier failed/interrupted attempts, and the #3160
consumer's focused reader, replay, and source-size captures. The archive's
`provenance.txt` describes exact revisions and outcomes. Raw member bytes are
listed in the archive's `SHA256SUMS`; the outer `SHA256SUMS` pins this README
and `raw-attempts.zip`.

The final focused reader captures are kept with each observed result: attempt
1 had one test failure; attempts 2 and 3 each passed all 26 tests. The 6303
source-size capture records unacknowledged build growth of 39,632 bytes. The
later source-size write and final report record the generated acknowledgment
and passing check. Earlier full consumer output (89 tests, four known lifecycle
failures tracked in #2926) and fixture-only checks without a runtime were
console-only and had no raw transcripts available to include.

The original 9700X failed comparison remains in the earlier immutable
[first-failure audit](../2026-10-09T0446Z-issue3130-first-full-failure/README.md).
This cloud follow-up does not qualify performance or replace that evidence.
