#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#include <buster/lib/base.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/arena.c>
#include <buster/lib/file.c>
#include <buster/lib/integer.c>
#include <stdio.h>
BUSTER_GLOBAL_LOCAL void audit_initialize(void)
{
    static ProgramState state;
    program_state = &state;
    os_state.page_size = (u64)sysconf(_SC_PAGESIZE);
    os_state.allocation_granularity = os_state.page_size;
    os_state.logical_thread_count = 1;
    pthread_mutex_init(&os_state.entity_mutex, 0);
    os_state.entity_arena = arena_create((ArenaCreation){0});
    state.arena = arena_create((ArenaCreation){0});
    thread_context_select(thread_context_allocate());
}

int main(int argc, char** argv)
{
    audit_initialize();
    bool ok = true;
    if(argc==2 && strcmp(argv[1],"scratch")==0)
    {
        String8 keys[]={S8("PATH")}, vals[]={S8("/bin:/usr/bin")};
        program_state->input.environment_keys=(SliceString8){keys,1};
        program_state->input.environment_values=(SliceString8){vals,1};
        for (u32 index = 0; index < 2; index += 1)
        {
            Arena* output = thread_context_selected()->arenas[index];
            u64 before = output->position;
            String8 name = executable_resolve_in_path(output, S8("sh"));
            char expected[64] = {0};
            bool valid = name.length && name.length < sizeof(expected);
            if (valid)
            {
                memcpy(expected, name.pointer, name.length);
            }
            printf("scratch=%u before=%llu after=%llu returned=%.*s\n", index,
                   (unsigned long long)before, (unsigned long long)output->position,
                   (int)name.length, name.pointer);
            memset(arena_allocate(output, char8, 128), 'X', 128);
            valid = valid && memcmp(name.pointer, expected, name.length) == 0;
            printf("output survives next allocation: %s\n", valid ? "yes" : "NO");
            ok = ok && valid;
        }
    }
    else if(argc==2 && strcmp(argv[1],"map")==0)
    {
        char dir[]="/tmp/buster-audit-XXXXXX";
        ok=mkdtemp(dir)!=0;
        char old[4096]; ok=ok && getcwd(old,sizeof(old))!=0;
        if(ok)
        {
            ok=chdir(dir)==0;
            String8 path=S8("relative.bin");
            ok=ok && file_write(path,(ByteSlice){(u8*)"hello",5});
            FileMapRead map=file_map_read(program_state->arena,path,(FileReadOptions){.map_required=1});
            ok=ok && map.mapped_pointer && map.bytes.length==5;
            if(map.bytes.pointer)ok=ok && memcmp(map.bytes.pointer,"hello",5)==0;
            printf("relative required mapping: %s\n",ok?"success":"FAILED");
            file_map_unmap(map); unlink("relative.bin"); chdir(old);rmdir(dir);
        }
    }
    else if(argc==2 && strcmp(argv[1],"empty")==0)
    {
        String8 copy=string_duplicate_arena(program_state->arena,(String8){0},true);
        String8 pieces[]={(String8){0},S8("ok"),(String8){0}};
        String8 join=string_join_arena(program_state->arena,(SliceString8){pieces,3},true);
        ok=copy.length==0 && copy.pointer[0]==0 && join.length==2 && memcmp(join.pointer,"ok\0",3)==0;
        puts("empty string operations completed");
    }
    else ok=false;
    return !ok;
}
