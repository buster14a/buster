# UEFI compiler target

Buster can compile C directly to freestanding PE32+ EFI applications without a host compiler, CRT, import library, or external linker.

## Supported targets

| Target triple | Firmware architecture | C ABI and data model | Conventional removable-media filename |
|---|---|---|---|
| `x86_64-unknown-uefi` | x86-64 | Microsoft x64 calling convention, LLP64, 16-bit `wchar_t` | `EFI/BOOT/BOOTX64.EFI` |
| `aarch64-unknown-uefi` | AArch64 | AAPCS64, LP64, 16-bit `wchar_t` | `EFI/BOOT/BOOTAA64.EFI` |

Both targets define `__UEFI__`, set `__STDC_HOSTED__` to `0`, emit COFF-compatible intermediate objects, retain PE base relocations, and emit architecture-appropriate PE unwind metadata in `.pdata` and `.xdata`.

## Entry point

The default entry symbol is `UefiMain`. It receives the image handle and the firmware System Table:

```c
typedef unsigned long long EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

EFI_STATUS UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    (void)image_handle;
    (void)system_table;
    return 0;
}
```

The target selects the required calling convention automatically. A different symbol can be selected with `-e`, `--entry`, or `--entry=<symbol>`:

```sh
build/Debug/ide cc --target=x86_64-unknown-uefi --entry=FirmwareEntry -g0 firmware.c -o BOOTX64.EFI
```

## Building images

```sh
build/Debug/ide cc --target=x86_64-unknown-uefi -g0 firmware.c -o BOOTX64.EFI
```

```sh
build/Debug/ide cc --target=aarch64-unknown-uefi -g0 firmware.c -o BOOTAA64.EFI
```

Without `-o`, the executable name is `a.efi`. Debug information is enabled by default; `-g` writes CodeView information into the image and a sibling PDB, while `-g0` omits source-debug data. Firmware unwind metadata is emitted independently of `-g`.

For removable media, place the resulting executable at the architecture-specific path shown above on the EFI System Partition. The compiler emits an EFI application image (`Subsystem = 10`), not a boot-service driver or runtime driver.

## Headers and libraries

UEFI compilation is deliberately freestanding. The driver does not inject host C system-header paths, because host libc headers describe the wrong runtime and ABI. Supply firmware headers explicitly with `-I` or `-isystem`, for example an EDK II include tree.

Multiple C translation units and static archives are supported. `-l` must resolve to a static archive for a UEFI target. Dynamic libraries, import tables, host frameworks, raw `-Wl,` options, thread-local storage, and DWARF unwind sections are rejected rather than silently producing an invalid firmware image.

## Image contract

The native linker writes a PE32+ image with:

- x86-64 (`0x8664`) or AArch64 (`0xaa64`) machine type;
- EFI application subsystem;
- no import directory or CRT startup stub;
- `UefiMain` or the requested entry symbol as `AddressOfEntryPoint`;
- `.reloc` blocks with `IMAGE_REL_BASED_DIR64` entries for absolute addresses;
- `.pdata` and `.xdata` exception/unwind information;
- deterministic section layout and optional CodeView/PDB debug data.

Current deliberate exclusions are UEFI drivers with subsystem 11 or 12, TLS, dynamic imports, and arbitrary external-linker flag passthrough.

## Reference firmware execution gate

`test_uefi` in the native build driver compiles and boots this repository's
`tests/uefi_boot.c` with both targets and all four allocators (`none`,
`mir-stack`, `fast`, `quality`). The existing structural tests remain in
`test_all`; a structural pass does not certify firmware execution.

From the repository root, after building `ide`:

```sh
./build.sh build --config Release -t ide
build/build test_uefi --self-test build/uefi-self-test
build/build test_uefi build/Release/ide build/uefi
```

Both output directories must be new. The harness refuses existing output,
including partial runs, so stale markers and images cannot pass a later run.
The firmware root defaults to `/usr/share`; set `BUSTER_UEFI_FIRMWARE_ROOT`
when packages have been extracted elsewhere. QEMU and `sha256sum` must be on
`PATH`. Missing prerequisites report `status=unavailable runtime_validated=0`
and fail the command. A version/hash mismatch also fails before booting;
there is no structural-only success or optional skip for this gate.

The reference lane uses Ubuntu 24.04 amd64 packages from the immutable
[2026-08-28 Ubuntu snapshot](https://snapshot.ubuntu.com/ubuntu/20260828T000000Z/):

| Input | Pin |
| --- | --- |
| `qemu-system-x86`, `qemu-system-arm`, `qemu-system-common`, `qemu-system-data` | `1:8.2.2+ds-0ubuntu1.18` |
| `ovmf`, `qemu-efi-aarch64` | `2024.02-2ubuntu0.9` (EDK2 2024.02) |
| x86-64 machine / CPU | `pc-q35-8.2` / `qemu64` |
| AArch64 machine / CPU | `virt-8.2` / `cortex-a57` |
| Both machines | TCG, one emulation thread, one CPU, 512 MiB RAM, no network, no display, serial log, no reboot; normal QEMU timers and fixed RTC (`2024-02-01T00:00:00`, VM clock) |

`tools/uefi_boot.c` verifies the exact QEMU version line and SHA-256 of each
firmware code image and pristine variable template. The GitHub `UEFI firmware
boot` job installs those exact packages from the snapshot and is required by
`CI complete`. It uses the ordinary PR merge checkout, main/tag pushes,
merge groups and manual dispatches, under the existing `GH_ACTIONS_CI_ENABLED`
switch. Update the snapshot, package versions, firmware hashes and machine
configuration together when intentionally refreshing the reference lane.

Each boot gets a deterministic raw disk with a 16 MiB FAT16 EFI System
Partition (MBR type `0xef`, starting at LBA 2048), containing `EFI/BOOT/BOOTX64.EFI` or
`EFI/BOOT/BOOTAA64.EFI` and a private copy of the pristine variable store. The C harness writes the
fixed filesystem layout, dates and volume ID directly; it uses no formatter,
mount, or QEMU virtual-FAT backend.
The fixture checks the image handle and system table signature, firmware
service calls, ten-argument direct and indirect calls (including stack
arguments on both ABIs), a volatile stack array, initialized data, zeroed
storage, and absolute function/data pointers with an addend. The harness
requires the PE preferred image base to be 5 GiB; both virtual machines place
RAM below 4 GiB, and the fixture checks its actual entry address is below
4 GiB. The pointer checks therefore require the firmware to apply base
relocations rather than merely load at the preferred address.

A positive run requires exactly one architecture-specific
`BUSTER_UEFI_BOOT_PASS` serial line, no `BUSTER_UEFI_BOOT_FAIL`, and a normal
QEMU exit with status zero after the application calls `ResetSystem` with
`EfiResetShutdown`. A marker followed by a timeout, crash, nonzero exit or
failure marker fails. Every compiler/tool/firmware child has a 120-second
bound. Each architecture also boots a deliberately failing FAST image: its
failure marker and clean shutdown must be observed, and the positive oracle
must reject it. These two negative controls are separate from the eight
positive runtime validations.

The output retains the revision, compiler/fixture/image/media hashes, QEMU version,
firmware hashes, commands (length-prefixed arguments), stdout/stderr, serial
logs, native process status, timeout outcome, EFI images and per-leg verdicts.
CI retains this evidence for 14 days, excluding the large mutable variable
stores. The self-test checks missing, duplicated, wrong-architecture and
failure markers, unsuccessful termination, and a real bounded timeout.
A complete run ends with `UEFI_BOOT_RESULT checks=10/10 unavailable_targets=0
status=pass`; all eight positive boots and both negative controls must pass.
