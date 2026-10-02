// Linux image-browser catalog/read/worker implementation.
// catalog_open bounds enumeration and copies an immutable lexical path list.
// worker_loop owns one active read/decode and one synchronized result slot.
// worker_cancelled checks generation between read chunks and after decoding.
// worker_stop_join preserves all resources until the OS join succeeds.
// Source bytes and scratch are released before output ownership is published.
// No generic application framework or new native dependency is introduced.

#include <buster/apps/image_browser/image_browser_linux.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

BUSTER_GLOBAL_LOCAL bool image_browser_linux_path_valid(String8 path)
{
    bool result = path.pointer && path.length && path.length <= IMAGE_BROWSER_MAX_PATH_BYTES;
    for (u64 index = 0; result && index < path.length; index += 1)
    {
        result = path.pointer[index] != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_extension(String8 name)
{
    static String8 const extensions[] = {
        S8("png"), S8("jpg"), S8("jpeg"), S8("gif"), S8("bmp"), S8("tga"),
        S8("qoi"), S8("pnm"), S8("pbm"), S8("pgm"), S8("ppm"), S8("pam"),
    };
    u64 start = name.length;
    for (u64 index = 0; index < name.length; index += 1)
    {
        if (name.pointer[index] == '.')
        {
            start = index + 1;
        }
    }
    bool result = false;
    for (u64 extension = 0; !result && extension < BUSTER_ARRAY_LENGTH(extensions); extension += 1)
    {
        String8 wanted = extensions[extension];
        bool equal = name.length - start == wanted.length;
        for (u64 index = 0; equal && index < wanted.length; index += 1)
        {
            u8 byte = (u8)name.pointer[start + index];
            if (byte >= 'A' && byte <= 'Z')
            {
                byte = (u8)(byte + ('a' - 'A'));
            }
            equal = byte == (u8)wanted.pointer[index];
        }
        result = equal;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_join_path(String8 directory, String8 name, char buffer[4096], String8* path)
{
    bool slash = directory.length && directory.pointer[directory.length - 1] != '/';
    u64 size = directory.length + (slash ? 1u : 0u) + name.length;
    bool result = size <= IMAGE_BROWSER_MAX_PATH_BYTES;
    if (result)
    {
        memcpy(buffer, directory.pointer, (size_t)directory.length);
        u64 position = directory.length;
        if (slash)
        {
            buffer[position] = '/';
            position += 1;
        }
        memcpy(buffer + position, name.pointer, (size_t)name.length);
        buffer[size] = 0;
        *path = (String8){.pointer = (char8*)buffer, .length = size};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_catalog_add(ImageBrowserCatalog* catalog, String8 path, u64* bytes, s32* error)
{
    bool result = catalog->paths.length < IMAGE_BROWSER_MAX_FILES &&
                  path.length + 1 <= IMAGE_BROWSER_MAX_CATALOG_BYTES - *bytes;
    if (result)
    {
        char8* owned = arena_allocate(catalog->arena, char8, path.length + 1);
        memcpy(owned, path.pointer, (size_t)path.length);
        owned[path.length] = 0;
        catalog->paths.pointer[catalog->paths.length] = (String8){.pointer = owned, .length = path.length};
        catalog->paths.length += 1;
        *bytes += path.length + 1;
    }
    else
    {
        *error = E2BIG;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s32 image_browser_linux_path_compare(String8 left, String8 right)
{
    u64 common = BUSTER_MIN(left.length, right.length);
    s32 result = (s32)memcmp(left.pointer, right.pointer, (size_t)common);
    if (!result && left.length != right.length)
    {
        result = left.length < right.length ? -1 : 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_browser_linux_catalog_sort(ImageBrowserCatalog* catalog)
{
    // Iterative Shell sort over at most 4096 values; no recursion/callbacks.
    for (u64 gap = catalog->paths.length / 2; gap; gap /= 2)
    {
        for (u64 index = gap; index < catalog->paths.length; index += 1)
        {
            String8 value = catalog->paths.pointer[index];
            u64 position = index;
            while (position >= gap && image_browser_linux_path_compare(catalog->paths.pointer[position - gap], value) > 0)
            {
                catalog->paths.pointer[position] = catalog->paths.pointer[position - gap];
                position -= gap;
            }
            catalog->paths.pointer[position] = value;
        }
    }
}

void image_browser_catalog_destroy(ImageBrowserCatalog* catalog)
{
    if (catalog->arena)
    {
        arena_destroy(catalog->arena, 1);
    }
    *catalog = (ImageBrowserCatalog){0};
}

bool image_browser_catalog_open(String8 input, ImageBrowserCatalog* catalog, s32* error)
{
    *catalog = (ImageBrowserCatalog){0};
    *error = 0;
    bool result = image_browser_linux_path_valid(input);
    char input_c[4096];
    char selected_c[4096];
    String8 selected = {0};
    String8 directory = input;
    struct stat status = {0};
    if (result)
    {
        memcpy(input_c, input.pointer, (size_t)input.length);
        input_c[input.length] = 0;
        if (lstat(input_c, &status) != 0)
        {
            *error = errno;
            result = false;
        }
        else if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
        {
            *error = S_ISLNK(status.st_mode) ? ELOOP : ENOTSUP;
            result = false;
        }
    }
    else
    {
        *error = EINVAL;
    }
    bool explicit_file = result && S_ISREG(status.st_mode);
    if (explicit_file)
    {
        u64 name_start = 0;
        for (u64 index = 0; index < input.length; index += 1)
        {
            if (input.pointer[index] == '/')
            {
                name_start = index + 1;
            }
        }
        directory = name_start ? (String8){.pointer = input.pointer, .length = name_start == 1 ? 1 : name_start - 1} : S8(".");
        String8 name = {.pointer = input.pointer + name_start, .length = input.length - name_start};
        result = image_browser_linux_join_path(directory, name, selected_c, &selected);
        if (!result)
        {
            *error = ENAMETOOLONG;
        }
    }
    int descriptor = -1;
    DIR* stream = 0;
    char directory_c[4096];
    if (result)
    {
        memcpy(directory_c, directory.pointer, (size_t)directory.length);
        directory_c[directory.length] = 0;
        descriptor = open(directory_c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (descriptor < 0)
        {
            *error = errno;
            result = false;
        }
        else
        {
            stream = fdopendir(descriptor);
            result = stream != 0;
            if (!result)
            {
                *error = errno;
                close(descriptor);
            }
        }
    }
    if (result)
    {
        u64 reservation = IMAGE_BROWSER_MAX_CATALOG_BYTES + IMAGE_BROWSER_MAX_FILES * sizeof(String8) + IMAGE_BROWSER_ARENA_OVERHEAD;
        catalog->arena = arena_create((ArenaCreation){
            .reserved_size = reservation, .initial_size = reservation, .flags = {.no_pool = true},
        });
        result = catalog->arena != 0;
        if (result)
        {
            catalog->paths.pointer = arena_allocate(catalog->arena, String8, IMAGE_BROWSER_MAX_FILES);
        }
        else
        {
            *error = ENOMEM;
        }
    }
    u64 path_bytes = 0;
    if (result && explicit_file)
    {
        result = image_browser_linux_catalog_add(catalog, selected, &path_bytes, error);
    }
    u64 scanned = 0;
    bool finished = false;
    while (result && !finished)
    {
        errno = 0;
        struct dirent* record = readdir(stream);
        if (!record)
        {
            finished = true;
            if (errno)
            {
                *error = errno;
                result = false;
            }
        }
        else
        {
            scanned += 1;
            if (scanned > IMAGE_BROWSER_MAX_SCAN_RECORDS)
            {
                *error = E2BIG;
                result = false;
            }
            else
            {
                String8 name = {.pointer = (char8*)record->d_name, .length = (u64)strlen(record->d_name)};
                if (image_browser_linux_extension(name))
                {
                    struct stat entry = {0};
                    int stat_result = fstatat(dirfd(stream), record->d_name, &entry, AT_SYMLINK_NOFOLLOW);
                    if (stat_result != 0 && errno != ENOENT)
                    {
                        *error = errno;
                        result = false;
                    }
                    else if (stat_result == 0 && S_ISREG(entry.st_mode))
                    {
                        char path_c[4096];
                        String8 path = {0};
                        result = image_browser_linux_join_path(directory, name, path_c, &path);
                        if (!result)
                        {
                            *error = ENAMETOOLONG;
                        }
                        else if (!explicit_file || image_browser_linux_path_compare(path, selected) != 0)
                        {
                            result = image_browser_linux_catalog_add(catalog, path, &path_bytes, error);
                        }
                    }
                }
            }
        }
    }
    if (stream && closedir(stream) != 0 && result)
    {
        *error = errno;
        result = false;
    }
    if (result && !catalog->paths.length)
    {
        *error = ENOENT;
        result = false;
    }
    if (result)
    {
        image_browser_linux_catalog_sort(catalog);
        if (explicit_file)
        {
            for (u64 index = 0; index < catalog->paths.length; index += 1)
            {
                if (image_browser_linux_path_compare(catalog->paths.pointer[index], selected) == 0)
                {
                    catalog->initial_index = index;
                }
            }
        }
    }
    else
    {
        image_browser_catalog_destroy(catalog);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_browser_worker_lock(ImageBrowserWorker* worker)
{
    BUSTER_CHECK_RAW(pthread_mutex_lock(&worker->mutex) == 0);
}

BUSTER_GLOBAL_LOCAL void image_browser_worker_unlock(ImageBrowserWorker* worker)
{
    BUSTER_CHECK_RAW(pthread_mutex_unlock(&worker->mutex) == 0);
}

BUSTER_GLOBAL_LOCAL bool image_browser_worker_cancelled(ImageBrowserWorker* worker, ImageBrowserRequest request)
{
    image_browser_worker_lock(worker);
    bool result = worker->stopping || worker->generation != request.generation;
    image_browser_worker_unlock(worker);
    return result;
}

typedef struct ImageBrowserDecodeWork ImageBrowserDecodeWork;
struct ImageBrowserDecodeWork
{
    ImageBrowserRequest request;
    ByteSlice encoded;
    Arena* output;
    Arena* scratch;
    ImageBrowserResult result;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType image_browser_linux_decode(void* argument)
{
    ImageBrowserDecodeWork* work = (ImageBrowserDecodeWork*)argument;
    work->result = image_browser_decode(work->request, work->encoded, work->output, work->scratch, IMAGE_FORMAT_UNKNOWN);
    return;
}

BUSTER_GLOBAL_LOCAL ImageBrowserResult image_browser_linux_load(ImageBrowserWorker* worker, ImageBrowserRequest request, String8 path)
{
    ImageBrowserResult result = {.request = request, .status = IMAGE_BROWSER_LOAD_READ_ERROR};
    bool ready = !image_browser_worker_cancelled(worker, request);
    if (!ready)
    {
        result.status = IMAGE_BROWSER_LOAD_CANCELLED;
    }
    char path_c[4096];
    int descriptor = -1;
    struct stat before = {0};
    Arena* source = 0;
    Arena* output = 0;
    Arena* scratch = 0;
    ByteSlice encoded = {0};
    if (ready)
    {
        // NONBLOCK prevents a replaced FIFO from blocking before fstat.
        // NOFOLLOW rejects a replaced leaf symlink. Only regular files decode.
        memcpy(path_c, path.pointer, (size_t)path.length);
        path_c[path.length] = 0;
        descriptor = open(path_c, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        if (descriptor < 0)
        {
            result.system_error = errno;
            ready = false;
        }
    }
    if (ready)
    {
        if (fstat(descriptor, &before) != 0)
        {
            result.system_error = errno;
            ready = false;
        }
        else if (!S_ISREG(before.st_mode) || before.st_size < 0)
        {
            result.system_error = ENOTSUP;
            ready = false;
        }
        else
        {
            result.encoded_size = (u64)before.st_size;
            if (result.encoded_size > IMAGE_BROWSER_MAX_ENCODED_BYTES)
            {
                result.status = IMAGE_BROWSER_LOAD_ENCODED_LIMIT;
                ready = false;
            }
        }
    }
    if (ready)
    {
        u64 reservation = result.encoded_size + IMAGE_BROWSER_ARENA_OVERHEAD;
        source = arena_create((ArenaCreation){
            .reserved_size = reservation, .initial_size = reservation, .flags = {.no_pool = true},
        });
        ready = source != 0;
        if (ready)
        {
            encoded.length = result.encoded_size;
            encoded.pointer = encoded.length ? arena_allocate(source, u8, encoded.length) : 0;
        }
        else
        {
            result.system_error = ENOMEM;
        }
    }
    u64 position = 0;
    while (ready && position < encoded.length)
    {
        if (image_browser_worker_cancelled(worker, request))
        {
            result.status = IMAGE_BROWSER_LOAD_CANCELLED;
            ready = false;
        }
        else
        {
            u64 amount = BUSTER_MIN(IMAGE_BROWSER_READ_CHUNK, encoded.length - position);
            ssize_t count = read(descriptor, encoded.pointer + position, (size_t)amount);
            if (count > 0)
            {
                position += (u64)count;
            }
            else if (count == 0)
            {
                result.system_error = EIO;
                ready = false;
            }
            else if (errno != EINTR)
            {
                result.system_error = errno;
                ready = false;
            }
        }
    }
    if (ready)
    {
        struct stat after = {0};
        if (fstat(descriptor, &after) != 0)
        {
            result.system_error = errno;
            ready = false;
        }
        else if (before.st_size != after.st_size || before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
                 before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
                 before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        {
            result.system_error = EIO;
            ready = false;
        }
    }
    // Linux consumes the descriptor even when close reports EINTR: no retry.
    if (descriptor >= 0 && close(descriptor) != 0 && ready)
    {
        result.system_error = errno;
        ready = false;
    }
    if (ready && image_browser_worker_cancelled(worker, request))
    {
        result.status = IMAGE_BROWSER_LOAD_CANCELLED;
        ready = false;
    }
    if (ready)
    {
        u64 output_size = IMAGE_BROWSER_MAX_DECODED_BYTES + IMAGE_BROWSER_ARENA_OVERHEAD;
        u64 scratch_size = IMAGE_BROWSER_MAX_SCRATCH_BYTES + IMAGE_BROWSER_ARENA_OVERHEAD;
        output = arena_create((ArenaCreation){
            .reserved_size = output_size, .initial_size = output_size, .flags = {.no_pool = true},
        });
        scratch = arena_create((ArenaCreation){
            .reserved_size = scratch_size, .initial_size = scratch_size, .flags = {.no_pool = true},
        });
        ready = output && scratch;
        if (!ready)
        {
            result.system_error = ENOMEM;
        }
    }
    if (ready)
    {
        ImageBrowserDecodeWork work = {.request = request, .encoded = encoded, .output = output, .scratch = scratch};
        // The OS worker wrapper supplies its ThreadContext. One lane executes
        // inline: no second gang, context, worker or persistent source borrow.
        lane_run(1, image_browser_linux_decode, &work);
        result = work.result;
        output = 0;
    }
    if (scratch)
    {
        arena_destroy(scratch, 1);
    }
    if (source)
    {
        arena_destroy(source, 1);
    }
    if (output)
    {
        arena_destroy(output, 1);
    }
    if (image_browser_worker_cancelled(worker, request))
    {
        u64 encoded_size = result.encoded_size;
        image_browser_result_release(&result);
        result = (ImageBrowserResult){.request = request, .status = IMAGE_BROWSER_LOAD_CANCELLED, .encoded_size = encoded_size};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ThreadReturnType image_browser_worker_loop(void* argument)
{
    ImageBrowserWorker* worker = (ImageBrowserWorker*)argument;
    bool finished = false;
    while (!finished)
    {
        ImageBrowserRequest request = {0};
        String8 path = {0};
        bool accepted = false;
        image_browser_worker_lock(worker);
        while (!worker->queued && !worker->stopping)
        {
            BUSTER_CHECK_RAW(pthread_cond_wait(&worker->condition, &worker->mutex) == 0);
        }
        if (worker->queued)
        {
            request = worker->request;
            path = worker->path;
            worker->queued = false;
            worker->active = true;
            accepted = true;
        }
        else
        {
            finished = true;
        }
        image_browser_worker_unlock(worker);
        if (accepted)
        {
            ImageBrowserResult loaded = image_browser_linux_load(worker, request, path);
            image_browser_worker_lock(worker);
            BUSTER_CHECK_RAW(!worker->has_result);
            worker->active = false;
            worker->request = (ImageBrowserRequest){0};
            worker->path = (String8){0};
            worker->result = loaded;
            worker->has_result = true;
            finished = worker->stopping;
            image_browser_worker_unlock(worker);
        }
    }
    return;
}

bool image_browser_worker_start(ImageBrowserWorker* worker, s32* error)
{
    *worker = (ImageBrowserWorker){0};
    *error = pthread_mutex_init(&worker->mutex, 0);
    bool mutex_ready = *error == 0;
    bool condition_ready = false;
    if (mutex_ready)
    {
        *error = pthread_cond_init(&worker->condition, 0);
        condition_ready = *error == 0;
    }
    bool result = condition_ready;
    if (result)
    {
        worker->initialized = true;
        worker->thread = os_thread_create((ThreadCreateOptions){.callback = image_browser_worker_loop, .argument = worker});
        result = worker->thread != 0;
        if (!result)
        {
            *error = EAGAIN;
        }
    }
    if (!result)
    {
        if (condition_ready)
        {
            BUSTER_CHECK_RAW(pthread_cond_destroy(&worker->condition) == 0);
        }
        if (mutex_ready)
        {
            BUSTER_CHECK_RAW(pthread_mutex_destroy(&worker->mutex) == 0);
        }
        *worker = (ImageBrowserWorker){0};
    }
    return result;
}

void image_browser_worker_set_generation(ImageBrowserWorker* worker, u64 generation)
{
    if (worker->initialized)
    {
        image_browser_worker_lock(worker);
        worker->generation = generation;
        image_browser_worker_unlock(worker);
    }
}

bool image_browser_worker_submit(ImageBrowserWorker* worker, ImageBrowserRequest request, String8 path)
{
    bool result = worker->initialized && request.generation && image_browser_linux_path_valid(path);
    if (result)
    {
        image_browser_worker_lock(worker);
        result = !worker->stopping && !worker->queued && !worker->active && !worker->has_result &&
                 worker->generation == request.generation;
        if (result)
        {
            worker->request = request;
            worker->path = path;
            worker->queued = true;
            result = pthread_cond_signal(&worker->condition) == 0;
            if (!result)
            {
                worker->request = (ImageBrowserRequest){0};
                worker->path = (String8){0};
                worker->queued = false;
            }
        }
        image_browser_worker_unlock(worker);
    }
    return result;
}

bool image_browser_worker_take(ImageBrowserWorker* worker, ImageBrowserResult* result)
{
    bool taken = false;
    if (worker->initialized)
    {
        image_browser_worker_lock(worker);
        taken = worker->has_result;
        if (taken)
        {
            *result = worker->result;
            worker->result = (ImageBrowserResult){0};
            worker->has_result = false;
        }
        image_browser_worker_unlock(worker);
    }
    return taken;
}

bool image_browser_worker_stop_join(ImageBrowserWorker* worker, ImageBrowserResult* result, bool* has_result)
{
    *has_result = false;
    bool joined = true;
    if (worker->initialized)
    {
        image_browser_worker_lock(worker);
        worker->stopping = true;
        BUSTER_CHECK_RAW(pthread_cond_signal(&worker->condition) == 0);
        image_browser_worker_unlock(worker);
        joined = os_thread_join(worker->thread);
        if (joined)
        {
            worker->thread = 0;
            image_browser_worker_lock(worker);
            *has_result = worker->has_result;
            if (*has_result)
            {
                *result = worker->result;
                worker->result = (ImageBrowserResult){0};
                worker->has_result = false;
            }
            image_browser_worker_unlock(worker);
            BUSTER_CHECK_RAW(pthread_cond_destroy(&worker->condition) == 0);
            BUSTER_CHECK_RAW(pthread_mutex_destroy(&worker->mutex) == 0);
            *worker = (ImageBrowserWorker){0};
        }
    }
    return joined;
}
