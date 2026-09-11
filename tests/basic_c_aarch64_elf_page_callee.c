int check_page_table(unsigned long long const *value, int index)
{
    return *value != (index ? 0x123456789abcdef0ull : 0x8000000000000001ull);
}
