static char magic[] = "\x7f" "ELF";
static char sized[5] = "ab" u8"cd";
static char *pointer = "pointer" "-" "value";
static char escaped[] =
    "\u00e9" "\U0001f642" "\e";
#if L'\u00e9' != 0xe9 || U'\U0001f642' != 0x1f642
#error wide character constants failed in preprocessing
#endif
typedef __WCHAR_TYPE__ test_wchar;
typedef __CHAR16_TYPE__ test_char16;
typedef __CHAR32_TYPE__ test_char32;
static test_wchar wide[] =
    L"A" L"\U0001f642";
static test_char16 utf16[] =
    u"A" u"\U0001f642";
static test_char32 utf32[] =
    U"A" U"\U0001f642";
static test_wchar *wide_pointer =
    L"wide";
static test_char16 character_constant =
    u'\u00e9';

struct StringHolder
{
    char text[4];
};

static struct StringHolder holder = {
    "st" u8"r",
};

static test_char16 first_utf16(
    test_char16 const *text)
{
    return text[0];
}

int main(void)
{
    char local[] = "lo" u8"cal";
    test_char16 local_utf16[] =
        u"x" u"\u00e9";
    char *local_pointer = "local-pointer";
    test_char16 *local_utf16_pointer =
        u"local-wide";
    unsigned char *expression =
        (unsigned char *)("\x7f" "ELF");
    if (sizeof("a" u8"bc") != 4) return 1;
    if (sizeof("\u00e9" "\U0001f642") != 7)
        return 21;
    if ((unsigned char)escaped[0] != 0xc3 ||
        (unsigned char)escaped[1] != 0xa9 ||
        (unsigned char)escaped[2] != 0xf0 ||
        (unsigned char)escaped[3] != 0x9f ||
        (unsigned char)escaped[4] != 0x99 ||
        (unsigned char)escaped[5] != 0x82 ||
        escaped[6] != 27 ||
        escaped[7] != 0)
        return 22;
#if __WCHAR_WIDTH__ == 16
    if (sizeof(wide) !=
            4 * sizeof(test_wchar) ||
        wide[0] != 0x41 ||
        wide[1] != 0xd83d ||
        wide[2] != 0xde42 ||
        wide[3] != 0)
#else
    if (sizeof(wide) !=
            3 * sizeof(test_wchar) ||
        wide[0] != 0x41 ||
        wide[1] != 0x1f642 ||
        wide[2] != 0)
#endif
        return 23;
    if (sizeof(utf16) !=
            4 * sizeof(test_char16) ||
        utf16[0] != 0x41 ||
        utf16[1] != 0xd83d ||
        utf16[2] != 0xde42 ||
        utf16[3] != 0)
        return 24;
    if (sizeof(u"A" u"\U0001f642") !=
            4 * sizeof(test_char16) ||
        (u"A" u"\u00e9")[1] != 0xe9 ||
        first_utf16(u"Q") != 'Q')
        return 31;
    if (sizeof(utf32) !=
            3 * sizeof(test_char32) ||
        utf32[0] != 0x41 ||
        utf32[1] != 0x1f642 ||
        utf32[2] != 0)
        return 25;
    if (wide_pointer[0] != 'w' ||
        wide_pointer[3] != 'e' ||
        wide_pointer[4] != 0)
        return 26;
    if (sizeof(local_utf16) !=
            3 * sizeof(test_char16) ||
        local_utf16[0] != 'x' ||
        local_utf16[1] != 0xe9 ||
        local_utf16[2] != 0)
        return 27;
    if (u'\u00e9' != 0xe9 ||
        U'\U0001f642' != 0x1f642 ||
        L'\u00e9' != 0xe9 ||
        u8'a' != 'a')
        return 28;
    if (character_constant != 0xe9)
        return 30;
    if (local_pointer[5] != '-' ||
        local_utf16_pointer[5] != '-')
        return 29;
    if (sizeof(local) != 6) return 2;
    if (magic[0] != 0x7f) return 3;
    if (magic[1] != 'E') return 4;
    if (magic[3] != 'F') return 5;
    if (magic[4] != 0) return 6;
    if (sized[3] != 'd') return 7;
    if (sized[4] != 0) return 8;
    if (pointer[7] != '-') return 9;
    if (pointer[12] != 'e') return 10;
    if (holder.text[2] != 'r') return 11;
    if (holder.text[3] != 0) return 12;
    if (expression[0] != 0x7f) return 13;
    if (expression[1] != 'E') return 14;
    if (local[0] != 'l') return 15;
    if (local[1] != 'o') return 16;
    if (local[2] != 'c') return 17;
    if (local[3] != 'a') return 18;
    if (local[4] != 'l') return 19;
    if (local[5] != 0) return 20;
    return 0;
}
