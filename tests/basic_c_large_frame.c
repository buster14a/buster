static int large_frame(void)
{
    unsigned char bytes[40000];
    bytes[0] = 13;
    bytes[39999] = 17;
    // Exercise bounded zero initialization, all copy directions, and an odd
    // byte tail without adding a runtime memory helper dependency.
    struct LargeZero
    {
        unsigned char data[4099];
        unsigned marker;
        float number;
        void *pointer;
        _Bool enabled;
    };
    struct LargeZero initial = {.marker = 23, .number = 1.25f};
    struct LargeZero copied = {0};
    struct LargeZero *destination = &copied;
    *destination = initial;
    struct ByteTail { unsigned char data[4099]; } tail = {0};
    struct ByteTail duplicate;
    tail.data[4098] = 71;
    duplicate = tail;
    int valid = copied.marker == 23 && copied.number == 1.25f &&
                copied.pointer == 0 && copied.enabled == 0;
    for (unsigned index = 0; index < 4099; index += 1)
    {
        valid = valid && copied.data[index] == 0 &&
                duplicate.data[index] == (index == 4098 ? 71 : 0);
    }
    return valid ? bytes[0] + bytes[39999] : 0;
}

int main(void)
{
    return large_frame() == 30 ? 0 : 1;
}
