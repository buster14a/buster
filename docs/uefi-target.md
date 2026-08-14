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
