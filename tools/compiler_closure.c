// Attempt-local frozen baseline closure, included by the trusted build.c.
// Entry: compiler_closure_main. snapshot parks build/ and the immutable
// bootstrap cache; restore returns both to their original absolute paths.
// inventory binds every source/generated/configuration/driver file, modes,
// timestamps and resolved producer tools. No compiler generation or Ninja
// invocation occurs on restore. Unsupported state fails without a fallback.
// The existing compiler comparison owns admission, subprocess cancellation
// and attempt-root cleanup; .complete is published only after both moves.
//
// Linux is the approved measurement platform. Other driver builds retain an
// explicit unsupported result rather than an unverified filesystem recipe.
#define BUSTER_COMPILER_CLOSURE_SCHEMA "buster-compiler-closure-v1"
#define BUSTER_COMPILER_CLOSURE_RECORD_LIMIT 65536ull
#define BUSTER_COMPILER_CLOSURE_BYTE_LIMIT (8ull << 30)
#define BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT (8ull << 20)

#if BUSTER_LINUX
typedef struct CompilerClosureInventory CompilerClosureInventory;
struct CompilerClosureInventory
{
    Arena* arena;
    String8List lines;
    u64 records;
    u64 bytes;
    bool success;
};

BUSTER_GLOBAL_LOCAL bool compiler_closure_path_safe(String8 path)
{
    bool result = path.length != 0;
    for (u64 index = 0; result && index < path.length; index += 1)
    {
        char8 byte = path.pointer[index];
        result = byte != '\t' && byte != '\n' && byte != '\r' && byte != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_hash(Arena* arena, String8 path, String8* digest, struct stat* stats)
{
    String8 terminated = string_duplicate_arena(arena, path, true);
    struct stat before = {0};
    struct stat opened = {0};
    struct stat after = {0};
    bool success = lstat((char*)terminated.pointer, &before) == 0 && S_ISREG(before.st_mode);
    int descriptor = success ? open((char*)terminated.pointer, O_RDONLY | O_NOFOLLOW) : -1;
    success = success && descriptor >= 0 && fstat(descriptor, &opened) == 0 &&
        before.st_dev == opened.st_dev && before.st_ino == opened.st_ino && before.st_size == opened.st_size;
    Sha256 hash;
    sha256_init(&hash);
    u8 buffer[65536];
    u64 bytes = 0;
    while (success)
    {
        ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count == 0) { break; }
        if (count < 0)
        {
            if (errno != EINTR) { success = false; }
        }
        else
        {
            bytes += (u64)count;
            success = bytes <= BUSTER_COMPILER_CLOSURE_BYTE_LIMIT;
            if (success) { sha256_add(&hash, buffer, (u64)count); }
        }
    }
    success = success && fstat(descriptor, &after) == 0 && bytes == (u64)before.st_size &&
        before.st_size == after.st_size && before.st_mode == after.st_mode &&
        before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec;
    if (descriptor >= 0 && close(descriptor) != 0) { success = false; }
    if (success)
    {
        char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
        sha256_finish_hex(&hash, digits);
        *digest = (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
        *stats = before;
    }
    return success;
}

BUSTER_GLOBAL_LOCAL void compiler_closure_record(CompilerClosureInventory* inventory, String8 scope, String8 relative, String8 path)
{
    struct stat status = {0};
    String8 digest = S8("-");
    bool directory = false;
    String8 terminated = string_duplicate_arena(inventory->arena, path, true);
    inventory->success = inventory->success && compiler_closure_path_safe(relative) &&
        inventory->records < BUSTER_COMPILER_CLOSURE_RECORD_LIMIT && lstat((char*)terminated.pointer, &status) == 0;
    if (inventory->success)
    {
        directory = S_ISDIR(status.st_mode);
        inventory->success = directory || compiler_closure_hash(inventory->arena, path, &digest, &status);
    }
    if (inventory->success && !directory)
    {
        inventory->success = status.st_size >= 0 && (u64)status.st_size <= BUSTER_COMPILER_CLOSURE_BYTE_LIMIT - inventory->bytes;
        if (inventory->success) { inventory->bytes += (u64)status.st_size; }
    }
    if (inventory->success)
    {
        inventory->records += 1;
        string8_list_push(inventory->arena, &inventory->lines,
            string_format(inventory->arena, S8("{S8}\t{S8}\t{u64}\t{u64}\t{u64}\t{u64}\t{S8}\t{S8}\n"),
                scope, directory ? S8("D") : S8("F"), (u64)(status.st_mode & 07777),
                directory ? 0ull : (u64)status.st_mtim.tv_sec,
                directory ? 0ull : (u64)status.st_mtim.tv_nsec,
                directory ? 0ull : (u64)status.st_size, digest, relative));
    }
}

BUSTER_GLOBAL_LOCAL void compiler_closure_walk(CompilerClosureInventory* inventory, String8 scope, String8 root, bool source)
{
    String8List pending = {0};
    string8_list_push(inventory->arena, &pending, S8(""));
    for (String8Node* node = pending.first; inventory->success && node; node = node->next)
    {
        String8 directory = path_join(inventory->arena, root, node->string);
        MuslDirectoryEntry* entries = 0;
        u64 count = 0;
        inventory->success = musl_list_directory(inventory->arena, directory, &entries, &count) &&
            count <= BUSTER_COMPILER_CLOSURE_RECORD_LIMIT - inventory->records;
        // Deterministic insertion sort; individual directory inventories are
        // small and this avoids callback dispatch in the build driver.
        for (u64 index = 1; inventory->success && index < count; index += 1)
        {
            MuslDirectoryEntry entry = entries[index];
            u64 at = index;
            while (at && assembly_import_string_compare(&entry.name, &entries[at - 1].name) < 0)
            {
                entries[at] = entries[at - 1];
                at -= 1;
            }
            entries[at] = entry;
        }
        for (u64 index = 0; inventory->success && index < count; index += 1)
        {
            String8 name = entries[index].name;
            bool excluded = source && !node->string.length &&
                (string_equal(name, S8(".git")) || string_equal(name, S8("build")) || string_equal(name, S8(".cache")));
            if (!excluded)
            {
                String8 relative = node->string.length ? path_join(inventory->arena, node->string, name) : name;
                compiler_closure_record(inventory, scope, relative, path_join(inventory->arena, root, relative));
                if (inventory->success && entries[index].is_directory) { string8_list_push(inventory->arena, &pending, relative); }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL ProductionProfileCommandResult compiler_closure_git(Arena* arena, String8 root, String8 argument)
{
    ProductionProfileContext context = {.arena = arena};
    String8 arguments[] = {S8("git"), S8("-C"), root, S8("rev-parse"), S8("--verify"), argument};
    return production_profile_capture(&context, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments));
}

BUSTER_GLOBAL_LOCAL String8 compiler_closure_inventory(Arena* arena, String8 root, String8 build, String8 bootstrap, String8 base, String8 tree)
{
    CompilerClosureInventory inventory = {.arena = arena, .success = true};
    ProductionProfileCommandResult revision = compiler_closure_git(arena, root, S8("HEAD"));
    ProductionProfileCommandResult source_tree = compiler_closure_git(arena, root, S8("HEAD^{tree}"));
    ProductionProfileContext context = {.arena = arena};
    String8 clean_arguments[] = {S8("git"), S8("-C"), root, S8("diff"), S8("--quiet"), S8("--exit-code"), S8("HEAD"), S8("--")};
    ProductionProfileCommandResult clean = production_profile_capture(&context, (SliceString8)BUSTER_ARRAY_TO_SLICE(clean_arguments));
    inventory.success = clean.success && revision.success && source_tree.success &&
        string_equal(production_profile_trim(revision.output), base) && string_equal(production_profile_trim(source_tree.output), tree);
    string8_list_push(arena, &inventory.lines, string_format(arena, S8("BUSTER_COMPILER_CLOSURE_V1\nroot\t{S8}\nbase\t{S8}\ntree\t{S8}\n"),
        root, base, tree));
    compiler_closure_walk(&inventory, S8("source"), root, true);
    compiler_closure_walk(&inventory, S8("build"), build, false);
    compiler_closure_walk(&inventory, S8("bootstrap"), bootstrap, false);
    String8 tools[] = {S8("clang"), S8("cmake"), S8("ninja"), S8("tcc"), S8("ld"), S8("ld.lld"), S8("mold")};
    for (u64 index = 0; inventory.success && index < BUSTER_ARRAY_LENGTH(tools); index += 1)
    {
        String8 tool = executable_resolve_in_path(arena, tools[index]);
        if (tool.length)
        {
            tool = os_path_absolute(arena, tool, true);
            compiler_closure_record(&inventory, S8("tool"), tool, tool);
        }
        else if (index < 4) { inventory.success = false; }
    }
    if (inventory.success)
    {
        ProductionProfileContext context = {.arena = arena};
        String8 command[] = {S8("clang"), S8("-print-resource-dir")};
        ProductionProfileCommandResult resource = production_profile_capture(&context, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        String8 directory = resource.success ? os_path_absolute(arena, production_profile_trim(resource.output), true) : (String8){0};
        inventory.success = directory.length != 0;
        if (inventory.success) { compiler_closure_walk(&inventory, S8("resource"), directory, false); }
    }
    String8 result = {0};
    if (inventory.success)
    {
        string8_list_push(arena, &inventory.lines, string_format(arena, S8("END\t{u64}\t{u64}\n"), inventory.records, inventory.bytes));
        result = string_join_arena(arena, string8_list_to_slice(arena, inventory.lines), false);
        if (result.length > BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT) { result = (String8){0}; }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_timestamps(Arena* arena, String8 root, String8 manifest)
{
    bool result = true;
    u64 line_start = 0;
    for (u64 index = 0; result && index <= manifest.length; index += 1)
    {
        if (index == manifest.length || manifest.pointer[index] == '\n')
        {
            String8 line = {.pointer = manifest.pointer + line_start, .length = index - line_start};
            line_start = index + 1;
            if (string_starts_with_sequence(line, S8("source\tF\t")))
            {
                String8 fields[8] = {0};
                u64 field = 0;
                u64 start = 0;
                for (u64 at = 0; at <= line.length && field < BUSTER_ARRAY_LENGTH(fields); at += 1)
                {
                    if (at == line.length || line.pointer[at] == '\t')
                    {
                        fields[field++] = (String8){.pointer = line.pointer + start, .length = at - start};
                        start = at + 1;
                    }
                }
                u64 seconds = 0;
                u64 nanos = 0;
                result = field == BUSTER_ARRAY_LENGTH(fields) && production_profile_path_components_safe(fields[7]) &&
                    fields[7].pointer[0] != '/' && production_profile_u64(fields[3], &seconds) &&
                    production_profile_u64(fields[4], &nanos) && nanos < 1000000000ull;
                if (result)
                {
                    String8 path = path_join(arena, root, fields[7]);
                    struct timespec times[2] = {{.tv_nsec = UTIME_OMIT}, {.tv_sec = (time_t)seconds, .tv_nsec = (long)nanos}};
                    result = utimensat(AT_FDCWD, (char*)path.pointer, times, AT_SYMLINK_NOFOLLOW) == 0;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_cache_valid(Arena* arena, String8 root)
{
    String8 cache = path_join(arena, root, S8("build/CMakeCache.txt"));
    ByteSlice bytes = file_read(arena, cache, (FileReadOptions){0});
    String8 text = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
    String8 home = string_format(arena, S8("CMAKE_HOME_DIRECTORY:INTERNAL={S8}\n"), root);
    String8 ide = path_join(arena, root, S8("build/Release/ide"));
    String8 digest = {0};
    struct stat status = {0};
    bool result = text.length && production_profile_contains(text, S8("BUSTER_INCLUDE_TESTS:BOOL=OFF\n")) &&
        production_profile_contains(text, home) && compiler_closure_hash(arena, ide, &digest, &status) && (status.st_mode & 0111);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_transfer(Arena* arena, String8 operation, String8 root, String8 snapshot,
    String8 base, String8 tree, String8 receipt_path, String8 expected_digest)
{
    bool snapshot_operation = string_equal(operation, S8("snapshot"));
    bool restore = string_equal(operation, S8("restore"));
    bool verify = string_equal(operation, S8("verify"));
    String8 build = path_join(arena, root, S8("build"));
    String8 bootstrap = path_join(arena, root, S8(".cache/bootstrap-driver"));
    String8 saved_build = path_join(arena, snapshot, S8("build"));
    String8 saved_bootstrap = path_join(arena, snapshot, S8("bootstrap"));
    String8 manifest_path = path_join(arena, snapshot, S8("manifest.tsv"));
    String8 marker_path = path_join(arena, snapshot, S8(".complete"));
    String8 manifest = {0};
    String8 digest = {0};
    u64 start = os_now_microseconds();
    bool success = snapshot_operation || restore || verify;
    if (success && snapshot_operation)
    {
        success = compiler_closure_cache_valid(arena, root) && !path_exists(arena, snapshot);
        if (success) { manifest = compiler_closure_inventory(arena, root, build, bootstrap, base, tree); }
        success = success && manifest.length && os_make_directory_exclusive(snapshot).created;
        if (success)
        {
            digest = production_profile_sha256_text(arena, manifest);
            success = production_profile_write(manifest_path, manifest) &&
                os_file_replace(build, saved_build).v == 0 && os_file_replace(bootstrap, saved_bootstrap).v == 0 &&
                string_equal(manifest, compiler_closure_inventory(arena, root, saved_build, saved_bootstrap, base, tree)) &&
                production_profile_write(marker_path, digest);
        }
    }
    else if (success)
    {
        ByteSlice bytes = file_read(arena, manifest_path, (FileReadOptions){0});
        manifest = (String8){.pointer = (char8*)bytes.pointer, .length = bytes.length};
        ByteSlice marker_bytes = file_read(arena, marker_path, (FileReadOptions){0});
        String8 marker = {.pointer = (char8*)marker_bytes.pointer, .length = marker_bytes.length};
        digest = production_profile_sha256_text(arena, manifest);
        success = manifest.length && manifest.length <= BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT &&
            stage_object_sha256_valid(expected_digest) && string_equal(digest, expected_digest) && string_equal(marker, digest);
        if (success && restore)
        {
            success = compiler_closure_timestamps(arena, root, manifest) &&
                string_equal(manifest, compiler_closure_inventory(arena, root, saved_build, saved_bootstrap, base, tree));
            if (success)
            {
                // Source/root/parked bytes and tools are verified before the
                // only destructive operations. Candidate cache entries cannot
                // survive to select a candidate driver for baseline workloads.
                success = remove_path_recursive(arena, build) && remove_path_recursive(arena, bootstrap) &&
                    os_file_replace(saved_build, build).v == 0 && os_file_replace(saved_bootstrap, bootstrap).v == 0;
            }
        }
        if (success)
        {
            success = compiler_closure_cache_valid(arena, root) &&
                string_equal(manifest, compiler_closure_inventory(arena, root, build, bootstrap, base, tree));
        }
    }
    if (success)
    {
        String8 receipt = string_format(arena, S8("{\"schema\":\"" BUSTER_COMPILER_CLOSURE_SCHEMA "\",\"policy\":\"snapshot-v1\","
            "\"state\":\"{S8}\",\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"root_sha256\":\"{S8}\","
            "\"manifest_sha256\":\"{S8}\",\"duration_us\":{u64}\n}\n"),
            operation, base, tree, production_profile_sha256_text(arena, root), digest,
            os_now_microseconds() - start);
        success = production_profile_write(string_format(arena, S8("{S8}.manifest.tsv"), receipt_path), manifest) &&
            production_profile_write(receipt_path, receipt);
    }
    string_print(S8("COMPILER_CLOSURE operation={S8} state={S8} same_root=1 compiler_rebuilds=0\n"), operation,
        success ? S8("complete") : S8("failed"));
    return success;
}
#endif

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX
    if (arguments.length == 7)
    {
        String8 root = os_path_absolute(arena, arguments.pointer[1], true);
        String8 snapshot = os_path_absolute_lexical(arena, arguments.pointer[2], true);
        String8 parent = path_parent(arena, snapshot);
        String8 real_parent = os_path_absolute(arena, parent, true);
        bool safe = root.length && snapshot.length && real_parent.length && string_equal(parent, real_parent) &&
            compiler_closure_path_safe(root) && compiler_closure_path_safe(snapshot) &&
            production_profile_path_components_safe(snapshot) && !production_profile_path_is_child(root, snapshot) &&
            !production_profile_path_is_child(snapshot, root) && !string_equal(root, snapshot) &&
            arguments.pointer[3].length == 40 && arguments.pointer[4].length == 40;
        if (safe && compiler_closure_transfer(arena, arguments.pointer[0], root, snapshot, arguments.pointer[3],
            arguments.pointer[4], arguments.pointer[5], arguments.pointer[6]))
        {
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    else { string_print(S8("usage: compiler_closure snapshot|restore|verify ROOT SNAPSHOT BASE TREE RECEIPT EXPECTED_SHA256\n")); }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    string_print(S8("error: compiler_closure requires the approved Linux filesystem recipe\n"));
#endif
    return result;
}
