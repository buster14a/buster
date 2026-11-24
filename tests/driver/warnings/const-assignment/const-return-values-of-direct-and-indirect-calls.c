struct s { int a; const int b; };
const struct s *f(void);
const struct s *(*hp)(void);
void use(const struct s *(*lp)(void))
{
    f()->a = 1;
    hp()->a = 2;
    lp()->a = 3;
    (*hp)()->a = 4;
    hp()->b = 5;
    (&f)()->a = 6;
    (*f)()->a = 7;
    (**f)()->a = 8;
    (&*f)()->a = 9;
    ((f))()->a = 10;
}
