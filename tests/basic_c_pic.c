int buster_pic_global;
int buster_pic_values[2] = {11, 23};

int* buster_pic_address(void)
{
    return &buster_pic_global;
}

int buster_pic_read(void)
{
    return buster_pic_global;
}

// A runtime index retains a signed absolute displacement in non-PIC x86-64
// objects from both GCC and Clang, rather than relying on pointer truncation.
int buster_pic_index(int index)
{
    return buster_pic_values[index];
}

// Address arithmetic narrowed to 32 bits. Both GCC and Clang load the GOT
// slot into a 32-bit register here, without the REX prefix a 64-bit address
// needs: the R_X86_64_GOTPCRELX shapes the psABI converts to an absolute
// immediate rather than to an address computation.
unsigned buster_pic_low_address(void)
{
    return (unsigned)(unsigned long)&buster_pic_global;
}

unsigned buster_pic_low_distance(void)
{
    return (unsigned)((unsigned long)&buster_pic_values[0] - (unsigned long)&buster_pic_global);
}

int main(void)
{
    // The same two objects as full 64-bit addresses, which the linker relaxes
    // by the other rule. A volatile pointer keeps the comparison a runtime
    // one, so a wrong immediate disagrees with an address instead of matching
    // a second copy of itself.
    int* volatile global_address = &buster_pic_global;
    int* volatile values_address = buster_pic_values;
    buster_pic_global = 7;
    return buster_pic_address() != &buster_pic_global || buster_pic_read() != 7 ||
           buster_pic_index(0) != 11 || buster_pic_index(1) != 23 ||
           buster_pic_low_address() != (unsigned)(unsigned long)global_address ||
           buster_pic_low_distance() != (unsigned)((unsigned long)values_address - (unsigned long)global_address);
}
