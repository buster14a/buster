typedef unsigned char Byte;

int main(void)
{
    Byte bytes[3] = {1, 2, 3};
    // Explicit casts use the same array-to-pointer decay as an ordinary
    // expression.  Keep both destinations here: Unity's pointer assertions
    // cast string arrays to integers while cJSON uses array members as
    // pointers.
    Byte *pointer = (Byte *)bytes;
    unsigned long address = (unsigned long)bytes;
    return pointer[2] == 3 && address != 0 ? 0 : 1;
}
