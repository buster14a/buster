#include <lib.h>

#define QEMU_EXIT_PORT 0xF4
#define QEMU_EXIT_SUCCESS 0x00
#define QEMU_EXIT_FAILURE 0x11

LOCAL void outb(u16 port, u8 value)
{
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

LOCAL u8 inb(u16 port)
{
    u8 ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

[[noreturn]] LOCAL void qemu_exit(int code)
{
    outb(QEMU_EXIT_PORT, code);
    UNREACHABLE();
}

[[noreturn]] EXPORT void _start()
{
    qemu_exit(0);
}
