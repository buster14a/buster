Linux process-group census audit evidence, 2026-10-13T155006Z

Accepted final measurements:
- aa-os/: same baseline binary in both arms, OS-module noise control.
- ab-os-instructions-v3/: six final-code OS-module instruction pairs.
- ab-suite-final/: six final-code uninstrumented full-suite pairs.

Preserved prior attempts (not substituted for final measurements):
- ab-os-instructions/: qualified-event parser defect #4123; incomplete.
- ab-os-instructions-v2/: complete counts for the earlier test fixture.
- ab-suite/: driver interrupted before summary; no timing verdict.
- ab-suite-durable/: four pairs before the final test-fixture correction.

capture/ contains the supplied Superluminal capture's complete function
and thread exports, metadata, and selected untruncated main callgraph rows
with ancestors. export-inventory.json pins the full local raw callgraphs.
The capture does not bind an exact source/build identity or passing stdout.

perf-final/ is the separate 199 Hz inherited-child CPU sampling diagnostic.
Its retained-image run failed 22 existing assertions (#3674); it is not a
passing correctness run or native timing observation. recording-identity.json
pins the binary recording kept outside Git.

build/ pins configurations and frozen binary hashes. validation/ keeps logs
for the named revisions. experiment-manifest.json describes the final source,
host, environment and limitations. reproduce-commands.txt and the two final
Python collection/summary scripts document how native observations were made.
The older scripts remain for provenance, not as the current entry point.

SHA256SUMS covers every evidence file except itself. Raw outputs are copied
byte-for-byte, including empty stderr files, ANSI sequences and whitespace.
No executable, perf binary recording, or complete capture is committed here.

See ../../2026-10-13T155006Z.md for the measured claims, source behavior,
validation boundaries, separate findings and license report.
