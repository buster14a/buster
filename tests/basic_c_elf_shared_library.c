// A shared library that reaches data every way a library can: its own
// exported data (which an executable may copy-relocate), static data, a
// constant table of pointers, thread-local storage, an initializer array,
// and definitions in the executable that loads it.
int puts(const char* text);

int shared_counter = 40;
int shared_table[4] = {1, 2, 3, 4};
static int shared_hidden = 2;
static const char* const shared_names[] = {"alpha", "beta"};
int* shared_counter_pointer = &shared_counter;
_Thread_local int shared_thread_value = 5;

extern int application_value;
int application_callback(int value);

__attribute__((constructor)) static void shared_constructor(void)
{
    shared_counter += 1;
}

__attribute__((destructor)) static void shared_destructor(void)
{
    puts("shared destructor");
}

int shared_add(int value)
{
    shared_thread_value += 1;
    return value + shared_counter + shared_hidden + shared_thread_value;
}

int* shared_counter_address(void)
{
    return &shared_counter;
}

const char* shared_name(int index)
{
    return shared_names[index];
}

int shared_read_application(void)
{
    return application_value;
}

int shared_call_application(int value)
{
    return application_callback(value) + 1;
}

int (*shared_application_function(void))(int)
{
    return application_callback;
}
