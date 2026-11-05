void [[gnu::stdcall]] f(void);
void [[gnu::noreturn]] g(void);
void [[gnu::regparm(2)]] h(void);
typedef void F(void);
F [[gnu::stdcall]] k;
