// Buster-linked Android executable for android/runtime_order: run by
// android/runtime_order.sh on the emulator. Every event takes the next tick
// from the shared library's counter, so the ticks record bionic's order:
//   1, 2  the two DT_PREINIT_ARRAY entries, in array order;
//   3     the dependency's constructor;
//   4     this executable's constructor.
// The first preinit entry also calls `scale`, a GNU IFUNC defined in
// indirect.c (Buster's C frontend has no ifunc attribute; the linker accepts
// IFUNC definitions from foreign ELF objects). Buster's IFUNC runner is the
// first preinit entry, so the slot is filled before any user entry runs, and
// the resolver runs exactly once.
int printf(char const*, ...);
int runtime_order_tick(void);
int runtime_order_dependency_constructed(void);

static volatile int first_preinit;
static volatile int second_preinit;
static volatile int program_constructor;
static volatile int preinit_arguments;
static volatile int preinit_scale;

int scale(int);
int runtime_order_resolutions(void);
int (*volatile scale_pointer)(int) = scale;

static void first_entry(int argc, char** argv, char** envp)
{
    first_preinit = runtime_order_tick();
    preinit_arguments = argc >= 1 && argv && argv[0] && envp;
    preinit_scale = scale(7);
}

static void second_entry(int argc, char** argv, char** envp)
{
    (void)argc;
    (void)argv;
    (void)envp;
    second_preinit = runtime_order_tick();
}

__attribute__((section(".preinit_array"), used)) static void (*preinit_entries[])(int, char**, char**) = {first_entry, second_entry};

__attribute__((constructor)) static void program_entry(void)
{
    program_constructor = runtime_order_tick();
}

int main(void)
{
    int dependency = runtime_order_dependency_constructed();
    int (*local)(int) = scale;
    int ifunc = scale(7) == 21 && local(7) == 21 && scale_pointer(7) == 21 && local == scale_pointer && preinit_scale == 21 && runtime_order_resolutions() == 1;
    int ordered = first_preinit == 1 && second_preinit == 2 && dependency == 3 && program_constructor == 4 && preinit_arguments;
    printf("BUSTER_ANDROID_RUNTIME_ORDER preinit=%d,%d dependency=%d program=%d arguments=%d ifunc=%d,%d resolutions=%d status=%s\n",
           first_preinit, second_preinit, dependency, program_constructor, preinit_arguments, preinit_scale, scale(7), runtime_order_resolutions(),
           ordered && ifunc ? "ok" : "wrong");
    return !(ordered && ifunc);
}
