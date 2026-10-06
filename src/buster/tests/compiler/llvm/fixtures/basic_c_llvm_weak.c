extern int optional_function(void) __attribute__((weak));
extern int optional_data __attribute__((weak));
extern int mandatory_function(void);

__attribute__((weak)) int selected_data = 11;
__attribute__((weak)) int selected_function(void)
{
    return 11;
}

int ordinary_data = 7;
int ordinary_function(void)
{
    return 13;
}

static int private_data = 3;
static int private_function(void)
{
    return 5;
}

int weak_presence(void)
{
    return (optional_function != 0) + 2 * (&optional_data != 0);
}

int weak_direct(void)
{
    int result = 0;
    if (optional_function != 0)
    {
        result += optional_function();
    }
    if (&optional_data != 0)
    {
        result += optional_data;
    }
    return result;
}

int weak_indirect(void)
{
    int (*function)(void) = optional_function;
    int* data = &optional_data;
    int result = 0;
    if (function != 0)
    {
        result += function();
    }
    if (data != 0)
    {
        result += *data;
    }
    return result;
}

int weak_selected(void)
{
    return selected_data + selected_function();
}

int weak_ordinary(void)
{
    return ordinary_data + ordinary_function() + private_data + private_function();
}

int weak_required(void)
{
    return mandatory_function();
}
