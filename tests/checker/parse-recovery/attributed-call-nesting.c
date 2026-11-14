int known(int);

void check(void)
{
    __attribute__((unused)) known(unread(1, (int[2]){ 2, 3 }));
    __attribute__((unused)) known(({ int ignored = 1; another; ignored; }));
    __attribute__((unused)) known(1 +);
    __attribute__((unused)) known(((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((unread)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
    after_nesting;
}
