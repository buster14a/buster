# Linux kernel qualification frontier

[Compatibility index](../compatibility.md) · [Owning issue #3186](https://github.com/buster14a/buster/issues/3186)

This opt-in native C tool establishes a trusted minimal reference and exposes
Buster blockers. A successful diagnostic does **not** qualify a Buster-built
kernel. Broader configurations, upstream subsystem tests, module/driver cells,
and approved Zen 5 performance evidence remain separate acceptance work.

## Inputs and boundaries

Linux v6.12 is pinned to
[`adc218676eef25575469234709c2d87185ca223a`](https://github.com/torvalds/linux/tree/adc218676eef25575469234709c2d87185ca223a).
The immutable
[COPYING](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/COPYING)
declares GPL-2.0 WITH Linux-syscall-note, version 2 only.
Per-file SPDX declarations and the full upstream LICENSES directory are
retained; that root declaration does not replace component-specific terms.
The upstream source stays pristine and outside Buster's tracked source.

`tools/linux-kernel/minimal.config` is an allnoconfig input fragment, not the
resolved configuration. The tool retains and checks the resolved x86-64
configuration, serial console, initramfs, tmpfs, ELF userspace, timers, futex,
IPv4, optimization, and disabled modules. `CONFIG_RUST` is required to be
unselected if visible, while an omitted symbol remains unknown because its
Kconfig availability depends on the toolchain. The selected GCC flags keep
the kernel's required optimization; no `-O0` substitution is made.

GCC 14 builds the reference kernel and HOSTCC utilities. The pin's x86
compressed boot string compilation omits an explicit C dialect, so GCC 15's
C23 default fails on its older bool/false definitions. The reference uses
GCC 14 without patching source or changing Kbuild flags. ACPI and PCI are
explicitly enabled for QEMU's clean guest poweroff. Kernel relocation is
enabled, while address randomization stays disabled. Early serial output and
QEMU guest-error/CPU-reset diagnostics expose failures before console startup. A separate static
GCC/glibc init supplies seven named userspace controls: tmpfs file persistence,
anonymous memory, fork/wait, pipe communication, monotonic timer, signal
delivery, and guest loopback UDP. These are smoke controls, not upstream
selftests or pthread/synchronization qualification. They run with no guest
network interface, shared host directories, physical disks, or passthrough.

Distribution QEMU's named `max` CPU model with software TCG and its independent firmware boots this
reference. No Buster-built QEMU, musl, BusyBox, or guest compiler is used.
QEMU/firmware, GCC/binutils, glibc, cpio, host generators and their libraries
are external prerequisites. The earlier opt-in hosted runs recorded installed
package versions, available package copyright files, binary hashes and firmware
hashes, with missing license records explicit in `licenses/unavailable.txt`;
a future main-only recipe must retain the same records.
Their full component license inventory and supply-chain qualification remain
incomplete; this tool does not authorize redistribution of built binaries.
Buster's first-party license remains undecided as documented in
[LICENSES/README.md](../../../LICENSES/README.md); issue #621 remains
unselected. No license selection is made for external assembler/linker tools.

## Execution and scheduling

The compiler is generated and built by the existing native `build.c` driver.
The standalone frontier tool deliberately avoids changing that shared driver
while independent compatibility workers are active. Its build and
orchestration are C; existing upstream scripts remain upstream prerequisites.

The ordinary and queued lint jobs compile this first-party C tool with Clang
and GCC and run only its fixture-based `self-test`. That control reads no
upstream tree and starts no external project workload. The upstream frontier
remains opt-in. Whole external workloads belong to the single main-only owner
in [external-project CI](../../external-project-ci.md) (#3501); the earlier
manual/branch-push workflow was removed. A pinned recipe joins that shared
Linux job only when its owner qualifies it. Development and correctness runs
use disposable cloud infrastructure, never the maintainer's laptop or general
9700X agent work.

Build the tool with Clang and GCC (`-O2 -std=c11 -Wall -Wextra -Wpedantic
-Werror -fwrapv -fno-strict-aliasing -funsigned-char -Isrc`). The existing
ordinary/queued lint jobs run both builds and the fixture-only self-test. Build
the init from
`control_init.c` statically into an initramfs that also holds `/dev/console`
and `/tmp`. The reference compiler defaults to `gcc-14`; set
`BUSTER_KERNEL_REFERENCE_CC` to override it. Then run:

```sh
kernel-frontier self-test
kernel-frontier run /absolute/pristine/linux /absolute/build/Release/ide /absolute/new-output /absolute/control.cpio /absolute/tools/linux-kernel/minimal.config
```

Output must not exist. The tool never deletes/reuses a previous result tree.

`kernel_source_verify` checks the source against the pin before any compiler
runs, and again afterwards.
- It clears inherited `GIT_*` overrides and disables replacement objects and
  system/global configuration.
- `HEAD` must be the pin and the source must be the repository top level.
- Every entry of the pin's tree must exist with its exact type and executable
  bit. Its raw bytes, or a symlink's target, must hash (`--no-filters`) to the
  recorded blob.
- No other file, symlink or special entry may exist outside the top-level
  `.git`.

The index is never consulted, so assume-unchanged/skip-worktree flags, sparse
checkouts, ignored files and filters cannot hide drift. The local object
database is trusted as content-addressed; it is not re-hashed.

Each stage has an independent bound:
reference configure 120 s, reference build 600 s, reference boot 90 s, prior `qemu64` exception diagnostic 3 s,
candidate configure 60 s, each direct driver reduction 15 s, and each selected target-C replay 15 s. A timeout
kills the stage's process group and remains exit 124. Job limits are not
evidence of successful kernel work.

Native controls check argument preservation (spaces and empty arguments),
nonzero exit attribution, timeout cleanup, malformed/truncated action results,
ledger row/status parsing, and collision-free record names after PID reuse.
The deterministic classifier matrix covers C `-c`, C `-S`, preprocessed
`.S`, raw `.s`, explicit `-x` overrides and reset with `-x none`,
including suffix overrides, GCC stage-stop precedence, and preprocessing/syntax
probes. Stdin C actions remain inventoried but are not replayed because input
bytes are not retained; their target-C rows stay pending and the replay summary
reports `stdin_input_not_captured`. Config controls require
whole-line symbol matches, treat missing symbols as unknown, and reject
contradictory duplicates. A fixture census proves that host `.S` records do
not count as target assembly and that missing action results cannot pass.
Both the Clang and GCC builds must be warning-clean.

`kernel_source_controls` runs the source-pin gate against a disposable Git
fixture.
- It must refuse each of these:
  - an edit hidden by assume-unchanged
  - an edit hidden by skip-worktree
  - an ignored extra file
  - an executable-bit change
  - a missing tracked file
  - altered bytes that a replacement ref maps to the pin
- It must still pass when `GIT_DIR`/`GIT_INDEX_FILE` point elsewhere.

## Reading evidence

`*.argv` files use NUL-delimited fields: working directory followed by the
actual command argv. `commands/target-*.argv` and
`commands/host-*.argv` separate trusted CC from HOSTCC; `buster-*.argv`
records are candidate-only. Each action result is parsed fail-closed, bound to the output named by its
recorded argv, and records the exit status, role, resolved executable path
(path identity only), output path, and a local device/inode/size tuple when
that output exists. The tuple is syntax-checked metadata, not a content hash
or a cross-run identity.

`actions.tsv` inventories recorded target, host, and candidate CC-wrapper
invocations. Its classifier applies each `-x` setting only to following inputs,
including joined `-xc`, and recognizes C `.c`/`.i`, preprocessed `.S`, and
raw `.s`. GCC stop-stage precedence is preprocessing (`-E`), assembly
generation (`-S`), then object compilation (`-c`), independent of argument
order. Joined and split `-o` outputs are captured; response files are recorded
as ambiguous because their contents are not expanded. Unknown inputs remain
visible as other-input rows, and host assembly stays outside target counters.
`action-census.txt` reconciles wrapper record totals, all classifier classes,
and success/failure/missing-result totals. Reference C replays use the separate
`target-c.tsv` ledger and are explicitly excluded from wrapper totals.
`mechanisms.tsv` reports exact-line values from the resolved trusted and
candidate configurations as enabled, disabled, or unknown. Missing symbols stay
unknown; contradictory duplicates make the census fail. `HAVE_*` entries are capabilities, not proof of callsite use. Alternative
callsite use, exception-table use, and static-call callsites remain unknown.
The permanent static-call trampoline obligation is retained when inline support
is unavailable. The ledger checks configured `CONFIG_MITIGATION_RETPOLINE`,
`CONFIG_MITIGATION_RETHUNK`, `CONFIG_MITIGATION_SLS`, and `CONFIG_OBJTOOL`
when those symbols are present; missing symbols remain unknown. Every row
keeps runtime execution unattempted.

`tool-coverage.tsv` leaves direct assembler, linker, archive, objcopy,
objdump, objtool, post-link and nested compiler-child identities unobserved
unless a dedicated record exists. The trusted GNU/LLVM standalone assembler
invocations are not parsed; candidate external-assembler and native-Buster
assembler cells remain unattempted. Raw Kbuild logs and generated `.cmd` files
remain useful evidence, but this harness does not parse them into a complete
tool census. It also does not compute object content hashes or prove that an
object was included in the final image; those statuses remain explicit.

The trusted kernel must produce all seven expected passes and a clean guest
poweroff, and QEMU must exit zero before any candidate frontier runs.
The Buster CC wrapper forwards all arguments as `ide cc`, including
preprocessor/identity probes. It never impersonates GCC, substitutes another
compiler for target C, fabricates configure answers, or ignores kernel flags.

The first candidate attempt is pristine Kbuild configuration. Separate
`buster-driver-version` and `buster-driver-preprocessing` reductions retain
CLI diagnostics Kbuild redirects away; the preprocessing reduction uses empty
`/dev/null` input and is not a compiler-identity oracle. A failure is
retained as a blocker in `buster-configure.log` and `stages.txt`.
Independent target-C replays then use reference-generated headers explicitly
marked unqualified for a candidate kernel. They are diagnostic reductions,
not a candidate build or boot. Required C-to-object and C-to-assembly generator
actions remain inventoried in `target-c.tsv`; the wider `actions.tsv` ledger
also distinguishes assembly input and host actions. Compiler feature probes
remain separately available in raw command records. At most 24 required target-C
actions are selected in lexical output-path order. Their second rows record
attempted/pass/fail status; all remaining actions stay unattempted.

Only output and dependency-file destinations change during replay; all
semantic options and pristine source arguments remain. Original and replay
argv are both retained. Reference objects are not overwritten. Unsupported
options, failed compiles, missing candidate-generated inputs, and unavailable
runtime/link tests cannot be promoted into a compatibility percentage.

`*.result` captures diagnostic wall/user/system time and Linux wait4 max RSS.
wait4 RSS is the maximum reported for the waited process and children it
waited for, not concurrent process-tree peak memory. No timing from this
cloud/emulation path satisfies the performance acceptance in #3186/#2761.
Clean/source-edit/header-edit costs, image/code/debug/stack size comparisons,
and applicable exact-candidate 9700X measurements remain unqualified.

## Completion after the frontier

Reduce each observed failure with the existing compiler owner before editing
shared driver/frontend/IR/codegen contracts. Require fresh exact-head
self-hosting and relevant compiler regressions for any such fixes.
Then require a matched candidate-generated configuration/header closure,
all target C actions compiled by Buster, complete object provenance, a
Buster-built image booting with the independent controls, applicable selected
upstream tests with pass/fail/skip counts, and explicit broader-config deltas.
Integrate separately qualified #3179 userspace only after independent kernel
and userspace controls pass. Keep #3186 open until its staged acceptance is
actually satisfied.

The original frontier growth is acknowledged in
[`docs/source-size/acks/3186-linux-kernel-frontier.txt`](../../source-size/acks/3186-linux-kernel-frontier.txt).
This assembly-census change adds 58,603 build-category bytes including its
bounded first-party lint control; that incremental growth is acknowledged in
[`docs/source-size/acks/4079-linux-assembly-census.txt`](../../source-size/acks/4079-linux-assembly-census.txt).
The harness changes no compiler behavior or native-retirement identity
bindings.

QEMU exit zero alone is insufficient: a prior nonrelocatable reference exited
in 0.09 s with an empty serial log and was rejected before candidate work.
The declared fixture now enables relocation and early boot diagnostics;
relocation's role in that earlier exit is not independently established.

The accepted reference CPU cell is `max`. The prior `qemu64` cell is
retained only as a bounded exception diagnostic: its exit status is never a
reference pass. The same kernel and initramfs are used in both cells.
A CPU-model difference, if observed, does not qualify `qemu64` support.

The initial hosted census records 929 C-to-object and three C-to-assembly
required actions. Its `max` reference passes all seven controls and clean
poweroff, while the same image triple-faults on `qemu64`. Kbuild rejects the
Buster compiler as unknown, and all 24 unchanged-flag diagnostic replays
refuse `-Wp,-MMD,<path>`; the other 908 actions remain unattempted.
The passing frontier reports `qualified_kernel=no` and leaves #3186 open.

A later container rerun against main `897275fb9e3c` used GCC 14.2.0 and
QEMU 8.2.2 on Ubuntu 24.04. Source verification passed on all 86,680 tracked
entries, both before and after the run. The `max` reference again passes all
seven controls. The earlier target-C census records 932 required actions; it predates the assembly, tool-coverage and mechanism ledgers described above.

- **Configuration** still stops at `-P` (#1418).
- **Bounded replays:** the depfile and macro-prefix-map blockers are fixed.
  Of the 24 replays, 15 boot actions now stop at i386 code generation (#3654)
  and nine at `-mcmodel=small` (#4052).
- **Full replay:** a separate unchanged-flag replay of all 932 actions,
  outside this tool's 24-action bound, first stops at `-fshort-wchar` for
  903 of them (#4052).
- **Frontend findings** behind those options are #3237 and #4053.

An earlier container run recorded only 926 actions. Process IDs wrapped
during that build, so PID-only record names overwrote earlier records. Records
are now reserved with `O_EXCL` (`kernel_reserve`).
