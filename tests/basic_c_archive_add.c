int archive_bias(void);

int add_values(int left, int right)
{
    return left + right + archive_bias();
}
