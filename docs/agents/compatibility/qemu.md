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
Both compiler command lines, including `--host-cc`, pin `-march=x86-64`.
Buster's implicit CPU is the host's detected model, while Clang defaults to the
portable baseline; the explicit common pin aligns predefined feature macros
and selected object ISA before either variant runs. The compiler executable
hash and actual probe/build arguments are retained separately.
The same explicit configure options are used for Clang and Buster:
`--without-default-features --disable-download --disable-docs
--disable-guest-agent --disable-tools --disable-fdt --disable-strip
--enable-install-blobs --disable-rust --extra-cflags="-O0 -g"`.
The normal install-blob manifest supplies QEMU's build-local `qemu-bundle`
firmware symlinks through its unchanged postconfigure script. No install target
runs and no firmware binaries are uploaded. Disabling this manifest removes the
ROM entries from the bundle and makes QTests fail resource lookup, even with an
absolute firmwarepath. Firmware file hashes and its pinned README are retained
before either compiler runs. Normal target
device configuration stays upstream-owned. Optional features are
declared before either compiler runs; a failure must not trigger feature removal.
QEMU's defaults include optimization; the final extra flags request a matched
debug/debug census. Check the retained actual compiler arguments before making
a debug configuration claim.

External prerequisites are Git, Clang, Meson >=1.5, Ninja, make, pkg-config,
Python/venv, coreutils, the independent `x86_64-linux-gnu-gcc` guest compiler,
socat for I/O command tests, xorriso/xorrisofs for CD-ROM fixtures,
and the development files for GLib, pixman and zlib. Upstream
Python scripts are used unchanged; the Buster harness adds no Python.
The retained historical census records package versions, compiler identities and test-tool hashes/notices.
Missing socat or xorrisofs produces upstream skips of applicable tests; the hosted
cell provisioned both before either variant ran. Architecture-inapplicable tests,
disabled optional crypto backends and unavailable host kernel facilities retain
their actual upstream skip reasons. QEMU's bundled
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

No source is vendored into Buster. The historical hosted census checked out the
unchanged GitHub mirrors. Because Meson does not apply wrap packagefiles to an already fetched directory,
the native action copies QEMU's unchanged `meson.build` and `meson_options.txt`
into the two Berkeley projects after pristine input admission. These are the
upstream wrap's build descriptions, not C source patches. Commands are retained;
input admission requires any existing copies to match the pinned QEMU
`subprojects/packagefiles` byte for byte, and verifies the prepared trees again.

## Commands and input boundary

With the trusted Release compiler built, run on authorized cloud infrastructure:

```sh
./build.sh qemu_input_self_test
./build.sh test_qemu /absolute/path/to/qemu /absolute/path/to/build/Release/ide
```

The action refuses a wrong pin, tracked edits, extra untracked/ignored input,
hidden index flags and sparse checkout. Immutable Git probes disable replacement
objects and compare the index to the immutable HEAD tree. Raw no-filter blob
hashes independently compare regular-file checkout bytes to that index, in
bounded batches. File type and executable mode are checked without following
links; symlink targets are compared to their immutable blob. Native controls
reject equal-size/restored-mtime edits even when Git's stat cache reports clean,
as well as executable-mode and regular-file/symlink substitutions. A native control
creates a replacement ref with a clean-looking altered checkout and requires
rejection before any compiler runs. Each dependency is verified separately;
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

Ordinary external-project execution belongs exclusively to the main-push OS
jobs in [external-project CI](../../external-project-ci.md). The earlier
branch/manual QEMU workflow is removed under #3501. This census is not yet
assigned to the shared owner: its source/dependency/firmware intake and full
candidate qualification remain incomplete under #3185. Add a pinned recipe
and assignment to the shared Linux job when ready; do not restore a per-project
workflow or dispatch route. The separate `qemu_input_self_test` command accepts no QEMU source or compiler
inputs: it creates only synthetic Git fixtures and runs in the existing TCC
bootstrap job. The scheduling scanner continues to refuse `test_qemu` routes
outside the shared owner.

Historical run [38137123079](https://github.com/buster14a/buster/actions/runs/38137123079)
at `0abe1ac3216de3dd3bbdbc8111ae6d614f87dc83` retains the independent reference
census and genuine candidate configure failure. It does not validate newer
source. Diagnostic retention includes commands, stdout/stderr, child statuses,
configure/Meson logs and test inventories; no emulator or firmware binaries.

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
No guest image fixture is supplied by this first stage. QTests consume the
pinned PC BIOS/ROM directory; their exact firmware/ROM usage, licenses and hashes remain
unqualified until enumerated. A test pass cannot stand for that audit. QEMU's recorded source links include
[SeaBIOS LGPLv3 notice](https://github.com/coreboot/seabios/blob/a6ed6b701f0a57db0569ab98b0661c12a6ec3ff8/COPYING.LESSER),
[iPXE target-dependent license policy](https://github.com/ipxe/ipxe/blob/4bd064de239dab2426b31c9789a1f4d78087dc63/COPYING),
and the pinned [EDK2 combined notices](https://github.com/qemu/qemu/blob/b2995afed254e4804611df52210067a4d5dbdf0b/pc-bios/edk2-licenses.txt).
These source notices do not establish the complete license/build closure of
each prebuilt ROM; that mapping remains a qualification gate.

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
