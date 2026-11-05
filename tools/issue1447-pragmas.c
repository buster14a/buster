// Original probes for Linux/x86-64 pragma handling in pinned Clang 23.1.4.
#if CASE == 1
#pragma scalar_storage_order big-endian
#elif CASE == 2
#pragma GCC scalar_storage_order big-endian
#elif CASE == 3
#pragma ms_struct on
#elif CASE == 4
#pragma data_seg("custom_data")
#elif CASE == 5
#pragma code_seg("custom_code")
#elif CASE == 6
#pragma bss_seg("custom_bss")
#elif CASE == 7
#pragma const_seg("custom_const")
#elif CASE == 8
#pragma init_seg(user)
#elif CASE == 9
#pragma section("custom_section", read, write)
#elif CASE == 10
#pragma GCC target("avx2")
#elif CASE == 11
#pragma fenv_access(on)
#elif CASE == 12
#pragma float_control(precise, on)
#elif CASE == 13
#pragma float_control(precise, off)
#elif CASE == 14
#pragma float_control(except, on)
#elif CASE == 15
#pragma float_control(except, off)
#elif CASE == 16
#pragma float_control(push)
#elif CASE == 17
#pragma float_control(pop)
#elif CASE == 18
#pragma float_control(precise, off, push)
#elif CASE == 19
#pragma clang fp contract(fast)
#elif CASE == 20
#pragma clang fp reassociate(on)
#elif CASE == 21
#pragma clang fp exceptions(strict)
#elif CASE == 22
#pragma clang fp exceptions(ignore)
#elif CASE == 23
#pragma clang fp exceptions(maytrap)
#elif CASE == 24
#pragma clang fp reciprocal(on)
#elif CASE == 25
#pragma clang fp eval_method(double)
#elif CASE == 26
#pragma weak value
#elif CASE == 27
#pragma weak alias = value
extern int alias;
#elif CASE == 28
#pragma redefine_extname value renamed
#elif CASE == 29
#pragma redefine_extname function renamed_function
#elif CASE == 30
#pragma weak undeclared
#elif CASE == 31
#pragma weak value trailing
#elif CASE == 32
#pragma redefine_extname value renamed trailing
#elif CASE == 33
#pragma ms_struct reset
#elif CASE == 34
#pragma ms_struct nonsense
#elif CASE == 35
#pragma scalar_storage_order nonsense
#elif CASE == 36
#pragma data_seg(nonsense
#endif

struct Layout { unsigned char first : 7; unsigned int second : 3; char last; };
int layout_size = sizeof(struct Layout);
int value = 7;
int zero;
const int constant = 11;
double function(double a, double b, double c) { return a * b + c; }

#if CASE == 37
#pragma weak value
#elif CASE == 38
#pragma redefine_extname value renamed
#elif CASE == 39
#pragma redefine_extname function renamed_function
#elif CASE == 40
extern int alias __attribute__((alias("value"), selectany));
#elif CASE == 41
extern int alias __attribute__((selectany, alias("value")));
#elif CASE == 42
static int internal = 13;
#pragma weak internal
#elif CASE == 43
#pragma weak alias = value
extern int alias;
#elif CASE == 44
#pragma weak alias = undeclared
extern int alias;
#elif CASE == 45
double within(double a, double b)
{
    int n = 1;
#pragma fenv_access(on)
    return a / b + n;
}
#elif CASE == 46
#pragma float_control(precise, off)
#pragma fenv_access(on)
#elif CASE == 47
#pragma float_control(precise, off)
#pragma float_control(except, on)
#elif CASE == 48
#pragma clang fp reassociate(on) contract(off)
#endif
