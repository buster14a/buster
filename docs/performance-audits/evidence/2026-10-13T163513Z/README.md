# Retained function-lane evidence

`summary.json` contains readable paired results and source/binary identities.
`raw-evidence.zip` retains every raw observation and selected final validation
log without changing their bytes, plus frozen generated workloads, acquisition
scripts, CFG/function-size census, sampling profile and bootstrap traces.
Its internal `SHA256SUMS` pins each member, including empty output streams;
the external manifest pins the archive and summary. Both manifests were checked
against the retained bytes. The archive groups many empty/raw files for review
and transfer; it does not replace or alter their contents.

The trusted compiler binaries remain in the task's local `work/bin` and are
identified by their hashes and build commands; they are not published binaries.
The frozen full compiler input is identified by `frozen-manifest.json` and the
4cc005528 source revision. Generated stress inputs are included in the archive.
Linux native-runtime evidence does not establish Zen 5 performance, native
Windows/macOS runtime behavior, or issue #54's target-host acceptance.

Buster's first-party license remains unselected; see
[LICENSES/README.md](../../../../LICENSES/README.md). No external source was
imported by this implementation.
