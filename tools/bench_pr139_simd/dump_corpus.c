// Reads each physical file through Buster's production lexer. Output records
// hold spelling bytes exactly as the decoder sees them, and token shapes.
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/os.h>
#include <buster/lib/system_headers.h>
#include <stdio.h>

OsState os_state;
static ProgramState dump_program;
ProgramState* program_state = &dump_program;

static bool write_u32(FILE* file, u32 value)
{
    u8 bytes[4] = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

int main(int argc, char** argv)
{
    int result = 1;
    if (argc == 4)
    {
        os_state.page_size = os_get_page_size();
        os_state.allocation_granularity = os_state.page_size;
        os_state.arena = arena_create((ArenaCreation){0});
        os_state.entity_arena = arena_create((ArenaCreation){0});
        pthread_mutex_init(&os_state.entity_mutex, 0);
        dump_program.arena = arena_create((ArenaCreation){0});
        ThreadContext* context = thread_context_allocate();
        thread_context_select(context);
        c_prewarm();
        FILE* input = fopen(argv[1], "rb");
        fseek(input, 0, SEEK_END);
        u64 length = (u64)ftell(input);
        fseek(input, 0, SEEK_SET);
        char8* source = malloc(length + 1);
        bool success = fread(source, 1, length, input) == length;
        fclose(input);
        source[length] = 0;
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(1), .initial_size = BUSTER_MB(32)});
        CLexResult lex = c_lex(arena, (String8){source, length});
        success = success && lex.diagnostic_count == 0 && lex.token_count <= UINT32_MAX;
        FILE* shapes = fopen(argv[2], "wb");
        success = success && write_u32(shapes, (u32)lex.token_count);
        success = success && fwrite(lex.token_shapes, 1, lex.token_count, shapes) == lex.token_count;
        fclose(shapes);
        FILE* literals = fopen(argv[3], "wb");
        u32 count = 0;
        for (u64 index = 0; index < lex.token_count; index += 1)
        {
            if (lex.tokens[index].kind == C_TOKEN_STRING_LITERAL)
            {
                String8 spelling = c_token_spelling(lex.spelling_base, lex.tokens[index]);
                if (spelling.pointer[0] == '"' || (spelling.length > 2 && spelling.pointer[0] == 'u' && spelling.pointer[1] == '8')) count += 1;
            }
        }
        success = success && write_u32(literals, count);
        for (u64 index = 0; index < lex.token_count; index += 1)
        {
            if (lex.tokens[index].kind == C_TOKEN_STRING_LITERAL)
            {
                String8 spelling = c_token_spelling(lex.spelling_base, lex.tokens[index]);
                if (spelling.pointer[0] == '"' || (spelling.length > 2 && spelling.pointer[0] == 'u' && spelling.pointer[1] == '8'))
                {
                    success = success && write_u32(literals, (u32)spelling.length);
                    success = success && fwrite(spelling.pointer, 1, spelling.length, literals) == spelling.length;
                }
            }
        }
        fclose(literals);
        printf("tokens=%llu literals=%u diagnostics=%llu\n", (unsigned long long)lex.token_count, count, (unsigned long long)lex.diagnostic_count);
        result = success ? 0 : 1;
    }
    return result;
}
