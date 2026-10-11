# musl + BusyBox Linux userspace qualification

[Agent instructions](../../../AGENTS.md) · [Harness index](../compatibility.md)

The opt-in native command reuses [musl](musl.md) and
[libc-test](libc-test.md) before attempting an unchanged BusyBox application:

```sh
./build.sh build --config Release -t ide
./build.sh test_userspace /path/to/busybox /path/to/musl /path/to/libc-test /existing/parent/fresh-results
```

The implementation lives in [tools/userspace_qualification.c](../../../tools/userspace_qualification.c),
included by the native build driver. Initial execution is Linux x86-64 on
GitHub-hosted Ubuntu; it requires existing Clang, GNU make/binutils, Git,
installed Linux UAPI headers, cp, and noninteractive sudo/chroot. No external
source is vendored or patched. Results must be outside the input checkouts;
the command refuses an existing output directory and unsafe paths in upstream
make's CC expansion. Keep the repository and every external input unchanged
through the run. A harness being present is not an observed qualification pass.

## Inputs and licenses

| Input | Immutable identity | License evidence |
|---|---|---|
| BusyBox 1.36.1 | `1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4` (`1_36_1`) | [LICENSE](https://github.com/mirror/busybox/blob/1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4/LICENSE): GPL-2.0-only for the combined distribution; component grants and notices remain separate. |
| musl 1.2.6 | `9fa28ece75d8a2191de7c5bb53bed224c5947417` | [COPYRIGHT](https://github.com/ifduyue/musl/blob/9fa28ece75d8a2191de7c5bb53bed224c5947417/COPYRIGHT): MIT overall, with enumerated BSD and permissive third-party notices; preserve source-file notices. |
| libc-test | `68edb8bd73dab8147ee54c8bec638f4d2b3cff37` | Existing exact pin from the libc-test guide. Full fixture/component license inventory remains to be retained by the qualification campaign; no blanket license claim. |
| Linux UAPI | The executing image's `/usr/include/linux`, `asm-generic`, x86-64 `asm` | External header prerequisite, copied only into fresh results. Image/package and per-header identity/license census remains incomplete; do not claim a reproducible kernel-header pin. |

BusyBox's [ash](https://github.com/mirror/busybox/blob/1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4/shell/ash.c)
retains its Berkeley copyright notice beside GPLv2-or-later component wording.
The [tar parser](https://github.com/mirror/busybox/blob/1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4/archival/libarchive/get_header_tar.c)
has GPLv2-or-later component wording.
Upstream [testsuite/README](https://github.com/mirror/busybox/blob/1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4/testsuite/README)
declares GPL-2.0-or-later for the suite; individual assets still require inspection.
These inspected examples do not constitute the full enabled-component census.
No binary redistribution or relicensing is part of this command. Buster's own
license decision remains [unselected](../../../LICENSES/README.md).

## Declared configuration and cells

[busybox.config](../../../tests/userspace/busybox.config) is an explicit seed:
ash, file/directory and process/exit-status operations, text utilities,
uncompressed tar and SHA-256. Upstream `allnoconfig` resolves it; the candidate
starts from that exact resolved `.config` and runs `silentoldconfig`.
Different resolved configurations fail instead of silently reducing features.
The seed requests debug, unoptimized, static output. It is a bounded userspace,
not defconfig, networking, init, privileged device management or a complete OS.

The command first calls the existing full `test_musl` gate with libc-test,
retaining its architecture replacements, startup, static/shared probes and
per-test classifications. Failure stops the BusyBox stage; it never authorizes
a reduced/substituted archive merely because an archive file exists.

BusyBox uses its normal external make/generator scripts. Host generators and
CC preprocessing/version queries run with Clang and are labeled separately.
Required target-C `-c` actions run with the selected compiler. Unsupported
flags are not removed: upstream's optional compiler probes make that decision.
No configure success or replacement C object is fabricated.

| Cell | Application C | libc/startup | Link |
|---|---|---|---|
| Independent reference | Clang | Clang-built musl | External Clang/GNU ld |
| Application isolation | Buster FAST, machine fallback refused | Clang-built musl | External Clang/GNU ld |
| Joint static workflow | The same Buster application objects | Buster-built musl | External Clang/GNU ld |

The joint link replays the retained final upstream link argv, changing exactly
the three startup paths, libc archive and output path. It does not recompile
the application with a different configuration. musl's empty library aliases
(`m`, `pthread`, `dl`, `crypt`, `rt`) resolve to its explicit libc archive;
no host libc/startup search is allowed. Native Buster linking and dynamic
BusyBox remain separate, unqualified cells.

## Runtime and evidence

Each successful build is copied into its own initially empty chroot with only
`/bin/busybox`, a project-owned [workload](../../../tests/userspace/workload.sh),
and a fresh `/tmp`. ash standalone applet dispatch avoids host utility fallback.
No host filesystem is mounted into the chroot. The workload checks pipelines,
file copies/moves/symlinks, a tar roundtrip, selected text operations,
child exit status, background waiting and SHA-256 against
[fixed expected output](../../../tests/userspace/expected.txt).
A zero exit status without the exact transcript fails.

`reference-actions/` and `buster-actions/` retain one directory per CC action:
effective length-prefixed argv, upstream argv, stdout/stderr, OS status and
timeout/capture/cleanup facts, source/output names and object SHA-256.
A failed make uses `-k` to expose independent failures. Its logs and attempted
actions must be read together: action count is not proof that all required
actions were attempted. Complete required/attempted/pass/fail/skip inventories,
source/header closure and all applicable BusyBox upstream tests remain
completion work under [#3179](https://github.com/buster14a/buster/issues/3179).

The opt-in [workflow](../../../.github/workflows/userspace-qualification.yml)
runs only on its explicitly named development branch or manual dispatch. It
does not add a PR/merge-group/main workload or a required check. Each job reports
machine specifications and retains logs, outputs and compiler build metadata,
including failures. Commands and the receipt report cloud correctness only.
All clean/edit/header cost, process-tree memory, code-size and runtime performance
acceptance remains incomplete until the existing approved 9700X service supplies
the relevant exact-candidate/configuration evidence.
