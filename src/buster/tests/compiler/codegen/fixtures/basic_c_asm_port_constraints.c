// Compile only: port I/O requires operating-system privileges.
void constraint_outb(unsigned char value, unsigned short port)
{
    __asm__ volatile("outb %0,%1" : : "a"(value), "dN"(port));
}

unsigned char constraint_inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "dN"(port));
    return value;
}

void constraint_outb_small(unsigned char value)
{
    __asm__ volatile("outb %0,%1" : : "a"(value), "dN"((unsigned short)255));
}

unsigned char constraint_inb_large(void)
{
    unsigned char value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "dN"((unsigned short)256));
    return value;
}
