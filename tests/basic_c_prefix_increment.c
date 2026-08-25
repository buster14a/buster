static int advance_and_rewind(char **input, char *begin)
{
    ++(*input);
    if (*input != begin + 1)
    {
        return 0;
    }
    --(*input);
    return *input == begin;
}

int main(void)
{
    char text[] = "xy";
    char *cursor = text;
    return advance_and_rewind(&cursor, text) ? 0 : 1;
}
