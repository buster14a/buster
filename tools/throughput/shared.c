/* Minimal library linkage for the measurement runner and its native tests.
 * build.c compiles this alongside either entry point. Only common foundations
 * are included: no compiler, target detector, application entry point or UI.
 * The tool owns its ProgramState and calling-thread scratch-context lifetime.
 */
#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>

BUSTER_V_IMPL OsState os_state;
static ProgramState throughput_program;
BUSTER_V_IMPL ProgramState* program_state = &throughput_program;

#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
