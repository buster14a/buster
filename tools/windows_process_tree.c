// Native Windows descendant capture for the bootstrap case scheduler (#3690).
//
// Owner: tools/bootstrap_wrapper_cases.py launches this helper before the
// direct child's `taskkill /T /F`, so descendant identities are retained as
// native handles before termination is requested (#2985).
// Entry points:
// - `capture <root-pid> <budget-ms>` (Windows): admit the root's snapshot
//   descendants, print one capture record, wait for a line or EOF on stdin
//   (the caller's termination request), then wait for every retained handle
//   within the budget measured from helper start and print one wait record.
// - `walk <root-pid> <root-created> <cutoff> <pid>:<parent>:<created>...`
//   (every host): run the same admission over a synthetic snapshot; `exited`
//   or `error` replaces a creation time. This is the portable control seam.
// Map: tree_admit walks parent edges; tree_open_* supply identities;
// tree_print_capture/tree_wait_print publish records.
//
// Windows keeps a dead parent's PID in th32ParentProcessID, and PIDs are
// reused. A link therefore proves descent only when the child is no older
// than the parent it names: an older process is excluded and published, and
// nothing beneath it is opened. A process created after the pre-snapshot
// cutoff is a PID replacement (or a late spawn) and fails capture, as does any
// open, time query or snapshot failure. An exited intermediate passes its
// nearest verified ancestor's creation time to its children, so ambiguity
// only adds waits. The root PID is stable: the caller holds an open handle to
// the unreaped root, so Windows cannot reuse that number during capture.
#include <buster/lib/base.h>
#include <buster/lib/system_headers.h>
#include <stdio.h>
#include <string.h>
#if BUSTER_WINDOWS
#include <tlhelp32.h>
#endif

#define TREE_PROCESS_CAPACITY 16384
#define TREE_IMAGE_CAPACITY 256
#define TREE_ACCESS_WAIT_QUERY 0x101000 // SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION.
#define TREE_WAIT_TIMEOUT 258
#define TREE_ERROR_INVALID_PARAMETER 87 // OpenProcess on an exited PID.

typedef enum TreeIdentity
{
    TREE_IDENTITY_OPENED,
    TREE_IDENTITY_EXITED,
    TREE_IDENTITY_ERROR,
} TreeIdentity;

typedef enum TreeFailure
{
    TREE_FAILURE_NONE,
    TREE_FAILURE_USAGE,
    TREE_FAILURE_CAPACITY,
    TREE_FAILURE_ROOT,
    TREE_FAILURE_SNAPSHOT,
    TREE_FAILURE_OPEN,
    TREE_FAILURE_REPLACEMENT,
    TREE_FAILURE_CLOSE,
} TreeFailure;

typedef enum TreeMark
{
    TREE_MARK_UNSEEN,
    TREE_MARK_CAPTURED,
    TREE_MARK_EXCLUDED,
    TREE_MARK_EXITED,
    TREE_MARK_ROOT,
    TREE_MARK_FAILED,
} TreeMark;

typedef struct Tree Tree;
struct Tree
{
    u32 pid[TREE_PROCESS_CAPACITY];
    u32 parent[TREE_PROCESS_CAPACITY];
    // Synthetic identities for `walk`; capture queries the live process.
    u64 synthetic_created[TREE_PROCESS_CAPACITY];
    u8 synthetic_identity[TREE_PROCESS_CAPACITY];
    u8 mark[TREE_PROCESS_CAPACITY];
    u64 created[TREE_PROCESS_CAPACITY];
    u64 bound[TREE_PROCESS_CAPACITY];
#if BUSTER_WINDOWS
    HANDLE handle[TREE_PROCESS_CAPACITY];
    u32 wait_result[TREE_PROCESS_CAPACITY];
    char image[TREE_PROCESS_CAPACITY][TREE_IMAGE_CAPACITY];
#endif
    // Each entry enters once after the root.
    u32 frontier_pid[TREE_PROCESS_CAPACITY + 1];
    u64 frontier_bound[TREE_PROCESS_CAPACITY + 1];
    u32 count;
    u32 root;
    u64 root_created;
    u64 cutoff;
    bool live;
    TreeFailure failure;
    u32 failure_pid;
    u32 failure_parent;
    u64 failure_created;
    u32 failure_code;
};

BUSTER_GLOBAL_LOCAL Tree tree;

BUSTER_GLOBAL_LOCAL bool tree_decimal(char const* text, u64* output)
{
    u64 length = strlen(text);
    bool result = length && length <= 20 && (length == 1 || text[0] != '0');
    u64 value = 0;
    for (u64 i = 0; result && i < length; i += 1)
    {
        u8 byte = (u8)text[i];
        result = byte >= '0' && byte <= '9' && value <= (UINT64_MAX - (u64)(byte - '0')) / 10;
        if (result)
        {
            value = value * 10 + (u64)(byte - '0');
        }
    }
    if (result)
    {
        *output = value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool tree_pid(char const* text, u32* output)
{
    u64 value = 0;
    bool result = tree_decimal(text, &value) && value <= UINT32_MAX;
    if (result)
    {
        *output = (u32)value;
    }
    return result;
}

// Parse `<pid>:<parent>:<created|exited|error>` without modifying argv.
BUSTER_GLOBAL_LOCAL bool tree_synthetic_entry(char const* text, u32 index)
{
    char field[3][24] = {{0}};
    u32 field_index = 0;
    u32 length = 0;
    bool result = true;
    for (char const* cursor = text; result && *cursor; cursor += 1)
    {
        if (*cursor == ':')
        {
            field_index += 1;
            length = 0;
            result = field_index < 3;
        }
        else
        {
            result = length + 1 < sizeof(field[0]);
            if (result)
            {
                field[field_index][length] = *cursor;
                length += 1;
            }
        }
    }
    result = result && field_index == 2 && tree_pid(field[0], &tree.pid[index]) && tree_pid(field[1], &tree.parent[index]);
    if (result)
    {
        if (strcmp(field[2], "exited") == 0)
        {
            tree.synthetic_identity[index] = TREE_IDENTITY_EXITED;
        }
        else if (strcmp(field[2], "error") == 0)
        {
            tree.synthetic_identity[index] = TREE_IDENTITY_ERROR;
        }
        else
        {
            tree.synthetic_identity[index] = TREE_IDENTITY_OPENED;
            result = tree_decimal(field[2], &tree.synthetic_created[index]);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void tree_fail(TreeFailure failure, u32 index, u32 code)
{
    if (tree.failure == TREE_FAILURE_NONE)
    {
        tree.failure = failure;
        tree.failure_pid = index < tree.count ? tree.pid[index] : tree.root;
        tree.failure_parent = index < tree.count ? tree.parent[index] : 0;
        tree.failure_created = index < tree.count ? tree.created[index] : 0;
        tree.failure_code = code;
    }
}

BUSTER_GLOBAL_LOCAL TreeIdentity tree_open_synthetic(u32 index)
{
    TreeIdentity result = (TreeIdentity)tree.synthetic_identity[index];
    if (result == TREE_IDENTITY_OPENED)
    {
        tree.created[index] = tree.synthetic_created[index];
    }
    else if (result == TREE_IDENTITY_ERROR)
    {
        tree_fail(TREE_FAILURE_OPEN, index, 5);
    }
    return result;
}

#if BUSTER_WINDOWS
BUSTER_GLOBAL_LOCAL u64 tree_filetime(FILETIME time)
{
    return ((u64)time.dwHighDateTime << 32) | time.dwLowDateTime;
}

BUSTER_GLOBAL_LOCAL bool tree_creation_time(HANDLE handle, u64* output)
{
    FILETIME created, exited, kernel_time, user_time;
    bool result = GetProcessTimes(handle, &created, &exited, &kernel_time, &user_time) != 0;
    if (result)
    {
        *output = tree_filetime(created);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL TreeIdentity tree_open_live(u32 index)
{
    // Query creation identity as well as waiting: a numeric snapshot PID can
    // be reused before OpenProcess acquires a native handle.
    TreeIdentity result = TREE_IDENTITY_ERROR;
    HANDLE handle = OpenProcess(TREE_ACCESS_WAIT_QUERY, FALSE, tree.pid[index]);
    if (handle)
    {
        if (tree_creation_time(handle, &tree.created[index]))
        {
            tree.handle[index] = handle;
            result = TREE_IDENTITY_OPENED;
        }
        else
        {
            tree_fail(TREE_FAILURE_OPEN, index, (u32)GetLastError());
            if (!CloseHandle(handle))
            {
                tree_fail(TREE_FAILURE_CLOSE, index, (u32)GetLastError());
            }
        }
    }
    else
    {
        u32 error = (u32)GetLastError();
        if (error == TREE_ERROR_INVALID_PARAMETER)
        {
            result = TREE_IDENTITY_EXITED;
        }
        else
        {
            tree_fail(TREE_FAILURE_OPEN, index, error);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void tree_release(u32 index)
{
    if (tree.handle[index])
    {
        if (!CloseHandle(tree.handle[index]))
        {
            tree_fail(TREE_FAILURE_CLOSE, index, (u32)GetLastError());
        }
        tree.handle[index] = 0;
    }
}

BUSTER_GLOBAL_LOCAL void tree_snapshot(void)
{
    HANDLE root = OpenProcess(TREE_ACCESS_WAIT_QUERY, FALSE, tree.root);
    if (!root)
    {
        tree_fail(TREE_FAILURE_ROOT, UINT32_MAX, (u32)GetLastError());
    }
    else
    {
        if (!tree_creation_time(root, &tree.root_created))
        {
            tree_fail(TREE_FAILURE_ROOT, UINT32_MAX, (u32)GetLastError());
        }
        if (!CloseHandle(root))
        {
            tree_fail(TREE_FAILURE_CLOSE, UINT32_MAX, (u32)GetLastError());
        }
    }
    if (tree.failure == TREE_FAILURE_NONE)
    {
        FILETIME cutoff;
        GetSystemTimePreciseAsFileTime(&cutoff);
        tree.cutoff = tree_filetime(cutoff);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            tree_fail(TREE_FAILURE_SNAPSHOT, UINT32_MAX, (u32)GetLastError());
        }
        else
        {
            PROCESSENTRY32W entry = {.dwSize = sizeof(entry)};
            BOOL present = Process32FirstW(snapshot, &entry);
            while (present && tree.failure == TREE_FAILURE_NONE)
            {
                if (tree.count == TREE_PROCESS_CAPACITY)
                {
                    tree_fail(TREE_FAILURE_CAPACITY, UINT32_MAX, 0);
                }
                else
                {
                    tree.pid[tree.count] = entry.th32ProcessID;
                    tree.parent[tree.count] = entry.th32ParentProcessID;
                    if (!WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, tree.image[tree.count], TREE_IMAGE_CAPACITY, 0, 0))
                    {
                        tree.image[tree.count][0] = 0;
                    }
                    tree.count += 1;
                    present = Process32NextW(snapshot, &entry);
                }
            }
            if (tree.failure == TREE_FAILURE_NONE && GetLastError() != ERROR_NO_MORE_FILES)
            {
                tree_fail(TREE_FAILURE_SNAPSHOT, UINT32_MAX, (u32)GetLastError());
            }
            if (!CloseHandle(snapshot))
            {
                tree_fail(TREE_FAILURE_CLOSE, UINT32_MAX, (u32)GetLastError());
            }
        }
    }
}
#endif

BUSTER_GLOBAL_LOCAL TreeIdentity tree_open(u32 index)
{
    TreeIdentity result;
#if BUSTER_WINDOWS
    if (tree.live)
    {
        result = tree_open_live(index);
    }
    else
    {
        result = tree_open_synthetic(index);
    }
#else
    result = tree_open_synthetic(index);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL void tree_admit(void)
{
    u32 frontier = 1;
    tree.frontier_pid[0] = tree.root;
    tree.frontier_bound[0] = tree.root_created;
    for (u32 i = 0; i < tree.count; i += 1)
    {
        if (tree.pid[i] == tree.root)
        {
            tree.mark[i] = TREE_MARK_ROOT; // The caller owns the root's handle.
        }
    }
    while (frontier && tree.failure == TREE_FAILURE_NONE)
    {
        frontier -= 1;
        u32 parent = tree.frontier_pid[frontier];
        u64 bound = tree.frontier_bound[frontier];
        for (u32 i = 0; i < tree.count && tree.failure == TREE_FAILURE_NONE; i += 1)
        {
            if (tree.mark[i] == TREE_MARK_UNSEEN && tree.parent[i] == parent && tree.pid[i] != parent)
            {
                TreeIdentity identity = tree_open(i);
                tree.bound[i] = bound;
                switch (identity)
                {
                    case TREE_IDENTITY_EXITED:
                    {
                        tree.mark[i] = TREE_MARK_EXITED;
                        tree.frontier_pid[frontier] = tree.pid[i];
                        tree.frontier_bound[frontier] = bound;
                        frontier += 1;
                    } break;
                    case TREE_IDENTITY_OPENED:
                    {
                        if (tree.created[i] < bound)
                        {
                            // Stale parent link: provably not that parent's child.
                            tree.mark[i] = TREE_MARK_EXCLUDED;
#if BUSTER_WINDOWS
                            tree_release(i);
#endif
                        }
                        else if (tree.created[i] > tree.cutoff)
                        {
                            tree.mark[i] = TREE_MARK_EXCLUDED;
                            tree_fail(TREE_FAILURE_REPLACEMENT, i, 0);
#if BUSTER_WINDOWS
                            tree_release(i);
#endif
                        }
                        else
                        {
                            tree.mark[i] = TREE_MARK_CAPTURED;
                            tree.frontier_pid[frontier] = tree.pid[i];
                            tree.frontier_bound[frontier] = tree.created[i];
                            frontier += 1;
                        }
                    } break;
                    case TREE_IDENTITY_ERROR:
                    {
                        tree.mark[i] = TREE_MARK_FAILED;
                    } break;
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void tree_print_image(u32 index)
{
#if BUSTER_WINDOWS
    printf(",\"image\":\"");
    for (char const* cursor = tree.live ? tree.image[index] : ""; *cursor; cursor += 1)
    {
        u8 byte = (u8)*cursor;
        if (byte == '"' || byte == '\\')
        {
            printf("\\%c", byte);
        }
        else if (byte < 0x20)
        {
            printf("\\u%04x", byte);
        }
        else
        {
            putchar(byte);
        }
    }
    putchar('"');
#else
    (void)index;
#endif
}

BUSTER_GLOBAL_LOCAL void tree_print_capture(void)
{
    static char const* const failure_names[] = {
        [TREE_FAILURE_NONE] = "none",
        [TREE_FAILURE_USAGE] = "usage",
        [TREE_FAILURE_CAPACITY] = "capacity",
        [TREE_FAILURE_ROOT] = "root",
        [TREE_FAILURE_SNAPSHOT] = "snapshot",
        [TREE_FAILURE_OPEN] = "open",
        [TREE_FAILURE_REPLACEMENT] = "replacement",
        [TREE_FAILURE_CLOSE] = "close",
    };
    printf("{\"root\":%u,\"root_created\":%llu,\"cutoff\":%llu", tree.root,
           (unsigned long long)tree.root_created, (unsigned long long)tree.cutoff);
    if (tree.failure != TREE_FAILURE_NONE)
    {
        printf(",\"failure\":\"%s\",\"pid\":%u,\"parent\":%u,\"created\":%llu,\"code\":%u",
               failure_names[tree.failure], tree.failure_pid, tree.failure_parent,
               (unsigned long long)tree.failure_created, tree.failure_code);
    }
    char const* const lists[] = {"captured", "excluded", "exited"};
    u8 const marks[] = {TREE_MARK_CAPTURED, TREE_MARK_EXCLUDED, TREE_MARK_EXITED};
    for (u32 list = 0; list < BUSTER_ARRAY_LENGTH(lists); list += 1)
    {
        char const* separator = "";
        printf(",\"%s\":[", lists[list]);
        for (u32 i = 0; i < tree.count; i += 1)
        {
            bool replacement = tree.failure == TREE_FAILURE_REPLACEMENT && tree.pid[i] == tree.failure_pid;
            if (tree.mark[i] == marks[list] && !replacement)
            {
                printf("%s{\"pid\":%u,\"parent\":%u", separator, tree.pid[i], tree.parent[i]);
                if (marks[list] != TREE_MARK_EXITED)
                {
                    printf(",\"created\":%llu,\"parent_created\":%llu", (unsigned long long)tree.created[i],
                           (unsigned long long)tree.bound[i]);
                }
                tree_print_image(i);
                putchar('}');
                separator = ",";
            }
        }
        putchar(']');
    }
    printf("}\n");
    fflush(stdout);
}

#if BUSTER_WINDOWS
// Shared deadline: every retained handle waits within the original budget.
BUSTER_GLOBAL_LOCAL bool tree_wait_print(u64 deadline)
{
    char const* pending_separator = "";
    char const* failed_separator = "";
    bool result = true;
    printf("{\"pending\":[");
    for (u32 i = 0; i < tree.count; i += 1)
    {
        if (tree.handle[i])
        {
            u64 now = GetTickCount64();
            DWORD wait = WaitForSingleObject(tree.handle[i], now < deadline ? (DWORD)(deadline - now) : 0);
            if (wait == TREE_WAIT_TIMEOUT)
            {
                printf("%s%u", pending_separator, tree.pid[i]);
                pending_separator = ",";
                result = false;
            }
            else if (wait != WAIT_OBJECT_0)
            {
                // WAIT_OBJECT_0 alone certifies termination; keep the error code.
                tree.wait_result[i] = wait == WAIT_FAILED ? (u32)GetLastError() : (u32)wait;
                tree.mark[i] = TREE_MARK_FAILED;
                result = false;
            }
        }
    }
    printf("],\"failed\":[");
    for (u32 i = 0; i < tree.count; i += 1)
    {
        if (tree.handle[i] && tree.mark[i] == TREE_MARK_FAILED)
        {
            printf("%s[%u,%u]", failed_separator, tree.pid[i], tree.wait_result[i]);
            failed_separator = ",";
        }
    }
    printf("],\"close_failed\":[");
    char const* close_separator = "";
    for (u32 i = 0; i < tree.count; i += 1)
    {
        if (tree.handle[i] && !CloseHandle(tree.handle[i]))
        {
            printf("%s%u", close_separator, tree.pid[i]);
            close_separator = ",";
            result = false;
        }
        tree.handle[i] = 0;
    }
    printf("]}\n");
    fflush(stdout);
    return result;
}
#endif

int main(int argc, char** argv)
{
    int result = 2;
    u64 budget = 0;
    bool capture = argc == 4 && strcmp(argv[1], "capture") == 0 && tree_pid(argv[2], &tree.root) && tree_decimal(argv[3], &budget);
    bool walk = argc >= 5 && strcmp(argv[1], "walk") == 0 && (u64)(argc - 5) < TREE_PROCESS_CAPACITY &&
        tree_pid(argv[2], &tree.root) && tree_decimal(argv[3], &tree.root_created) && tree_decimal(argv[4], &tree.cutoff);
    if (walk)
    {
        for (int i = 5; walk && i < argc; i += 1)
        {
            walk = tree_synthetic_entry(argv[i], tree.count);
            tree.count += 1;
        }
    }
#if BUSTER_WINDOWS
    if (capture)
    {
        u64 deadline = GetTickCount64() + budget;
        tree.live = true;
        tree_snapshot();
        if (tree.failure == TREE_FAILURE_NONE)
        {
            tree_admit();
        }
        if (tree.failure != TREE_FAILURE_NONE)
        {
            for (u32 i = 0; i < tree.count; i += 1)
            {
                tree_release(i);
            }
        }
        tree_print_capture();
        if (tree.failure == TREE_FAILURE_NONE)
        {
            // Hold the handles while the caller requests termination.
            int byte = getchar();
            while (byte != EOF && byte != '\n')
            {
                byte = getchar();
            }
            result = tree_wait_print(deadline) ? 0 : 1;
        }
    }
    else
#else
    (void)budget;
    if (capture)
    {
        fprintf(stderr, "windows_process_tree: capture requires Windows\n");
    }
    else
#endif
    if (walk)
    {
        tree_admit();
        tree_print_capture();
        result = tree.failure == TREE_FAILURE_NONE ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: windows_process_tree capture <root-pid> <budget-ms> | "
                        "walk <root-pid> <root-created> <cutoff> <pid>:<parent>:<created|exited|error>...\n");
    }
    return result;
}
