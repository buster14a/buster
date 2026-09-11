# Recovery status — September 11, 2026

Recovered from PR #160's actual target branch, at
`8c3b9944ff0ecee0c25d89b312ca005729f51abb`; it had not reached main.
The six original files are unchanged. See the
[recovery audit](../../docs/performance-audits/2026-09-11T131903Z.md)
for provenance, exact checks and limitations.

The historical numbers in the September 6 evidence are not fresh measurements.
The native compiler-throughput launcher remains `./build.sh bench_throughput`.
This opt-in harness isolates three kernels; it does not replace compiler,
self-host, sanitizer or throughput gates.

During recovery, extraction from current source and the check-only kernel
binary passed with a Clang-raw-lexer corpus and all 17 synthetic populations.
The complete `run.py` path requires Debug non-unity Buster lexer objects and
was not rebuilt or executed in this recovery. For historical reproduction,
use `--repo` with an isolated checkout/build of the original source revision.
Never apply the preserved experimental patches merely to replay measurements.
