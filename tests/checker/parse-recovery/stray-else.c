void plain(void)
{
    else;
    after_plain;
}

void nested(int condition)
{
    if (condition)
    {
        else;
        after_nested;
    }
    after_body;
}

void paired(int condition)
{
    if (condition)
    {
    }
    else
    {
    }
    after_paired;
}
