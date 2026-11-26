int undeclared_initializer = undeclared;
struct Incomplete incomplete;
void invalid_type;
void invalid_block(void)
{
    extern int initialized_in_block = 1;
}
int valid_initializer = 1;
int valid_tentative;
