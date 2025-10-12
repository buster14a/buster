#!/usr/bin/env python3
import os
import subprocess
import sys
import urllib.request
import shutil

def kvm_available():
    return os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.R_OK | os.W_OK)

is_ci = os.environ.get("BUSTER_CI", "0") == "1"
# Get OVMF
ovmf_dir = "ovmf"
arch = "x86_64"
if not os.path.exists(ovmf_dir):
    os.makedirs(ovmf_dir, exist_ok=True)
    target = f"{ovmf_dir}/ovmf-code-{arch}.fd"
    url = f"https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/ovmf-code-{arch}.fd"

    print(f"Downloading {url} -> {target}")
    urllib.request.urlretrieve(url, target)

    if arch == "aarch64":
        # Extend/truncate to 64 MiB
        subprocess.run(["dd", "if=/dev/zero", f"of={target}", "bs=1", "count=0", "seek=67108864"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    elif arch == "riscv64":
        # Extend/truncate to 32 MiB
        subprocess.run(["dd", "if=/dev/zero", f"of={target}", "bs=1", "count=0", "seek=33554432"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

# generate image
iso_root = "iso_root"
kernel_src = "build/buster_kernel"

result = subprocess.run(["make"], cwd="limine")

if result.returncode == 1:
    sys.exit(1)

if os.path.exists(iso_root):
    shutil.rmtree(iso_root)

os.makedirs(f"{iso_root}/boot/limine", exist_ok=True)
os.makedirs(f"{iso_root}/EFI/BOOT", exist_ok=True)

print(f"Copying {kernel_src} -> {iso_root}/boot/")
shutil.copy(kernel_src, f"{iso_root}/boot/kernel")

print("Copying limine.conf -> iso_root/boot/limine/")
shutil.copy("limine.conf", f"{iso_root}/boot/limine/")

if arch == "x86_64":
    files_to_copy = [
        "limine/limine-bios.sys",
        "limine/limine-bios-cd.bin",
        "limine/limine-uefi-cd.bin",
    ]
    for f in files_to_copy:
        print(f"Copying {f} -> iso_root/boot/limine/")
        shutil.copy(f, f"{iso_root}/boot/limine/")

    shutil.copy("limine/BOOTX64.EFI", f"{iso_root}/EFI/BOOT/")
    shutil.copy("limine/BOOTIA32.EFI", f"{iso_root}/EFI/BOOT/")

    subprocess.run([
        "xorriso", "-as", "mkisofs", "-R", "-r", "-J",
        "-b", "boot/limine/limine-bios-cd.bin",
        "-no-emul-boot", "-boot-load-size", "4", "-boot-info-table",
        "-hfsplus", "-apm-block-size", "2048",
        "--efi-boot", "boot/limine/limine-uefi-cd.bin",
        "-efi-boot-part", "--efi-boot-image", "--protective-msdos-label",
        iso_root, "-o", "build/image.iso"
    ], check=True)

    subprocess.run(["./limine/limine", "bios-install", f"build/image.iso"], check=True)

elif arch in ("aarch64", "riscv64", "loongarch64"):
    efi_name = {
        "aarch64": "BOOTAA64.EFI",
        "riscv64": "BOOTRISCV64.EFI",
        "loongarch64": "BOOTLOONGARCH64.EFI"
    }[arch]

    print(f"Copying limine/limine-uefi-cd.bin -> iso_root/boot/limine/")
    shutil.copy("limine/limine-uefi-cd.bin", f"{iso_root}/boot/limine/")
    print(f"Copying limine/{efi_name} -> iso_root/EFI/BOOT/")
    shutil.copy(f"limine/{efi_name}", f"{iso_root}/EFI/BOOT/")

    subprocess.run([
        "xorriso", "-as", "mkisofs", "-R", "-r", "-J",
        "-hfsplus", "-apm-block-size", "2048",
        "--efi-boot", "boot/limine/limine-uefi-cd.bin",
        "-efi-boot-part", "--efi-boot-image", "--protective-msdos-label",
        iso_root, "-o", f"build/image.iso"
    ], check=True)

shutil.rmtree(iso_root)
print(f"ISO built: build/image.iso")

# run it

qemu_args = [
    "qemu-system-x86_64",
	"-M", "q35",
	"-drive", "if=pflash,unit=0,format=raw,file=ovmf/ovmf-code-x86_64.fd,readonly=on",
	"-cdrom", "build/image.iso",
    "-no-reboot",
    "-display", "none" if is_ci else "gtk",
    "-d", "int,guest_errors,in_asm",
    "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
]

do_kvm = is_ci and kvm_available()

if do_kvm:
    qemu_args.append("-cpu"),
    qemu_args.append("host"),
    qemu_args.append("-accel")
    qemu_args.append("kvm")
else:
    qemu_args.append("-cpu"),
    qemu_args.append("max"),

debug = True if len(sys.argv) > 1 and sys.argv[1].lower() == "debug" else False

if debug:
    qemu_args.append("-s")
    qemu_args.append("-S")

print(qemu_args)

if debug:
    qemu_result = subprocess.Popen(qemu_args)
    result = subprocess.run([
        "kitty",
        "gdb",
        "-ex", "set disassembly-flavor intel",
        "-ex", "target remote :1234",
        "-ex", "hb _start",
        "-ex", "c",
        kernel_src,
    ])
else:
    result = subprocess.run(qemu_args)

print(f"Raw: {result.returncode}")
return_code = result.returncode >> 1
if return_code != 0:
    print("QEMU failed to run successfully!\n")
sys.exit(return_code)
