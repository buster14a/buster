# QEMU TCG compatibility census

[Harness index](../compatibility.md) · [Campaign #3185](https://github.com/buster14a/buster/issues/3185)

This native-C opt-in action starts the campaign with normal upstream configure,
a selected emulator build, and separate unit/QTest attempts. A successful census
does not qualify guest boot, functional tests, all compilation provenance, or
performance. The campaign remains open until those gates have actual evidence.

## Frozen configuration

The upstream authority is [qemu-project/qemu](https://gitlab.com/qemu-project/qemu);
its [GitHub mirror pin](https://github.com/qemu/qemu/tree/b2995afed254e4804611df52210067a4d5dbdf0b)
is QEMU v9.2.4, commit `b2995afed254e4804611df52210067a4d5dbdf0b`.
This is a bounded compatibility pin, not a recommendation to deploy this version.

The reference intake host is GitHub-hosted Ubuntu 24.04 Linux x86-64,
with actual package/compiler identities retained. Ubuntu 26.04 was rejected
at intake because its newer string header caused unchanged QEMU 9.2.4 to fail
its Clang reference build; that attempt is not a qualified cell.
The cell is Linux x86-64 host, `x86_64-softmmu`, TCG enabled and KVM disabled.
The same explicit configure options are used for Clang and Buster:
`--without-default-features --disable-download --disable-docs
--disable-guest-agent --disable-tools --disable-fdt --disable-strip
--disable-install-blobs --disable-rust --extra-cflags="-O0 -g"`.
Normal target device configuration stays upstream-owned. Optional features are
declared before either compiler runs; a failure must not trigger feature removal.
QEMU's defaults include optimization; the final extra flags request a matched
debug/debug census. Check the retained actual compiler arguments before making
a debug configuration claim.

External prerequisites are Git, Clang, Meson >=1.5, Ninja, make, pkg-config,
Python/venv, coreutils, the independent `x86_64-linux-gnu-gcc` guest compiler,
and the development files for GLib, pixman and zlib. Upstream
Python scripts are used unchanged; the Buster harness adds no Python.
The workflow records package versions and compiler identities. QEMU's bundled
Meson wheel is its normal offline bootstrap when the installed Meson is too old;
the action introspects with the Meson selected in each build's upstream venv. This diagnostic
environment is not yet an immutable SDK closure; package, header, linker and
object digests remain a separate full-qualification requirement.

Checkout these dependencies at the revisions already named in QEMU's wrap files:

| Source path below QEMU | Revision | Verified license |
|---|---|---|
| `subprojects/keycodemapdb` | `f5772a62ec52591ff6870b7e8ef32482371f22c6` | GPL-2.0-or-later OR BSD-3-Clause |
| `subprojects/berkeley-softfloat-3` | `b64af41c3276f97f0e181920400ee056b9c88037` | BSD-3-Clause |
| `subprojects/berkeley-testfloat-3` | `e7af9751d9f9fd3b47911f51a5cfd08af256a9ab` | BSD-3-Clause |

No source is vendored into Buster. The hosted workflow checks out the unchanged
GitHub mirrors. Because Meson does not apply wrap packagefiles to an already fetched directory,
the native action copies QEMU's unchanged `meson.build` and `meson_options.txt`
into the two Berkeley projects after pristine input admission. These are the
upstream wrap's build descriptions, not C source patches. Commands are retained;
input admission requires any existing copies to match the pinned QEMU
`subprojects/packagefiles` byte for byte, and verifies the prepared trees again.

## Commands and input boundary

With the trusted Release compiler built, run on authorized cloud infrastructure:

```sh
./build.sh test_qemu --self-test
./build.sh test_qemu /absolute/path/to/qemu /absolute/path/to/build/Release/ide
```

The action refuses a wrong pin, tracked edits, extra untracked/ignored input,
hidden index flags and sparse checkout. Each dependency is verified separately;
the three known dependency directories are the only root ignored-directory
exceptions. Meson's `subprojects/.wraplock` is admitted only as an empty regular
file at its canonical path; arbitrary untracked/ignored files remain refused.
Upstream commands inherit `PYTHONDONTWRITEBYTECODE=1` through recorded `env`
invocations to prevent Python generators from writing source-tree caches.
The guards run before each variant and after both compiler attempts.
Output belongs to a fresh `build/qemu-<pid>-<time>` directory outside QEMU.

The native action runs unmodified upstream `configure`, then
`ninja -j2 -v qemu-system-x86_64`. Buster is supplied as `ide cc`; there is
no Clang fallback for candidate C compilations or feature probes. Clang is an
independent reference in a separate build directory. `--host-cc` also selects
the variant compiler for host C build tools. The independent GNU x86-64 compiler
is explicitly selected with `--cross-cc-x86_64` for upstream guest-side test
compiler probes; those are separate from Buster host C actions and are not a
Buster guest-code-generation claim. Planned Ninja actions are
retained separately from executed verbose logs. Neither planned actions nor
`compile_commands.json` prove successful compiler/object coverage.

When a variant builds, the action retains Meson's test inventory and attempts
`make -j2 check-unit` and `make -j2 check-qtest` independently. Failed configure
or build stages leave explicit not-attempted reasons for later stages. Child
deadlines, capture limits and cleanup failures use the existing compatibility
process contract: 600 seconds per tool child and 16 MiB per output stream.
Timeouts/truncation remain failures. Do not raise these budgets without measuring
the reason. The summary reports stage outcomes; detailed upstream test
denominators, individual skips and failures remain in the upstream test logs.

The [dedicated workflow](../../../.github/workflows/qemu-compatibility.yml)
runs only on explicit pushes to `codex/3185-qemu-tcg-census` or manual dispatch.
It does not add the emulator campaign to ordinary PR, merge-group or main CI.
Each run retains refusal controls, self-host results, commands, stdout/stderr,
child statuses, configure/Meson logs and test inventories. It uploads diagnostic
text, not upstream emulator binaries or firmware.

## License intake and unqualified gates

QEMU's pinned [LICENSE](https://github.com/qemu/qemu/blob/b2995afed254e4804611df52210067a4d5dbdf0b/LICENSE)
states GPL version 2 for the emulator as a whole; individual sources carry
additional GPL, BSD, MIT and other compatible terms. Its
[COPYING](https://github.com/qemu/qemu/blob/b2995afed254e4804611df52210067a4d5dbdf0b/COPYING)
and [COPYING.LIB](https://github.com/qemu/qemu/blob/b2995afed254e4804611df52210067a4d5dbdf0b/COPYING.LIB)
are retained. This root notice is not a completed component audit.

Exact dependency grants:
[keycodemapdb README](https://github.com/qemu/keycodemapdb/blob/f5772a62ec52591ff6870b7e8ef32482371f22c6/README),
[SoftFloat COPYING.txt](https://github.com/ucb-bar/berkeley-softfloat-3/blob/b64af41c3276f97f0e181920400ee056b9c88037/COPYING.txt),
[TestFloat COPYING.txt](https://github.com/ucb-bar/berkeley-testfloat-3/blob/e7af9751d9f9fd3b47911f51a5cfd08af256a9ab/COPYING.txt).
Host dependency and tool package notices must be audited for the actual frozen
package closure before describing complete license intake.

Bundled firmware is separately licensed; see the pinned
[pc-bios README](https://github.com/qemu/qemu/blob/b2995afed254e4804611df52210067a4d5dbdf0b/pc-bios/README).
No guest fixture is supplied by this first stage. Individual QTests may consume
bundled ROMs; their exact firmware/ROM usage, licenses and hashes remain
unqualified until enumerated. A test pass cannot stand for that audit.

Remaining #3185 gates include the complete actual C-action/object ledger,
license/source/generated/system-header closure, applicable functional suite
and explicit upstream test denominators; independently validated guest arithmetic,
memory, process/syscall and device fixtures; trusted guest image boot, expected
serial output and clean shutdown; and clean/source/header-edit build costs,
memory, size and host-emulation workloads. Preserve independent QEMU and
independent guest controls before combining Buster-built components.

An x86-64 emulator running AArch64 guest code does not establish Buster native
AArch64 code generation. Emulated timings are distinct from native CPU timings.
No hosted census timing is approved performance evidence: each performance
conclusion requires the applicable exact-candidate 9700X route. Do not run
development work on the laptop or physical benchmark host.

Buster's first-party grant remains as recorded in
[LICENSES/README.md](../../../LICENSES/README.md); no license is selected here.
Live revisions, observed failures and ownership handoffs belong on #3185/its PR.
