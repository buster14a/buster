// Links against basic_c_elf_shared_library.c's library.  Each check sets one
// bit of the exit status; the address of main is printed so two runs of a
// position-independent build can be compared.
int printf(const char* format, ...);
int strcmp(const char* left, const char* right);

extern int shared_counter;
extern int shared_table[4];
extern int* shared_counter_pointer;
int shared_add(int value);
int* shared_counter_address(void);
const char* shared_name(int index);
int shared_read_application(void);
int shared_call_application(int value);
int (*shared_application_function(void))(int);

int application_value = 1000;

int application_callback(int value)
{
    return value * 2;
}

int main(void)
{
    int failed = shared_counter != 41;
    failed |= (shared_counter_address() != &shared_counter) << 1;
    failed |= (shared_counter_pointer != &shared_counter) << 2;
    shared_counter += 1;
    failed |= (shared_add(1) != 1 + 42 + 2 + 6) << 3;
    failed |= (shared_table[3] != 4) << 4;
    failed |= (strcmp(shared_name(1), "beta") != 0) << 5;
    application_value += 1;
    failed |= (shared_read_application() != 1001) << 6;
    failed |= (shared_call_application(5) != 11) << 7;
    failed |= (shared_application_function() != application_callback) << 8;
    printf("main at %p\n", (void*)main);
    return failed;
}
