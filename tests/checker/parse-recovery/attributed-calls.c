int known(int);
int familiar(int);
int object;

void first(void)
{
    __attribute__((unused)) missing_one(unread_one, (unread_two + 1));
    missing_one(1);
    __attribute__((unused)) known(unread_three);
    __attribute__((unused)) object(unread_four);
    known();
    object(1);
    after_first;
}

void second(void)
{
    __attribute__((unused)) __attribute__((aligned(8))) missing_two();
    __attribute__((unused)) famliar(unread_five);
    __attribute__((unused)) __builtin_missing(unread_six);
    __attribute__((unused)) __builtin_cts(unread_seven);
    after_second;
}
