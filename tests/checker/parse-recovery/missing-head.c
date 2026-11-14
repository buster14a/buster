void plain(void)
{
    while;
    after_plain;
}

void corrected(void)
{
    whil;
    after_corrected;
}

void conditional(void)
{
    if;
    after_conditional;
}

void counted(void)
{
    for;
    after_counted;
}

void nested(int condition)
{
    if (condition)
    {
        while;
        after_nested;
    }
    after_body;
}
