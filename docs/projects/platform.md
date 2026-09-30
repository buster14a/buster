# Platform services

[Catalogue](../../PROJECTS.md) · Area: `platform` · [Tracking](../project-tracking.md)

## Purpose and consumers

Provide OS, process, memory and file services to Buster code. Existing entry points
are [os.h](../../src/buster/lib/os.h), [file.h](../../src/buster/lib/file.h) and
[entry_point.h](../../src/buster/lib/entry_point.h). Compiler and tool invocations
are consumers, not the definition of the platform layer.

## Capability entry points

| Stable feature ID | Observable contract | Authoritative detail |
|---|---|---|
| `platform.processes` | Spawn with explicit argument/environment validation, executable lookup, standard-stream capture and failure reporting. | [Transactional process spawning](../agents/platform.md#transactional-process-spawning). |
| `platform.files` | Checked transfer completion and read boundaries, including partial I/O, EOF and native errors. | [File transfer completion](../agents/platform.md#file-transfer-completion) and [file reads](../agents/platform.md#file-read-boundaries). |
| `platform.memory` | Virtual memory commitment with separately reported advisory prefault outcomes. | [Commitment and prefaulting](../agents/platform.md#virtual-memory-commitment-and-prefaulting). |
| `platform.private-workspaces` | Claim a newly created directory without treating an existing entry as owned. | [Exclusive directory ownership](../agents/platform.md#exclusive-directory-ownership). |

Keep platform-specific behavior and recovery/error semantics in the linked
contract. A successful prefault request is not a residency or page-lock guarantee;
a completed file transfer is not by itself an atomic-publication contract.
Window/rendering integration has its own [graphics/UI home](graphics-ui.md).

## Validation and work

Use registered OS/file/arena tests and the relevant consumer regressions described
in the [platform guide](../agents/platform.md). Preserve the distinction between a
fault-injection test and actual execution on a named operating system. The PR
records which platform/configuration was tested and which was unavailable.

Changes shared by several products should have one platform issue and linked
consumer migrations. Use the existing [API migration workflow](../agents/workflow.md)
when an interface change crosses module boundaries.
