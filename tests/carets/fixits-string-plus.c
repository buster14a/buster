const char *literal_left(int index)
{
    return "abc" + index;
}

const char *literal_right(int index)
{
    return index + "abc";
}

const char *character_right(const char *pointer)
{
    return pointer + 'a';
}

const char *character_left(const char *pointer)
{
    return 'a' + pointer;
}

const char *grouped_index(int index)
{
    return "abc" + (index + 1);
}
