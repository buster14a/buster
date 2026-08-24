// Freestanding AArch64 entry point for the clang/buster c_abi pair.  The
// compiler driver supplies no libc startup when linking a target image, so
// return main's status through Linux's exit syscall for qemu-aarch64.
.text
.global _start
.type _start, %function
_start:
    bl main
    mov x8, #93
    svc #0
