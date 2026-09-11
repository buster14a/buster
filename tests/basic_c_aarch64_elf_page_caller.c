// Compile this translation unit with Clang -O2. Its address-taken constant
// table requires ordinary ELF ADRP/ADD relocations, including a +8 addend.
extern int check_page_table(unsigned long long const *value, int index);
int main(void)
{
    static unsigned long long const table[2] = {0x8000000000000001ull, 0x123456789abcdef0ull};
    return check_page_table(table, 0) || check_page_table(table + 1, 1);
}
