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

The [repository map](../agents/project.md) identifies this as outside the default
CMake target. This catalogue does not advertise a verified build/run invocation or
claim that normal compiler CI exercises it. Reproduce its current build and actual
inputs before treating the source as a usable packaged tool.

When taking up this project, put the supported invocation, image formats, output
contract, focused tests and limitations here. Record a demonstrated image build,
independent image inspection and any boot test separately; they are not equivalent
observations. Do not execute disk-writing experiments against real devices merely
to populate a feature record. Planned expansion belongs in an owning issue, not
an unchecked list of promised features in this page.
