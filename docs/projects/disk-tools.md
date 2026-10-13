# Disk-image tooling

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
and its module closure (see the [repository map](../agents/project.md)). On Linux
it is a default target and a `test_all` dependency, so a source that stops
compiling fails the normal build under Clang and GCC with warnings as errors.
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

When taking up this project, put the supported invocation, image formats, output
contract, focused tests and limitations here. Record a demonstrated image build,
independent image inspection and any boot test separately; they are not equivalent
observations. Do not execute disk-writing experiments against real devices merely
to populate a feature record. Planned expansion belongs in an owning issue, not
an unchecked list of promised features in this page.
