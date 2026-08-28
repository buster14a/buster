// The value of `++E` and of `E op= x` is what E holds after the store, in E's
// own type.  The arithmetic runs in the promoted type, so a narrow object's
// result has to come back down before anything reads it.  SQLite carries its
// page-header cell count into the high byte with
// `if( (++data[hdr+4])==0 ) data[hdr+3]++;`: a comparison that sees 256
// instead of 0 loses the carry, and every b-tree page past 255 cells then
// reports the wrong cell count on disk.
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef short s16;

static u8 header[4] = {0, 255, 0, 0};

static void add_cell(u8 *data)
{
    if ((++data[1]) == 0) data[0] += 1;
}

int main(void)
{
    u8 bytes[4] = {7, 255, 254, 1};
    s8 signed_byte = 127;
    u16 wide = 65535;
    s16 signed_wide = 32767;
    unsigned counter = 0;

    add_cell(header);
    if (header[1] != 0 || header[0] != 1) return 1;

    if ((++bytes[1]) != 0) return 2;
    if (bytes[1] != 0) return 3;
    if ((bytes[2] += 2) != 0) return 4;
    if (bytes[2] != 0) return 5;
    if ((++signed_byte) != -128) return 6;
    if ((++wide) != 0) return 7;
    if ((++signed_wide) != -32768) return 8;
    if ((--bytes[3]) != 0) return 9;
    if ((--bytes[3]) != 255) return 10;
    // The postfix forms produce the value from before the update, which is
    // already in the object's type.
    if ((bytes[0]++) != 7 || bytes[0] != 8) return 11;
    if ((bytes[1]--) != 0 || bytes[1] != 255) return 12;
    // A wide place keeps the promoted result: nothing is truncated here.
    counter = 255;
    if ((++counter) != 256) return 13;
    if ((counter += 4294967040u) != 4294967040u + 256u) return 14;
    return 0;
}
