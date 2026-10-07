# Disk-image tooling

[Catalogue](../../PROJECTS.md) · Area: `disk-tools` · [Tracking](../project-tracking.md)

## Purpose and existing source

[src/buster/apps/disk_builder.c](../../src/buster/apps/disk_builder.c) is the
standalone disk-image tool source. It has its own catalogue home rather than being
classified as a C frontend or compiler-backend feature.

`disk-tools.images` identifies this source-level capability area. The file contains
protective-MBR/GPT structures and disk-image verification helpers, including
`verify_disk` and `verify_disk_reserved_partitions_zeroed`. That observation is not
a complete format-conformance or bootability claim.

## Integration and validation boundary

`cmake/DiskBuilder.cmake` defines the `disk_builder` target, independent of `ide`
and its module closure (see the [repository map](../agents/project.md)). On Linux
it is a default target and a `test_all` dependency, so a source that stops
compiling fails the normal build under Clang and GCC with warnings as errors.
Fuzz-enabled trees (the Linux Release and sanitized Release CI shards) skip it,
because `entry_point.c` then needs the libFuzzer driver that only Buster programs
link; the Linux portability shard still compiles it. Windows and Apple
configurations do not build it yet; their compile has never been checked. That only
establishes that the source compiles: the tool is not run by CI and has no
focused test. When run from the repository root it reads
`build/minimal_fat32.img` as the reference image, writes `build/mine.img` and
reports whether the two match; reproduce that with your own reference image
before treating the output as usable.

When taking up this project, put the supported invocation, image formats, output
contract, focused tests and limitations here. Record a demonstrated image build,
independent image inspection and any boot test separately; they are not equivalent
observations. Do not execute disk-writing experiments against real devices merely
to populate a feature record. Planned expansion belongs in an owning issue, not
an unchecked list of promised features in this page.
