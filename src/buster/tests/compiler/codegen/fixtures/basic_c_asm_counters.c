unsigned long long asm_clear_upper(unsigned long long salt)
{
    __asm__ volatile("vzeroupper" : : : "memory");
    return salt;
}

unsigned long long asm_popcnt16(unsigned long long input)
{
    unsigned short value = input, count;
    __asm__("popcntw %1,%0" : "=a"(count) : "c"(value) : "cc");
    return count;
}

unsigned long long asm_popcnt32(unsigned long long input)
{
    unsigned value = input, count;
    __asm__("popcntl %1,%0" : "=a"(count) : "c"(value) : "cc");
    return count;
}

unsigned long long asm_popcnt64(unsigned long long input)
{
    unsigned long long count;
    __asm__("popcntq %1,%0" : "=a"(count) : "c"(input) : "cc");
    return count;
}

unsigned long long asm_popcnt_plain(unsigned long long input)
{
    unsigned long long count;
    __asm__("popcnt %1,%0" : "=a"(count) : "c"(input) : "cc");
    return count;
}

unsigned long long asm_lzcnt16(unsigned long long input)
{
    unsigned short value = input, count;
    __asm__("lzcntw %1,%0" : "=a"(count) : "c"(value) : "cc");
    return count;
}

unsigned long long asm_lzcnt32(unsigned long long input)
{
    unsigned value = input, count;
    __asm__("lzcntl %1,%0" : "=a"(count) : "c"(value) : "cc");
    return count;
}

unsigned long long asm_lzcnt64(unsigned long long input)
{
    unsigned long long count;
    __asm__("lzcntq %1,%0" : "=a"(count) : "c"(input) : "cc");
    return count;
}

unsigned long long asm_lzcnt_plain(unsigned long long input)
{
    unsigned long long count;
    __asm__("lzcnt %1,%0" : "=a"(count) : "c"(input) : "cc");
    return count;
}
