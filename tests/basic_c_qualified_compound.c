// A narrow right operand must not requalify the arithmetic result.
int qualified_compound_add(volatile int* value, const unsigned char* delta)
{
    *value += delta[0];
    return *value;
}

int main(void)
{
    volatile int value = 5;
    unsigned char delta = 2;
    int failures = qualified_compound_add(&value, &delta) != 7 || value != 7;
    failures += (value += 3) != 10;
    failures += (value -= 2) != 8;
    failures += (value *= 3) != 24;
    failures += (value /= 4) != 6;
    failures += (value %= 4) != 2;
    failures += (value <<= 3) != 16;
    failures += (value >>= 2) != 4;
    failures += (value |= 2) != 6;
    failures += (value ^= 1) != 7;
    failures += (value &= 3) != 3;
    failures += value++ != 3;
    failures += ++value != 5;
    failures += value-- != 5;
    failures += --value != 3;

    struct Box { int field; };
    volatile struct Box box = {4};
    failures += (box.field += 3) != 7 || box.field != 7;

    volatile unsigned char byte = 250;
    failures += (byte += 10) != 4 || byte != 4;
    volatile float scalar = 1.5f;
    failures += (scalar += 2.5f) != 4.0f;
    failures += (scalar /= 2.0f) != 2.0f;

    int elements[3] = {1, 2, 3};
    int* volatile pointer = elements;
    pointer += 2;
    failures += pointer != elements + 2 || *pointer != 3;
    pointer -= 1;
    failures += pointer != elements + 1 || *pointer != 2;
    return failures;
}
