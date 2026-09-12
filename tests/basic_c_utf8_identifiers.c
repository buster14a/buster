// UTF-8 source identifiers survive macros, scopes, calls and symbol lookup.
// Ordinary literals retain byte values, including non-UTF-8 escaped bytes.
#define café 7
#define CONCAT(a, b) a##b

int π = 11;

static int función(int 日本語)
{
    int naïve = 日本語 + café;
    return naïve + π;
}

int main(void)
{
    const unsigned char bytes[] = "\xFF\x80";
    int CONCAT(caf, é_local) = función(3);
    return café_local != 21 || bytes[0] != 255 || bytes[1] != 128;
}
