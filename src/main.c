#include <lib.h>
#include <limine.h>

#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};
__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

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
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        qemu_exit(1);
    }

    if ((framebuffer_request.response == 0) | (framebuffer_request.response->framebuffer_count < 1))
    {
        qemu_exit(1);
    }

    let framebuffer = framebuffer_request.response->framebuffers[0];

    for (u64 i = 0; i < 100; i++)
    {
        volatile u32 *fb_ptr = framebuffer->address;
        fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
    }

    qemu_exit(0);
}
