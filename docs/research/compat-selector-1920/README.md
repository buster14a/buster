# Compatibility selector witness for #1920

Immutable diagnostic evidence for [#1920](https://github.com/buster14a/buster/issues/1920).
Live research state and implementation ownership remain on that issue.

- Production source: `7afde682de1beb595c779192c9519b7524afb412`, tree `b1a3545713f54e390902b4e9a90fdb5b10bc608e`.
- Production `build.c` blob: `5a86d337a46666dec017ece3e8e02bed37c4831d`; SHA-256 `29113efb2ae7c614c8954a24b4ae96d849be6c2100c2702e4b0defbcb34d9ffc`.
- Executed diagnostic checkout: `53aabba9e9ebd11bd1027bd9d5d556c4a3373291`, tree `7efcb0c6232ab2c6607cc14d4328e956df5aedee`.
- [Hosted run 37036720318](https://github.com/buster14a/buster/actions/runs/37036720318), attempt 1, [job 110936627726](https://github.com/buster14a/buster/actions/runs/37036720318/job/110936627726), succeeded on Ubuntu 26.04 x86-64.
- Toolchain: Ubuntu Clang 21.1.8 (6ubuntu1). Exact command/version are in the summary.
- Diagnostic driver SHA-256: `f3d99b4453d51f1bdf24e4ebac91832941634fb785d655e56cdb61cc2320debe`.
- Original artifact 11241360283, ZIP SHA-256 `1ff30b1380cc3338e8e0f9047b2fd184d60c7072edcec7fb2d0ae47b00c75f42`.

The research script inserts guarded early observations into twelve compatibility callbacks in a temporary copy of the complete build driver. Original CLI parsing, option copies, all seven selectors, path joining and path existence checks remain unchanged. All selected paths contain inert, nonexecutable sentinels. The early observation bypasses external source verification and exits before any project or Buster compiler execution. This is selector evidence, not a compatibility campaign or production-fix acceptance result.

409 observations: 288 file-population/configuration cells, 96 alternate-root cells, 24 malformed-option controls, and one instrumentation-disabled preflight control. Raw stdout establishes 48 explicit Debug/Release cross-configuration selections; 24 explicit requests select flat `build/ide` with unverified configuration; all 96 alternate-root cells ignore the supplied root (48 return no candidate and 48 select the default root). Positive controls select the requested config when present. Omitted config remains Release-first. No missing candidate is labeled a cross-configuration substitution.

`raw.jsonl.gz` and `summary.json.gz` retain the original unmodified artifact bytes under deterministic gzip. Verify decompressed hashes:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| raw.jsonl | 162960 | `8998c450726389f8024ad685537c733b334bb11884eddbaeaf0aa21d3426962b` |
| summary.json | 66731 | `e3e3a1b2b85b2aaff12b8e53b750d1cc7ad01a1d477606ddfb93f404142b42c4` |

To reproduce on an isolated standard hosted Linux checkout of the diagnostic source, run `python3 tools/research/compat_selector_1920.py`. It compiles only the diagnostic build driver and enforces the source blob, complete observation count, exact baseline selections and negative controls. No output of this experiment qualifies performance, source-artifact identity, executable validity, Windows/macOS behavior, or past compatibility results. Inline `--option=value` forms were not exercised.

License: Buster first-party terms remain unselected under [the inspected LICENSES/README.md](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/LICENSES/README.md) and #621. No upstream project inputs, implementation, or dependency were imported.
