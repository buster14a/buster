/* Gateway export and offline reconstruction. This is included after transport.c.
 * Output is one 1024-byte receipt followed by exactly the canonical archive.
 * A failed/partial stream is never replayable. Destination paths are local CLI
 * arguments only, and never cross the authenticated service protocol.
 */
BUSTER_GLOBAL_LOCAL BqError bq_export_download(char const* socket_path, BqPacket* request, FILE* output,
                                                FILE* diagnostics)
{
    BqPacket response;
    BqError error = bq_transport_request(socket_path, request, &response);
    if (error == BQ_OK && !bq_public_response_valid(request, &response)) error = BQ_EXPORT_CORRUPT;
    u8 receipt[BQ_EXPORT_RECEIPT_CAP] = {0};
    char receipt_digest[SHA256_HEX_CAPACITY] = {0};
    u64 total = 0;
    if (error == BQ_OK)
    {
        u8 const* body = response.bytes + BQ_CONTROL_HEADER;
        memcpy(receipt, body + BQ_EXPORT_REPLY_HEADER, sizeof(receipt));
        memcpy(receipt_digest, body + 48, 64);
        total = bq_u64(body + 36);
#ifdef __linux__
        if (!bq_export_receipt_valid(receipt) || total != bq_u64(receipt + 24)) error = BQ_EXPORT_CORRUPT;
#endif
    }
    if (error == BQ_OK)
    {
        if (fwrite(receipt, 1, sizeof(receipt), output) != sizeof(receipt)) error = BQ_IO;
        memcpy(request->bytes + BQ_CONTROL_HEADER + 88, receipt_digest, 64);
    }
    Sha256 archive;
    sha256_init(&archive);
#ifdef __linux__
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(),
                                      bq_export_prepare_milliseconds(bq_export_receipt_recipe(receipt)));
#endif
    for (u64 cursor = 0; error == BQ_OK && cursor < total;)
    {
#ifdef __linux__
        if (!bq_worker_remaining(deadline)) error = BQ_EXPORT_TIMEOUT;
#endif
        bq_put64(request->bytes + BQ_CONTROL_HEADER + 80, cursor);
        if (error == BQ_OK) error = bq_transport_request(socket_path, request, &response);
        if (error == BQ_OK && !bq_public_response_valid(request, &response)) error = BQ_EXPORT_CORRUPT;
        if (error == BQ_OK)
        {
            u8 const* body = response.bytes + BQ_CONTROL_HEADER;
            u32 count = bq_u32(body + 44);
            if (bq_u64(body + 36) != total || memcmp(body + 48, receipt_digest, 64)) error = BQ_EXPORT_CORRUPT;
            else if (fwrite(body + BQ_EXPORT_REPLY_HEADER, 1, count, output) != count) error = BQ_IO;
            else sha256_add(&archive, body + BQ_EXPORT_REPLY_HEADER, count);
            cursor = bq_u64(body + 28);
        }
    }
#ifdef __linux__
    if (error == BQ_OK && !bq_worker_remaining(deadline)) error = BQ_EXPORT_TIMEOUT;
#endif
    if (error == BQ_OK)
    {
        char digest[SHA256_HEX_CAPACITY];
        sha256_finish_hex(&archive, (char8*)digest);
        if (memcmp(digest, receipt + 304, 64)) error = BQ_EXPORT_CORRUPT;
        else if (fflush(output) != 0) error = BQ_IO;
        else if (fprintf(diagnostics, "export-receipt-sha256=%s\n", receipt_digest) < 0) error = BQ_IO;
    }
    return error;
}

#ifdef __linux__
BUSTER_GLOBAL_LOCAL BqError bq_export_unpack(char const* archive_path, char const* destination,
                                              char const* expected_receipt)
{
    int input = open(archive_path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    struct stat before = {0}, after = {0};
    BqError error = !expected_receipt || strlen(expected_receipt) != 64 ||
                    !bq_result_digest_valid((u8 const*)expected_receipt) ? BQ_BAD_REQUEST :
                    input < 0 || fstat(input, &before) != 0 || !S_ISREG(before.st_mode) ? BQ_EXPORT_MISSING : BQ_OK;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_PREPARE_MILLISECONDS);
    u8 receipt[BQ_EXPORT_RECEIPT_CAP];
    if (error == BQ_OK) error = bq_export_io(input, receipt, sizeof(receipt), 0, false, deadline);
    u64 total = 0;
    if (error == BQ_OK)
    {
        char digest[SHA256_HEX_CAPACITY];
        bq_digest(receipt, sizeof(receipt), digest);
        total = bq_u64(receipt + 24);
        if (memcmp(digest, expected_receipt, 64) || !bq_export_receipt_valid(receipt) ||
            before.st_size < 0 || (u64)before.st_size != sizeof(receipt) + total) error = BQ_EXPORT_CORRUPT;
    }
    if (error == BQ_OK)
        deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(),
                                     bq_export_prepare_milliseconds(bq_export_receipt_recipe(receipt)));
    Sha256 archive;
    sha256_init(&archive);
    for (u64 cursor = 0; error == BQ_OK && cursor < total;)
    {
        u8 bytes[BQ_EXPORT_CHUNK_CAP];
        u64 count = total - cursor < sizeof(bytes) ? total - cursor : sizeof(bytes);
        error = bq_export_io(input, bytes, count, sizeof(receipt) + cursor, false, deadline);
        if (error == BQ_OK) sha256_add(&archive, bytes, count);
        cursor += count;
    }
    if (error == BQ_OK)
    {
        char digest[SHA256_HEX_CAPACITY];
        sha256_finish_hex(&archive, (char8*)digest);
        if (memcmp(digest, receipt + 304, 64)) error = BQ_EXPORT_CORRUPT;
    }
    char destination_leaf[BQ_PATH_CAP + 1];
    int parent = error == BQ_OK ? bq_worker_open_lease_parent(destination, destination_leaf) : -1;
    int root = -1;
    if (error == BQ_OK)
    {
        if (parent < 0 || mkdirat(parent, destination_leaf, 0700) != 0) error = BQ_BAD_REQUEST;
        else root = openat(parent, destination_leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (error == BQ_OK && root < 0) error = BQ_IO;
    }
    u64 offset = 0, payload = 0;
    u32 files = 0;
    char previous[BQ_PATH_CAP + 1] = {0};
    u32 entries = error == BQ_OK ? bq_u32(receipt + 44) : 0;
    for (u32 i = 0; error == BQ_OK && i < entries; i += 1)
    {
        u8 header[16];
        if (offset + sizeof(header) > total) error = BQ_EXPORT_CORRUPT;
        if (error == BQ_OK) error = bq_export_io(input, header, sizeof(header), sizeof(receipt) + offset, false, deadline);
        offset += sizeof(header);
        u32 type = error == BQ_OK ? bq_u32(header) : 0;
        u32 length = error == BQ_OK ? bq_u32(header + 4) : 0;
        u64 size = error == BQ_OK ? bq_u64(header + 8) : 0;
        if (error == BQ_OK && ((type != 1 && type != 2) || !length || length > BQ_PATH_CAP ||
            (type == 1 && size) || size > BQ_WORKER_BUNDLE_FILE_CAP || offset + length + size > total))
            error = BQ_EXPORT_CORRUPT;
        char path[BQ_PATH_CAP + 1] = {0};
        if (error == BQ_OK) error = bq_export_io(input, path, length, sizeof(receipt) + offset, false, deadline);
        offset += length;
        if (error == BQ_OK && (strlen(path) != length || !bq_worker_bundle_path_valid(path) ||
                              strcmp(previous, path) >= 0)) error = BQ_EXPORT_CORRUPT;
        u32 depth = 1;
        for (u32 j = 0; error == BQ_OK && j < length; j += 1) if (path[j] == '/') depth += 1;
        if (depth >= BQ_WORKER_BUNDLE_DEPTH_CAP) error = BQ_EXPORT_OVERSIZED;
        memcpy(previous, path, sizeof(path));
        char* slash = strrchr(path, '/');
        char* leaf = slash ? slash + 1 : path;
        if (slash) *slash = 0;
        int directory = error == BQ_OK ? (slash ? bq_worker_bundle_open_relative(root, path) :
                                         openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC)) : -1;
        int output = -1;
        if (error == BQ_OK)
        {
            if (directory < 0) error = BQ_EXPORT_CORRUPT;
            else if (type == 1)
            {
                if (mkdirat(directory, leaf, 0700) != 0) error = BQ_EXPORT_CORRUPT;
            }
            else
            {
                output = openat(directory, leaf, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400);
                if (output < 0) error = BQ_EXPORT_CORRUPT;
                files += 1;
                payload += size;
            }
        }
        for (u64 copied = 0; error == BQ_OK && copied < size;)
        {
            u8 bytes[BQ_EXPORT_CHUNK_CAP];
            u64 count = size - copied < sizeof(bytes) ? size - copied : sizeof(bytes);
            error = bq_export_io(input, bytes, count, sizeof(receipt) + offset + copied, false, deadline);
            if (error == BQ_OK) error = bq_export_io(output, bytes, count, copied, true, deadline);
            copied += count;
        }
        if (output >= 0)
        {
            if (fsync(output) != 0 && error == BQ_OK) error = BQ_IO;
            if (close(output) != 0 && error == BQ_OK) error = BQ_IO;
        }
        if (directory >= 0) close(directory);
        offset += size;
    }
    if (error == BQ_OK && (offset != total || files != bq_u32(receipt + 40) || payload != bq_u64(receipt + 32) ||
                          fstat(input, &after) != 0 || !bq_export_same(&before, &after))) error = BQ_EXPORT_CORRUPT;
    if (error == BQ_OK)
    {
        BqJob job = {.id = bq_u64(receipt + 8), .token = bq_u64(receipt + 16), .phase = BQ_FINISHED, .result_bound = true};
        job.request.size = bq_u32(receipt + 992);
        memcpy(job.request.bytes, receipt + 672, job.request.size);
        memcpy(job.digest, receipt + 48, 64);
        memcpy(job.result_manifest_digest, receipt + 112, 64);
        memcpy(job.result_bundle_digest, receipt + 176, 64);
        memcpy(job.result_full_digest, receipt + 240, 64);
        BqRecipeFiles recipe;
        bool valid = bq_recipe_files(bq_request_recipe(&job.request), &recipe);
        int manifest = valid ? openat(root, recipe.manifest, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
        struct stat info = {0};
        char text[BQ_WORKER_RESULT_CAP + 1] = {0};
        valid = valid && manifest >= 0 && fstat(manifest, &info) == 0 && S_ISREG(info.st_mode) &&
                info.st_size > 0 && info.st_size <= BQ_WORKER_RESULT_CAP;
        if (valid) valid = bq_export_io(manifest, text, (u64)info.st_size, 0, false, deadline) == BQ_OK;
        char const* key = valid ? strstr(text, "\nresult-root=") : NULL;
        char const* end = key ? strchr(key + 13, '\n') : NULL;
        valid = key && end && end > key + 13 && end - (key + 13) <= BQ_PATH_CAP;
        if (valid) memcpy(job.result_root, key + 13, (size_t)(end - (key + 13)));
        if (manifest >= 0) close(manifest);
        if (!valid || bq_worker_result_binding_validate_at(&job, root) != BQ_OK) error = BQ_EXPORT_INVALID;
        if (error == BQ_OK && (!bq_worker_result_sync_tree(root) || fsync(parent) != 0)) error = BQ_IO;
    }
    if (root >= 0) close(root);
    if (parent >= 0) close(parent);
    if (input >= 0) close(input);
    return error;
}
#else
BUSTER_GLOBAL_LOCAL BqError bq_export_unpack(char const* archive_path, char const* destination, char const* expected_receipt)
{
    (void)archive_path;
    (void)destination;
    (void)expected_receipt;
    return BQ_UNSUPPORTED;
}
#endif
