# Disk-image tooling

Status: **removed**. The disk builder, verification helpers and private header,
`disk_builder_tests` suite, and `cmake/DiskBuilder.cmake` are no longer in the
source tree. There is no `disk_builder` target or registered disk test module.

[Catalogue](../../PROJECTS.md) · Area: `disk-tools` · [Tracking](../project-tracking.md)

## Historical scope and limitations

The former tool constructed a 64 MiB disk image from a regular reference file.
Its verifier checked protective MBR geometry and reserved partitions; it did
not establish GPT correctness, filesystem validity or bootability. The
standalone entry point did not call the verifier, and CI did not run the
image-writing executable.

The [last pre-removal guide](https://github.com/buster14a/buster/blob/02a6414e020f5d549122197c3a7c8407df5bed76/docs/projects/disk-tools.md),
[builder](https://github.com/buster14a/buster/blob/02a6414e020f5d549122197c3a7c8407df5bed76/src/buster/apps/disk_builder.c),
[verifier](https://github.com/buster14a/buster/blob/02a6414e020f5d549122197c3a7c8407df5bed76/src/buster/apps/disk_builder_verification.c)
and [tests](https://github.com/buster14a/buster/blob/02a6414e020f5d549122197c3a7c8407df5bed76/src/buster/tests/disk_builder_test.c)
retain the former behavior and validation details at an immutable revision.
Their invocations apply to that historical checkout only.

The `disk-tools` Area ID remains for historical issue routing. Restoring this
project requires an explicit owner decision, a build graph and focused tests,
and independent image validation before making a support claim. Do not run
disk-writing experiments against real devices merely to populate this record.
