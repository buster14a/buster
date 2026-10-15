# Disk-image tooling

Status: **preserved, unmaintained source**. This area has no default build,
compiler-test, fuzz, analyzer or automatic CI coverage. Existing source, tests,
CMake fragments and earlier procedures below are retained for reference; their
buildability and behavior are not a current support promise. Reconnecting a
consumer requires an explicit maintenance decision.

[Catalogue](../../PROJECTS.md) · Area: `disk-tools` · [Tracking](../project-tracking.md)

## Purpose and existing source

[src/buster/apps/disk_builder.c](../../src/buster/apps/disk_builder.c) is the
standalone disk-image tool source. It has its own catalogue home rather than being
classified as a C frontend or compiler-backend feature. The protective MBR layout
and verification helpers live in
[src/buster/apps/disk_builder_verification.c](../../src/buster/apps/disk_builder_verification.c)
and its private header.

`disk-tools.images` identifies this source-level capability area. The verifier
helpers include `verify_disk` and `verify_disk_reserved_partitions_zeroed`. The
registered `disk_builder_tests` module checks MBR lengths against disk extent,
including the builder's 64 MiB geometry and 32-bit saturation. This remains a
helper-level MBR check: the tests do not validate GPT headers or arrays, CRCs,
filesystems, or bootability, and `entry_point` still does not call `verify_disk`.

## Integration and validation boundary

`cmake/DiskBuilder.cmake` defines the `disk_builder` target, independent of `ide`
and its module closure (see the [repository map](../agents/project.md)). The fragment is not included by the default graph. It was formerly a Linux
default target and a `test_all` dependency; that compile-maintenance obligation
has been removed.
Fuzz-enabled trees (the Linux Release and sanitized Release CI shards) skip it,
because `entry_point.c` then needs the libFuzzer driver that only Buster programs
link; the Linux portability shard still compiles it. Windows and Apple
configurations do not build it yet; their compile has never been checked. That only
establishes that the standalone tool compiles: the tool itself is not run by CI.
The registered test can be selected with `ide test --module=disk_builder_tests`;
it exercises the verifier helper without running the image-writing entry point.
When run from the repository root the standalone tool reads
`build/minimal_fat32.img` as the reference image, writes `build/mine.img` and
reports whether the two match; reproduce that with your own reference image
before treating the output as usable.

The reference must be a regular file of exactly 64 MiB. The builder opens the
file, checks its size before allocating or reading, reads exactly the bounded
extent, and checks that the opened file is still regular and exactly that size
after the read. Missing, empty, short, non-sector-sized, oversized, failed-read,
and changed-size references are rejected before image construction or output
publication. The builder's writer, comparison, and output slice all use the
same fixed 64 MiB extent.

The registered C tests cover missing, empty, 512-byte, 513-byte, and exact-size
references, a synthetic oversized pre-read stat, injected read and changed-size
failures, and preservation of an existing output sentinel on invalid input.
The oversized case uses the existing OS file fault seam so it proves rejection
before allocation without materializing another 64 MiB image. These are helper
regressions; they do not run the standalone executable in CI.

When taking up this project, put the supported invocation, image formats, output
contract, focused tests and limitations here. Record a demonstrated image build,
independent image inspection and any boot test separately; they are not equivalent
observations. Do not execute disk-writing experiments against real devices merely
to populate a feature record. Planned expansion belongs in an owning issue, not
an unchecked list of promised features in this page.
