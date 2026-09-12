// Deliberately scalar, with no vector extension in the oracle. All ABI
// boundaries are pointers and all expected values use host scalar semantics.
void arithmetic_bytes(void*, void const*, void const*);
void arithmetic_shorts(void*, void const*, void const*);
void arithmetic_words(void*, void const*, void const*);
void arithmetic_longs(void*, void const*, void const*);
void arithmetic_doubles(void*, void const*, void const*);
void arithmetic_word_masks(void*, void const*, void const*);

int main(void)
{
    int result = 0;
    union ArithmeticImage
    {
        signed char bytes[64];
        short shorts[32];
        unsigned int words[16];
        unsigned long long longs[8];
        double doubles[8];
    };
    _Alignas(64) union ArithmeticImage left = {0}, right = {0};
    _Alignas(64) struct { unsigned char before[64]; union ArithmeticImage value; unsigned char after[64]; } output;
    for (unsigned index = 0; index < 64; index += 1)
    {
        output.before[index] = (unsigned char)(index + 17);
        output.value.bytes[index] = 85;
        output.after[index] = (unsigned char)(index + 97);
    }
    for (unsigned index = 0; index < 8; index += 1)
    {
        left.bytes[index] = (signed char)((int)index * 7 - 27);
        right.bytes[index] = (signed char)((index & 1) ? -3 : 3);
    }
    arithmetic_bytes(&output.value, &left, &right);
    for (unsigned index = 0; index < 8; index += 1)
    {
        int a = left.bytes[index], b = right.bytes[index];
        signed char expected = (signed char)(((a / b) + (a % b)) ^ ((a >> 1) + (a < b ? -1 : 0)));
        result |= output.value.bytes[index] != expected;
    }
    for (unsigned index = 8; index < 64; index += 1) { result |= output.value.bytes[index] != 85; }
    for (unsigned index = 0; index < 8; index += 1)
    {
        left.shorts[index] = (short)((int)index * 7011 - 30000);
        right.shorts[index] = (short)((index & 1) ? -7 : 11);
    }
    arithmetic_shorts(&output.value, &left, &right);
    for (unsigned index = 0; index < 8; index += 1)
    {
        int a = left.shorts[index], b = right.shorts[index];
        short expected = (short)((-a / b) ^ (~a + (a >= b ? -1 : 0)));
        result |= output.value.shorts[index] != expected;
    }
    for (unsigned index = 16; index < 64; index += 1) { result |= output.value.bytes[index] != 85; }
    for (unsigned index = 0; index < 8; index += 1)
    {
        left.words[index] = 0xfffffff0u - index * 1234567u;
        right.words[index] = index * 13u + 7u;
    }
    arithmetic_words(&output.value, &left, &right);
    for (unsigned index = 0; index < 8; index += 1)
    {
        unsigned int a = left.words[index], b = right.words[index];
        unsigned int expected = ((a / b) + (a % b)) ^ (a << 2);
        result |= output.value.words[index] != expected;
    }
    for (unsigned index = 32; index < 64; index += 1) { result |= output.value.bytes[index] != 85; }
    left.words[0] = right.words[0];
    arithmetic_word_masks(&output.value, &left, &right);
    for (unsigned index = 0; index < 8; index += 1)
    {
        result |= output.value.words[index] != (left.words[index] > right.words[index] ? ~0u : 0);
    }
    for (unsigned index = 0; index < 8; index += 1)
    {
        left.longs[index] = 0xffffffffffffffffull - index * 123456789ull;
        right.longs[index] = index * 37ull + 7ull;
    }
    arithmetic_longs(&output.value, &left, &right);
    for (unsigned index = 0; index < 8; index += 1)
    {
        unsigned long long a = left.longs[index], b = right.longs[index];
        result |= output.value.longs[index] != ((a * b) ^ (a / b)) + (a % b);
    }
    for (unsigned index = 0; index < 8; index += 1)
    {
        left.doubles[index] = (double)index * 1.25 - 4.5;
        right.doubles[index] = (double)(1u << (index & 3));
    }
    arithmetic_doubles(&output.value, &left, &right);
    for (unsigned index = 0; index < 8; index += 1)
    {
        double a = left.doubles[index], b = right.doubles[index];
        result |= output.value.doubles[index] != -((a + b) * b - a / b);
    }
    for (unsigned index = 0; index < 64; index += 1)
    {
        result |= output.before[index] != (unsigned char)(index + 17);
        result |= output.after[index] != (unsigned char)(index + 97);
    }
    return result;
}
