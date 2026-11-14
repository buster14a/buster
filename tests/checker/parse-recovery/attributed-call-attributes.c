int old(int) __attribute__((deprecated));
int hidden(int) __attribute__((unavailable));
int restricted(int value) __attribute__((enable_if(value > 0, "positive")));
int no_return(int) __attribute__((noreturn));

void check(void)
{
    __attribute__((unused)) old(unread_one);
    __attribute__((unused)) hidden(unread_two);
    __attribute__((unused)) restricted(unread_three);
    __attribute__((unused)) no_return(unread_four);
    __attribute__((unknown_attribute)) old(unread_five);
    after_attributes;
}
