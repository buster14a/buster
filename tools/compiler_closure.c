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
    bool success = compiler_closure_admitting() && lstat((char*)terminated.pointer, &before) == 0 && S_ISREG(before.st_mode);
    int descriptor = success ? open((char*)terminated.pointer, O_RDONLY | O_NOFOLLOW) : -1;
    success = success && descriptor >= 0 && fstat(descriptor, &opened) == 0 &&
        before.st_dev == opened.st_dev && before.st_ino == opened.st_ino && before.st_size == opened.st_size;
    Sha256 hash;
    sha256_init(&hash);
    u8 buffer[65536];
    u64 bytes = 0;
    while (success && compiler_closure_admitting())
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
    success = success && compiler_closure_admitting() && fstat(descriptor, &after) == 0 && bytes == (u64)before.st_size &&
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
        inventory->success = directory || (S_ISREG(status.st_mode) && status.st_size >= 0 &&
            (u64)status.st_size <= BUSTER_COMPILER_CLOSURE_BYTE_LIMIT - inventory->bytes &&
            compiler_closure_hash(inventory->arena, path, &digest, &status));
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

// Bounded, nofollow enumeration. Do not use the general musl inventory:
// its allocations precede the caller's cap and a hostile directory could
// exhaust memory before this closure rejected it.
BUSTER_GLOBAL_LOCAL bool compiler_closure_list(Arena* arena, String8 directory, u64 limit, MuslDirectoryEntry** output, u64* count_out)
{
    String8 terminated = string_duplicate_arena(arena, directory, true);
    DIR* handle = opendir((char*)terminated.pointer);
    u64 capacity = BUSTER_MIN(limit, 32ull);
    MuslDirectoryEntry* entries = capacity ? arena_allocate(arena, MuslDirectoryEntry, capacity) : 0;
    u64 count = 0;
    bool success = handle != 0;
    while (success)
    {
        errno = 0;
        struct dirent* entry = readdir(handle);
        if (!entry) { success = errno == 0; break; }
        String8 name = string_from_pointer((char8*)entry->d_name);
        if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
        {
            success = count < limit;
            if (success && count == capacity)
            {
                u64 grown_capacity = BUSTER_MIN(capacity * 2, limit);
                MuslDirectoryEntry* grown = arena_allocate(arena, MuslDirectoryEntry, grown_capacity);
                memcpy(grown, entries, count * sizeof(*entries));
                entries = grown;
                capacity = grown_capacity;
            }
            if (success)
            {
                String8 path = path_join(arena, directory, name);
                struct stat status = {0};
                success = lstat((char*)path.pointer, &status) == 0 && (S_ISREG(status.st_mode) || S_ISDIR(status.st_mode));
                if (success)
                {
                    entries[count++] = (MuslDirectoryEntry){.name = string_duplicate_arena(arena, name, true),
                        .is_directory = S_ISDIR(status.st_mode)};
                }
            }
        }
    }
    if (handle && closedir(handle) != 0) { success = false; }
    *output = success ? entries : 0;
    *count_out = success ? count : 0;
    return success;
}

BUSTER_GLOBAL_LOCAL void compiler_closure_sift(MuslDirectoryEntry* entries, u64 root, u64 count)
{
    while (root < count / 2)
    {
        u64 child = root * 2 + 1;
        if (child + 1 < count && assembly_import_string_compare(&entries[child].name, &entries[child + 1].name) < 0) { child += 1; }
        if (assembly_import_string_compare(&entries[root].name, &entries[child].name) >= 0) { break; }
        MuslDirectoryEntry swap = entries[root];
        entries[root] = entries[child];
        entries[child] = swap;
        root = child;
    }
}

BUSTER_GLOBAL_LOCAL void compiler_closure_sort(MuslDirectoryEntry* entries, u64 count)
{
    for (u64 start = count / 2; start; start -= 1) { compiler_closure_sift(entries, start - 1, count); }
    for (u64 end = count; end > 1; end -= 1)
    {
        MuslDirectoryEntry swap = entries[0];
        entries[0] = entries[end - 1];
        entries[end - 1] = swap;
        compiler_closure_sift(entries, 0, end - 1);
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
        inventory->success = compiler_closure_list(inventory->arena, directory,
            BUSTER_COMPILER_CLOSURE_RECORD_LIMIT - inventory->records, &entries, &count);
        if (inventory->success) { compiler_closure_sort(entries, count); }
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

// Keep children in the comparison's owning process group. The trusted
// outer runner kills and reaps that entire group on timeout/cancellation;
// creating a detached inner group would let a preparation child escape it.
BUSTER_GLOBAL_LOCAL ProductionProfileCommandResult compiler_closure_capture(Arena* arena, SliceString8 arguments)
{
    CompilerClosurePhaseResult phase = compiler_closure_phase_run(arena, arguments, (String8){0}, 300ull * 1000000, false);
    ProductionProfileCommandResult result = {.wait = phase.wait, .success = phase.success};
    result.output = (String8){.pointer = (char8*)result.wait.streams[STANDARD_STREAM_OUTPUT].pointer,
        .length = result.wait.streams[STANDARD_STREAM_OUTPUT].length};
    result.error = (String8){.pointer = (char8*)result.wait.streams[STANDARD_STREAM_ERROR].pointer,
        .length = result.wait.streams[STANDARD_STREAM_ERROR].length};
    return result;
}

BUSTER_GLOBAL_LOCAL ProductionProfileCommandResult compiler_closure_git(Arena* arena, String8 root, String8 argument)
{
    String8 arguments[] = {S8("git"), S8("-C"), root, S8("rev-parse"), S8("--verify"), argument};
    return compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments));
}

BUSTER_GLOBAL_LOCAL String8 compiler_closure_read(Arena* arena, String8 path, u64 limit)
{
    String8 result = {0};
    String8 terminated = string_duplicate_arena(arena, path, true);
    int descriptor = open((char*)terminated.pointer, O_RDONLY | O_NOFOLLOW);
    struct stat status = {0};
    bool success = descriptor >= 0 && fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode) &&
        status.st_size > 0 && (u64)status.st_size <= limit;
    u64 used = 0;
    char8* bytes = success ? arena_allocate(arena, char8, (u64)status.st_size + 1) : 0;
    while (success && used < (u64)status.st_size)
    {
        ssize_t count = read(descriptor, bytes + used, (size_t)((u64)status.st_size - used));
        if (count > 0) { used += (u64)count; }
        else if (count == 0 || errno != EINTR) { success = false; }
    }
    if (success)
    {
        char extra = 0;
        success = read(descriptor, &extra, 1) == 0;
    }
    if (descriptor >= 0 && close(descriptor) != 0) { success = false; }
    if (success) { result = (String8){.pointer = bytes, .length = used}; }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_closure_cache_path(Arena* arena, String8 build, String8 key)
{
    String8 text = compiler_closure_read(arena, path_join(arena, build, S8("CMakeCache.txt")), BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT);
    String8 prefix = string_format(arena, S8("{S8}:FILEPATH="), key);
    String8 result = {0};
    u64 start = 0;
    for (u64 index = 0; index <= text.length && !result.length; index += 1)
    {
        if (index == text.length || text.pointer[index] == '\n')
        {
            String8 line = {.pointer = text.pointer + start, .length = index - start};
            start = index + 1;
            if (string_starts_with_sequence(line, prefix))
            {
                String8 value = {.pointer = line.pointer + prefix.length, .length = line.length - prefix.length};
                result = os_path_absolute(arena, value, true);
            }
        }
    }
    return result;
}


typedef struct CompilerClosureBootstrapIdentity CompilerClosureBootstrapIdentity;
struct CompilerClosureBootstrapIdentity
{
    String8 configuration;
    String8 marker;
    String8 marker_sha256;
    String8 artifact;
    String8 artifact_sha256;
    u64 dependency_count;
    u64 dependency_bytes;
};

BUSTER_GLOBAL_LOCAL bool compiler_closure_marker_fields(String8 line, String8 fields[4], u64* count)
{
    bool result = true;
    u64 start = 0;
    *count = 0;
    for (u64 index = 0; result && index <= line.length; index += 1)
    {
        if (index == line.length || line.pointer[index] == '\t')
        {
            result = *count < 4;
            if (result)
            {
                fields[*count] = (String8){.pointer = line.pointer + start, .length = index - start};
                *count += 1;
                start = index + 1;
            }
        }
    }
    return result;
}

// Validate the exact current wrapper's marker/artifact/dependency closure.
// source and cache may be parked copies; identity paths remain logical ROOT
// paths. A valid marker in an unrelated historical configuration is ignored.
BUSTER_GLOBAL_LOCAL bool compiler_closure_bootstrap_marker(Arena* arena, String8 root, String8 source,
    String8 cache, String8 entry, String8 marker, String8 configuration, CompilerClosureBootstrapIdentity* identity)
{
    CompilerClosureBootstrapIdentity parsed = {.configuration = configuration};
    String8 marker_path = path_join(arena, cache, marker);
    String8 entry_path = path_join(arena, cache, entry);
    String8 text = compiler_closure_read(arena, marker_path, BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT);
    String8 checked_marker_sha256 = {0};
    struct stat marker_status = {0};
    bool result = compiler_closure_path_safe(entry) && compiler_closure_path_safe(marker) &&
        production_profile_path_components_safe(entry) && production_profile_path_components_safe(marker) &&
        entry.pointer[0] != '/' && marker.pointer[0] != '/' &&
        stage_object_sha256_valid(configuration) && text.length &&
        compiler_closure_hash(arena, marker_path, &checked_marker_sha256, &marker_status) &&
        string_equal(checked_marker_sha256, production_profile_sha256_text(arena, text));
    u64 state = 0;
    bool saw_build_c = false;
    String8 previous_dependency = {0};
    String8 remaining = text;
    String8 line = {0};
    while (result && text_next_line(&remaining, &line))
    {
        String8 fields[4] = {0};
        u64 count = 0;
        result = compiler_closure_marker_fields(line, fields, &count);
        if (result && state == 0)
        {
            result = count == 1 && string_equal(fields[0], S8("BUSTER_BOOTSTRAP_CACHE_V1"));
            if (result) { state = 1; }
        }
        else if (result && state == 1)
        {
            result = count == 2 && string_equal(fields[0], S8("config")) && string_equal(fields[1], configuration);
            if (result) { state = 2; }
        }
        else if (result && state == 2)
        {
            result = count == 3 && string_equal(fields[0], S8("artifact")) &&
                compiler_closure_path_safe(fields[1]) && production_profile_path_components_safe(fields[1]) &&
                string_first_code_unit(fields[1], '/') == BUSTER_STRING_NO_MATCH &&
                string_first_code_unit(fields[1], '\\') == BUSTER_STRING_NO_MATCH && stage_object_sha256_valid(fields[2]);
            if (result)
            {
                String8 artifact = path_join(arena, entry_path, fields[1]);
                String8 artifact_sha256 = {0};
                struct stat status = {0};
                result = string_equal(marker_path, string_format(arena, S8("{S8}.complete"), artifact)) &&
                    compiler_closure_hash(arena, artifact, &artifact_sha256, &status) && status.st_size > 0 &&
                    (status.st_mode & 0111) && string_equal(artifact_sha256, fields[2]);
                if (result)
                {
                    parsed.artifact = path_join(arena, entry, fields[1]);
                    parsed.artifact_sha256 = artifact_sha256;
                    state = 3;
                }
            }
        }
        else if (result && state == 3)
        {
            if (count == 1 && string_equal(fields[0], S8("END")))
            {
                result = parsed.dependency_count != 0 && saw_build_c;
                if (result) { state = 4; }
            }
            else
            {
                result = count == 3 && string_equal(fields[0], S8("dependency")) &&
                    compiler_closure_path_safe(fields[1]) && stage_object_sha256_valid(fields[2]) &&
                    parsed.dependency_count < BUSTER_COMPILER_CLOSURE_RECORD_LIMIT &&
                    (!previous_dependency.length || assembly_import_string_compare(&previous_dependency, &fields[1]) < 0);
                if (result)
                {
                    bool absolute = fields[1].pointer[0] == '/';
                    result = production_profile_path_components_safe(fields[1]);
                    String8 dependency = fields[1];
                    if (!absolute) { dependency = path_join(arena, source, fields[1]); }
                    else if (production_profile_path_is_child(root, fields[1]))
                    {
                        String8 relative = {.pointer = fields[1].pointer + root.length + 1,
                            .length = fields[1].length - root.length - 1};
                        dependency = path_join(arena, source, relative);
                    }
                    String8 resolved = result ? os_path_absolute(arena, dependency, true) : (String8){0};
                    result = result && resolved.length && (absolute || production_profile_path_is_child(source, resolved));
                    String8 actual_sha256 = {0};
                    struct stat status = {0};
                    if (result)
                    {
                        String8 terminated = string_duplicate_arena(arena, resolved, true);
                        result = lstat((char*)terminated.pointer, &status) == 0 && S_ISREG(status.st_mode) && status.st_size >= 0 &&
                            (u64)status.st_size <= BUSTER_COMPILER_CLOSURE_BYTE_LIMIT - parsed.dependency_bytes &&
                            compiler_closure_hash(arena, resolved, &actual_sha256, &status) && string_equal(actual_sha256, fields[2]);
                    }
                    if (result)
                    {
                        saw_build_c |= string_equal(resolved, path_join(arena, source, S8("build.c")));
                        previous_dependency = fields[1];
                        parsed.dependency_count += 1;
                        parsed.dependency_bytes += (u64)status.st_size;
                    }
                }
            }
        }
        else { result = false; }
    }
    result = result && state == 4 && saw_build_c;
    if (result)
    {
        parsed.marker = marker;
        parsed.marker_sha256 = checked_marker_sha256;
        *identity = parsed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_bootstrap_identity(Arena* arena, String8 root, String8 source,
    String8 cache, CompilerClosureBootstrapIdentity* identity)
{
    String8 compiler = os_path_absolute_lexical(arena, executable_resolve_in_path(arena, S8("tcc")), true);
    String8 compiler_hash = {0};
    String8 helper_hash = {0};
    struct stat status = {0};
    bool result = compiler.length && compiler_closure_hash(arena, os_path_absolute(arena, compiler, true), &compiler_hash, &status) &&
        compiler_closure_hash(arena, path_join(arena, source, S8("tools/bootstrap_driver.sh")), &helper_hash, &status);
    String8 payload = result ? string_format(arena, S8("BUSTER_BOOTSTRAP_CONFIG_V1\ncompiler\t{S8}\ncompiler-sha256\t{S8}\n"
        "flag\t-Isrc\nflag\t-Wall\nflag\t-Werror\nflag\t-Wno-unused-function\nflag\t-g\nflag\t-MD\nhelper-sha256\t{S8}\n"),
        compiler, compiler_hash, helper_hash) : (String8){0};
    String8 configuration = production_profile_sha256_text(arena, payload);
    String8 entry = path_join(arena, S8("posix"), configuration);
    MuslDirectoryEntry* entries = 0;
    u64 count = 0;
    result = result && compiler_closure_list(arena, path_join(arena, cache, entry),
        BUSTER_COMPILER_CLOSURE_RECORD_LIMIT, &entries, &count);
    if (result) { compiler_closure_sort(entries, count); }
    bool selected = false;
    for (u64 index = 0; result && !selected && index < count; index += 1)
    {
        if (!entries[index].is_directory && string_ends_with_sequence(entries[index].name, S8(".complete")))
        {
            selected = compiler_closure_bootstrap_marker(arena, root, source, cache, entry,
                path_join(arena, entry, entries[index].name), configuration, identity);
        }
    }
    result = result && selected;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_closure_inventory(Arena* arena, String8 root, String8 source, String8 build, String8 bootstrap, String8 base, String8 tree)
{
    CompilerClosureInventory inventory = {.arena = arena, .success = true};
    ProductionProfileCommandResult revision = compiler_closure_git(arena, root, S8("HEAD"));
    ProductionProfileCommandResult source_tree = compiler_closure_git(arena, root, S8("HEAD^{tree}"));
    String8 clean_arguments[] = {S8("git"), S8("-C"), root, S8("diff"), S8("--quiet"), S8("--exit-code"), S8("HEAD"), S8("--")};
    ProductionProfileCommandResult clean = compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clean_arguments));
    inventory.success = clean.success && revision.success && source_tree.success &&
        string_equal(production_profile_trim(revision.output), base) && string_equal(production_profile_trim(source_tree.output), tree);
    string8_list_push(arena, &inventory.lines, string_format(arena, S8("BUSTER_COMPILER_CLOSURE_V1\nroot\t{S8}\nbase\t{S8}\ntree\t{S8}\n"),
        root, base, tree));
    compiler_closure_walk(&inventory, S8("source"), source, true);
    compiler_closure_walk(&inventory, S8("build"), build, false);
    compiler_closure_walk(&inventory, S8("bootstrap"), bootstrap, false);
    CompilerClosureBootstrapIdentity producer = {0};
    inventory.success = inventory.success && compiler_closure_bootstrap_identity(arena, root, source, bootstrap, &producer);
    if (inventory.success)
    {
        string8_list_push(arena, &inventory.lines, string_format(arena, S8("binding\tbootstrap_config\t{S8}\n"
            "binding\tbootstrap_marker\t{S8}\n" "binding\tbootstrap_artifact\t{S8}\n"),
            producer.configuration, producer.marker, producer.artifact));
    }
    String8 cache_keys[] = {S8("CMAKE_C_COMPILER"), S8("CMAKE_LINKER"), S8("CMAKE_MAKE_PROGRAM")};
    String8 configured_clang = {0};
    for (u64 index = 0; inventory.success && index < BUSTER_ARRAY_LENGTH(cache_keys); index += 1)
    {
        String8 path = compiler_closure_cache_path(arena, build, cache_keys[index]);
        inventory.success = path.length && compiler_closure_path_safe(path);
        if (inventory.success)
        {
            string8_list_push(arena, &inventory.lines, string_format(arena, S8("binding\t{S8}\t{S8}\n"), cache_keys[index], path));
            compiler_closure_record(&inventory, S8("tool"), cache_keys[index], path);
            if (!index) { configured_clang = path; }
        }
    }
    String8 tools[] = {S8("clang"), S8("cmake"), S8("ninja"), S8("tcc"), S8("ld"), S8("ld.lld"), S8("mold")};
    for (u64 index = 0; inventory.success && index < BUSTER_ARRAY_LENGTH(tools); index += 1)
    {
        String8 tool = executable_resolve_in_path(arena, tools[index]);
        if (tool.length)
        {
            tool = os_path_absolute(arena, tool, true);
            inventory.success = tool.length && compiler_closure_path_safe(tool);
            if (inventory.success)
            {
                string8_list_push(arena, &inventory.lines, string_format(arena, S8("binding\t{S8}\t{S8}\n"), tools[index], tool));
                compiler_closure_record(&inventory, S8("tool"), tools[index], tool);
            }
        }
        else
        {
            string8_list_push(arena, &inventory.lines, string_format(arena, S8("binding\t{S8}\tabsent\n"), tools[index]));
            if (index < 4) { inventory.success = false; }
        }
    }
    if (inventory.success)
    {
        String8 command[] = {configured_clang, S8("-print-resource-dir")};
        ProductionProfileCommandResult resource = compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        String8 directory = resource.success ? os_path_absolute(arena, production_profile_trim(resource.output), true) : (String8){0};
        inventory.success = directory.length && compiler_closure_path_safe(directory);
        if (inventory.success)
        {
            string8_list_push(arena, &inventory.lines, string_format(arena, S8("binding\tresource\t{S8}\n"), directory));
            compiler_closure_walk(&inventory, S8("resource"), directory, false);
        }
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

// Copies every preserved source byte, including ignored generated inputs.
// Manifest paths are checked before touching the matched checkout. Directories
// precede children in the producer's breadth-first inventory.
BUSTER_GLOBAL_LOCAL bool compiler_closure_copy_source(Arena* arena, String8 source, String8 destination, String8 manifest)
{
    bool result = true;
    u64 start = 0;
    for (u64 index = 0; result && index <= manifest.length; index += 1)
    {
        if (index == manifest.length || manifest.pointer[index] == '\n')
        {
            String8 line = {.pointer = manifest.pointer + start, .length = index - start};
            start = index + 1;
            if (string_starts_with_sequence(line, S8("source\t")))
            {
                String8 fields[8] = {0};
                u64 field = 0;
                u64 field_start = 0;
                for (u64 at = 0; at <= line.length && field < BUSTER_ARRAY_LENGTH(fields); at += 1)
                {
                    if (at == line.length || line.pointer[at] == '\t')
                    {
                        fields[field++] = (String8){.pointer = line.pointer + field_start, .length = at - field_start};
                        field_start = at + 1;
                    }
                }
                u64 mode = 0;
                result = field == BUSTER_ARRAY_LENGTH(fields) && compiler_closure_path_safe(fields[7]) &&
                    production_profile_path_components_safe(fields[7]) && fields[7].pointer[0] != '/' &&
                    production_profile_u64(fields[2], &mode) && mode <= 07777;
                if (result)
                {
                    String8 from = path_join(arena, source, fields[7]);
                    String8 to = path_join(arena, destination, fields[7]);
                    if (string_equal(fields[1], S8("D")))
                    {
                        result = os_make_directory_exclusive(to).created;
                    }
                    else if (string_equal(fields[1], S8("F")))
                    {
                        result = file_copy((CopyFileArguments){.original_path = from, .new_path = to});
                    }
                    else { result = false; }
                    if (result) { result = chmod((char*)to.pointer, (mode_t)mode) == 0; }
                }
            }
        }
    }
    result = result && compiler_closure_timestamps(arena, destination, manifest);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_clear_source(Arena* arena, String8 root)
{
    MuslDirectoryEntry* entries = 0;
    u64 count = 0;
    bool result = compiler_closure_list(arena, root, BUSTER_COMPILER_CLOSURE_RECORD_LIMIT, &entries, &count);
    for (u64 index = 0; result && index < count; index += 1)
    {
        String8 name = entries[index].name;
        bool retained = string_equal(name, S8(".git")) || string_equal(name, S8("build")) || string_equal(name, S8(".cache"));
        if (!retained) { result = remove_path_recursive(arena, path_join(arena, root, name)); }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_cache_valid(Arena* arena, String8 root)
{
    String8 cache = path_join(arena, root, S8("build/CMakeCache.txt"));
    String8 text = compiler_closure_read(arena, cache, BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT);
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
    String8 saved_source = path_join(arena, snapshot, S8("source"));
    String8 saved_build = path_join(arena, snapshot, S8("build"));
    String8 saved_bootstrap = path_join(arena, snapshot, S8("bootstrap"));
    String8 manifest_path = path_join(arena, snapshot, S8("manifest.tsv"));
    String8 marker_path = path_join(arena, snapshot, S8(".complete"));
    String8 manifest = {0};
    String8 digest = {0};
    u64 start = os_now_microseconds();
    u64 cleanup_us = compiler_closure_cleanup_us;
    u64 cleanup_waves = compiler_closure_cleanup_waves;
    u64 cleanup_signalled = compiler_closure_cleanup_signalled;
    u64 cleanup_reaped = compiler_closure_cleanup_reaped;
    u64 harness_preparation_us = 0;
    String8 harness_hash = {0};
    CompilerClosureBootstrapIdentity producer = {0};
    bool success = compiler_closure_admitting() && (snapshot_operation || restore || verify);
    if (success && snapshot_operation)
    {
        success = compiler_closure_cache_valid(arena, root) && !path_exists(arena, snapshot);
        if (success)
        {
            u64 preparation_start = os_now_microseconds();
                    String8 prepare[] = {path_join(arena, root, S8("build.sh")), S8("bench_throughput"), S8("help")};
            ProductionProfileCommandResult prepared = compiler_closure_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(prepare));
            harness_preparation_us = os_now_microseconds() - preparation_start;
            struct stat harness_status = {0};
            success = prepared.success && compiler_closure_hash(arena, path_join(arena, build, S8("throughput-tools/throughput")),
                &harness_hash, &harness_status) && (harness_status.st_mode & 0111);
        }
        if (success) { manifest = compiler_closure_inventory(arena, root, root, build, bootstrap, base, tree); }
        success = success && manifest.length && os_make_directory_exclusive(snapshot).created;
        if (success)
        {
            digest = production_profile_sha256_text(arena, manifest);
            success = production_profile_write(manifest_path, manifest) && os_make_directory_exclusive(saved_source).created &&
                compiler_closure_copy_source(arena, root, saved_source, manifest) &&
                os_file_replace(build, saved_build).v == 0 && os_file_replace(bootstrap, saved_bootstrap).v == 0 &&
                string_equal(manifest, compiler_closure_inventory(arena, root, saved_source, saved_build, saved_bootstrap, base, tree)) &&
                production_profile_write(marker_path, digest);
        }
    }
    else if (success)
    {
        manifest = compiler_closure_read(arena, manifest_path, BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT);
        String8 marker = compiler_closure_read(arena, marker_path, SHA256_HEX_CAPACITY - 1);
        digest = production_profile_sha256_text(arena, manifest);
        success = manifest.length && manifest.length <= BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT &&
            stage_object_sha256_valid(expected_digest) && string_equal(digest, expected_digest) && string_equal(marker, digest);
        if (success && restore)
        {
            success = string_equal(manifest, compiler_closure_inventory(arena, root, saved_source, saved_build, saved_bootstrap, base, tree));
            if (success)
            {
                // Source/root/parked bytes and tools are verified before the
                // only destructive operations. Candidate cache entries cannot
                // survive to select a candidate driver for baseline workloads.
                success = compiler_closure_admitting() && compiler_closure_clear_source(arena, root) && compiler_closure_copy_source(arena, saved_source, root, manifest) &&
                    remove_path_recursive(arena, build) && remove_path_recursive(arena, bootstrap) &&
                    os_file_replace(saved_build, build).v == 0 && os_file_replace(saved_bootstrap, bootstrap).v == 0;
            }
        }
        if (success)
        {
            success = compiler_closure_cache_valid(arena, root) &&
                string_equal(manifest, compiler_closure_inventory(arena, root, root, build, bootstrap, base, tree));
        }
    }
    if (success && !snapshot_operation)
    {
        struct stat status = {0};
        success = compiler_closure_hash(arena, path_join(arena, build, S8("throughput-tools/throughput")), &harness_hash, &status);
    }
    if (success)
    {
        success = compiler_closure_bootstrap_identity(arena, root, snapshot_operation ? saved_source : root,
            snapshot_operation ? saved_bootstrap : bootstrap, &producer);
    }
    {
        String8 receipt = string_format(arena, S8("{{\"schema\":\"" BUSTER_COMPILER_CLOSURE_SCHEMA "\",\"policy\":\"snapshot-v1\","
            "\"state\":\"{S8}\",\"operation\":\"{S8}\",\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"root_sha256\":\"{S8}\","
            "\"manifest_sha256\":\"{S8}\",\"harness_sha256\":\"{S8}\",\"bootstrap_marker_sha256\":\"{S8}\","
            "\"bootstrap_artifact_sha256\":\"{S8}\",\"duration_us\":{u64},\"harness_preparation_us\":{u64},"
            "\"ownership_schema\":\"buster-native-qualification-supervisor-v1\",\"cleanup_proven\":{S8},"
            "\"cleanup_us\":{u64},\"cleanup_waves\":{u64},\"cleanup_signalled\":{u64},\"cleanup_reaped\":{u64}\n}\n"),
            success ? S8("complete") : S8("failed"), operation, base, tree, production_profile_sha256_text(arena, root), digest, harness_hash, producer.marker_sha256, producer.artifact_sha256,
            os_now_microseconds() - start, harness_preparation_us,
            !compiler_closure_cleanup_failed ? S8("true") : S8("false"), compiler_closure_cleanup_us - cleanup_us,
            compiler_closure_cleanup_waves - cleanup_waves, compiler_closure_cleanup_signalled - cleanup_signalled,
            compiler_closure_cleanup_reaped - cleanup_reaped);
        bool retained = !compiler_closure_cleanup_failed || production_profile_write(
            string_format(arena, S8("{S8}.cleanup-uncertain"), receipt_path), S8("native child ownership or cleanup could not be proven; retain this attempt root\n"));
        bool written = retained && (!manifest.length || production_profile_write(string_format(arena, S8("{S8}.manifest.tsv"), receipt_path), manifest)) &&
            production_profile_write(receipt_path, receipt);
        success = success && written;
    }
    string_print(S8("COMPILER_CLOSURE operation={S8} state={S8} same_root=1 compiler_rebuilds=0\n"), operation,
        success ? S8("complete") : S8("failed"));
    return success;
}
#endif

#include "compiler_preparation.c"
#include "compiler_closure_test.c"

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX
    bool signals = compiler_closure_signals_begin();
    if (!signals) { return result; }
    if (arguments.length == 2 && string_equal(arguments.pointer[0], S8("containment-self-test")))
    {
        result = compiler_closure_phase_self_test(arena, arguments.pointer[1]);
    }
    else if (arguments.length && string_equal(arguments.pointer[0], S8("prepare")))
    {
        result = compiler_closure_prepare_main(arena, arguments);
    }
    else if (arguments.length && string_equal(arguments.pointer[0], S8("qualify")))
    {
        result = compiler_closure_qualification_main(arena, arguments);
    }
    else if ((arguments.length == 1 || (arguments.length == 3 && string_equal(arguments.pointer[1], S8("--export")))) &&
        string_equal(arguments.pointer[0], S8("self-test")))
    {
        result = compiler_closure_self_test(arena, arguments.length == 3 ? arguments.pointer[2] : (String8){0});
    }
    else if (arguments.length == 7)
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
    else { string_print(S8("usage: compiler_closure snapshot|restore|verify ROOT SNAPSHOT BASE TREE RECEIPT EXPECTED_SHA256\n"
        "       compiler_closure prepare ROOT OUTPUT POLICY BASE BASE_TREE HEAD HEAD_TREE [SECONDARY_HEAD SECONDARY_TREE]\n"
        "       compiler_closure qualify ROOT OUTPUT BASE BASE_TREE HEAD HEAD_TREE TRUSTED_LAB PYTHON\n")); }
    if (!compiler_closure_signals_end()) { result = PROCESS_RESULT_FAILED; }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    string_print(S8("error: compiler_closure requires the approved Linux filesystem recipe\n"));
#endif
    return result;
}
