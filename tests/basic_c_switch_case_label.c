// A case label is folded in the type it is spelled in, and C converts it to
// the promoted type of the controlling expression before it can match.  Only
// the GNU `case a ... b` path used to run that conversion, so a plain label
// reached dispatch carrying its own narrow bits: `case -1` on a `long long`
// switch was the immediate 0xffffffff, which never equals -1 and cannot be
// told apart from `case 4294967295`.  Every negative label below is therefore
// spelled without an `LL` suffix, which is what made the bug invisible --
// `case -1LL` was already 64 bits wide and always worked.
//
// The narrow switches are the other half of the contract: promotion, not
// truncation to the switched value's own type.  `case -1` on an
// `unsigned char` switch stays -1 and must never match 255.
//
// `char` is signed on x86-64 Linux and unsigned on AArch64, so every narrow
// switch names its signedness rather than depending on the target's.

static long long wide_input;
static unsigned long long unsigned_wide_input;
static signed char narrow_input;

static int wide_negative(long long value)
{
    switch (value)
    {
    case -1:
        return 11;
    default:
        return 31;
    }
}

// The two labels reduce to the same 32 bits unconverted, which used to trip
// the duplicate-label check and reject the whole function body.
static int wide_pair(long long value)
{
    switch (value)
    {
    case -1:
        return 12;
    case 4294967295LL:
        return 22;
    default:
        return 32;
    }
}

// Labels narrower than the switch, spelled by casting rather than by literal
// type: the signed one sign-extends to -1, the unsigned one zero-extends.
static int wide_cast_labels(long long value)
{
    switch (value)
    {
    case (signed char)-1:
        return 13;
    case (unsigned)-1:
        return 23;
    default:
        return 33;
    }
}

static int unsigned_wide(unsigned long long value)
{
    switch (value)
    {
    case -1:
        return 14;
    case 1:
        return 24;
    default:
        return 34;
    }
}

static int narrow(signed char value)
{
    switch (value)
    {
    case -1:
        return 15;
    case 100:
        return 25;
    default:
        return 35;
    }
}

// `unsigned char` promotes to `int`, so -1 stays -1 and 255 stays 255.
// Converting to the switched type instead would fold the two labels into one
// and report an overlap.
static int unsigned_narrow(unsigned char value)
{
    switch (value)
    {
    case -1:
        return 16;
    case 255:
        return 26;
    default:
        return 36;
    }
}

static int narrow_short(short value)
{
    switch (value)
    {
    case -1:
        return 17;
    case 32767:
        return 27;
    default:
        return 37;
    }
}

static int plain_int(int value)
{
    switch (value)
    {
    case -1:
        return 18;
    case 2147483647:
        return 28;
    default:
        return 38;
    }
}

// Ranges and plain labels in one switch: both kinds have to reach the same
// type, or the ordering and overlap checks compare a converted bound against
// an unconverted one.
static int mixed(long long value)
{
    switch (value)
    {
    case -4 ... -3:
        return 19;
    case -1:
        return 29;
    case 4294967294LL ... 4294967295LL:
        return 39;
    default:
        return 49;
    }
}

int main(void)
{
    if (wide_negative(-1) != 11 || wide_negative(4294967295LL) != 31 || wide_negative(0) != 31)
    {
        return 1;
    }
    if (wide_pair(-1) != 12 || wide_pair(4294967295LL) != 22 || wide_pair(1) != 32)
    {
        return 2;
    }
    if (wide_cast_labels(-1) != 13 || wide_cast_labels(4294967295LL) != 23 || wide_cast_labels(255) != 33)
    {
        return 3;
    }
    if (unsigned_wide(0xffffffffffffffffull) != 14 || unsigned_wide(1) != 24 || unsigned_wide(4294967295ull) != 34)
    {
        return 4;
    }
    if (narrow(-1) != 15 || narrow(100) != 25 || narrow(0) != 35)
    {
        return 5;
    }
    if (unsigned_narrow(255) != 26 || unsigned_narrow(0) != 36)
    {
        return 6;
    }
    if (narrow_short(-1) != 17 || narrow_short(32767) != 27 || narrow_short(0) != 37)
    {
        return 7;
    }
    if (plain_int(-1) != 18 || plain_int(2147483647) != 28 || plain_int(0) != 38)
    {
        return 8;
    }
    if (mixed(-4) != 19 || mixed(-3) != 19 || mixed(-1) != 29 || mixed(4294967295LL) != 39 || mixed(0) != 49)
    {
        return 9;
    }
    // The same dispatches again through mutable globals, so a constant
    // argument the lowering could fold away is not what is being measured.
    wide_input = -1;
    unsigned_wide_input = 0xffffffffffffffffull;
    narrow_input = -1;
    if (wide_negative(wide_input) != 11 || wide_pair(wide_input) != 12 || wide_cast_labels(wide_input) != 13)
    {
        return 10;
    }
    if (unsigned_wide(unsigned_wide_input) != 14 || narrow(narrow_input) != 15 || mixed(wide_input) != 29)
    {
        return 11;
    }
    return 0;
}
