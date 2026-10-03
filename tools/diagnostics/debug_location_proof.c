#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Hosted-only #2528 proof utility: write the two fixed source populations,
// or locate unique unmodified codegen lines for a GDB allocation observer.
static bool write_source(char const* shape, uint32_t count, char const* output)
{
    bool loops = !strcmp(shape, "loops");
    bool written = loops || !strcmp(shape, "ifs");
    FILE* file = written ? fopen(output, "wb") : 0;
    written = written && file;
    if (written)
    {
        written = fprintf(file, "%sint f(int x){int r=0;\n", loops ? "" : "int g(int); ") >= 0;
        for (uint32_t index = 0; written && index < count; index += 1)
        {
            if (loops)
            {
                written = fprintf(file, "for(int i=0;i<x;i++){r+=i^%u;}\n", index) >= 0;
            }
            else
            {
                written = fprintf(file, "if(x>%u){int t%u=g(x);r+=t%u;}\n", index, index, index) >= 0;
            }
        }
        written = written && fprintf(file, "return r;}\n") >= 0;
    }
    if (file && fclose(file))
    {
        written = false;
    }
    return written;
}

static bool write_observer(char const* role, char const* source, char const* output)
{
    bool baseline = !strcmp(role, "baseline");
    bool written = baseline || !strcmp(role, "candidate");
    char const* allocation = baseline ?
        "result.debug_locations = options.debug_info ? arena_allocate(arena, DebugLocationSeed, debug_location_sink.capacity) : 0;" :
        "DebugLocationSeed* grown = arena_allocate(sink->arena, DebugLocationSeed, grown_capacity);";
    char const* publication = "result.statistics.code_bytes = result.code.length;";
    FILE* file = written ? fopen(source, "rb") : 0;
    written = written && file;
    uint32_t line = 1;
    uint32_t allocation_line = 0;
    uint32_t publication_line = 0;
    uint32_t allocation_matches = 0;
    uint32_t publication_matches = 0;
    if (file)
    {
        char buffer[8192];
        while (fgets(buffer, sizeof(buffer), file))
        {
            if (strstr(buffer, allocation))
            {
                allocation_line = line;
                allocation_matches += 1;
            }
            if (strstr(buffer, publication))
            {
                publication_line = line;
                publication_matches += 1;
            }
            for (uint32_t index = 0; buffer[index]; index += 1)
            {
                line += buffer[index] == '\n';
            }
        }
        written = written && !ferror(file);
        written = !fclose(file) && written;
    }
    written = written && allocation_matches == 1 && publication_matches == 1;
    FILE* observer = written ? fopen(output, "wb") : 0;
    written = written && observer;
    if (observer)
    {
        char const* capacity = baseline ? "debug_location_sink.capacity" : "grown_capacity";
        char const* count = baseline ? "result.debug_location_count" : "result->debug_location_count";
        char const* arena = baseline ? "arena" : "sink->arena";
        written = fprintf(observer,
            "set pagination off\nset confirm off\nset breakpoint pending off\n"
            "set $proof_calls = 0\nset $proof_request_sum = (unsigned long long)0\n"
            "break %s:%u\ncommands\nsilent\n"
            "set $proof_bytes = (unsigned long long)%s * sizeof(DebugLocationSeed)\n"
            "set $proof_calls = $proof_calls + 1\n"
            "set $proof_request_sum = $proof_request_sum + $proof_bytes\n"
            "printf \"DEBUG_LOCATION_REQUEST_V1 role=%s capacity=%%u seed_bytes=%%llu bytes=%%llu emitted_before=%%u arena_position=%%llu arena_reserved=%%llu\\n\", "
            "%s, (unsigned long long)sizeof(DebugLocationSeed), $proof_bytes, %s, "
            "(unsigned long long)%s->position, (unsigned long long)%s->reserved_size\n"
            "continue\nend\n"
            "break %s:%u\ncommands\nsilent\n"
            "printf \"DEBUG_LOCATION_FINAL_V1 role=%s capacity=%%u emitted=%%u request_calls=%%u array_request_sum=%%llu arena_position=%%llu arena_reserved=%%llu\\n\", "
            "debug_location_sink.capacity, result.debug_location_count, $proof_calls, $proof_request_sum, "
            "(unsigned long long)arena->position, (unsigned long long)arena->reserved_size\n"
            "continue\nend\nrun\n"
            "printf \"DEBUG_LOCATION_EXIT_V1 role=%s inferior_status=%%d request_calls=%%u array_request_sum=%%llu\\n\", $_exitcode, $proof_calls, $proof_request_sum\n",
            source, allocation_line, capacity, role, capacity, count, arena, arena,
            source, publication_line, role, role) >= 0;
    }
    if (observer && fclose(observer))
    {
        written = false;
    }
    if (written)
    {
        printf("DEBUG_LOCATION_OBSERVER_V1 role=%s allocation_line=%u publication_line=%u source=%s\n",
               role, allocation_line, publication_line, source);
    }
    return written;
}

int main(int argc, char** argv)
{
    bool completed = false;
    if (argc == 5 && !strcmp(argv[1], "source"))
    {
        uint32_t count = !strcmp(argv[3], "12000") ? 12000 : (!strcmp(argv[3], "40000") ? 40000 : 0);
        completed = count && write_source(argv[2], count, argv[4]);
    }
    else if (argc == 5 && !strcmp(argv[1], "gdb"))
    {
        completed = write_observer(argv[2], argv[3], argv[4]);
    }
    if (!completed)
    {
        fprintf(stderr, "usage: proof source loops|ifs 12000|40000 output; proof gdb baseline|candidate codegen.c output\n");
    }
    return completed ? 0 : 1;
}
