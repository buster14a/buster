int known(int);

void rejected(int value)
{
    int* pointer = value;
    (void)pointer;
    __attribute__((unused)) known(unread_one);
    after_rejected;
}

void repaired(void)
{
    __attribute__((unused)) known(unread_two);
    after_repair;
