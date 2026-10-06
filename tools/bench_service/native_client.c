/* Private local native upload client. Only typed bounded frames cross the
 * socket; the local filename never does. Completion prints the immutable
 * manifest digest used in an ordinary native-execute-v1 submission. */
#ifdef __linux__
BUSTER_GLOBAL_LOCAL BqError bq_native_client_upload(char const* socket_path, char const* filename, FILE* output)
{
    int file = open(filename, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    char hash[65] = {0};
    BqError error = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
                    info.st_size >= 64 && info.st_size <= BQ_NATIVE_PROGRAM_CAP &&
                    bq_native_elf(file, (u64)info.st_size, 0) &&
                    bq_native_digest_fd(file, 0, (u64)info.st_size, hash, -1) ? BQ_OK : BQ_BAD_REQUEST;
    u8 body[BQ_CONTROL_BODY] = {0};
    if (error == BQ_OK)
    {
        memcpy(body, hash, 64);
        bq_put64(body + 64, (u64)info.st_size);
    }
    BqPacket request, response;
    u64 cursor = 0;
    if (error == BQ_OK)
    {
        bq_packet(&request, BQ_OP_NATIVE_BEGIN, 1, body, 72);
        error = bq_transport_request(socket_path, &request, &response);
        if (error == BQ_OK && !bq_public_response_valid(&request, &response)) error = BQ_BAD_REQUEST;
        if (error == BQ_OK) cursor = bq_u64(response.bytes + BQ_CONTROL_HEADER + 4);
    }
    for (u64 sequence = 2; error == BQ_OK && cursor < (u64)info.st_size; sequence += 1)
    {
        u32 count = (u64)info.st_size - cursor < BQ_CONTROL_BODY - 80 ? (u32)((u64)info.st_size - cursor) : BQ_CONTROL_BODY - 80;
        bq_put64(body + 72, cursor);
        if (pread(file, body + 80, count, (off_t)cursor) != count) error = BQ_IO;
        if (error == BQ_OK)
        {
            bq_packet(&request, BQ_OP_NATIVE_WRITE, sequence, body, 80 + count);
            error = bq_transport_request(socket_path, &request, &response);
            if (error == BQ_OK && (!bq_public_response_valid(&request, &response) ||
                bq_u64(response.bytes + BQ_CONTROL_HEADER + 4) != cursor + count)) error = BQ_BAD_REQUEST;
            if (error == BQ_OK) cursor += count;
        }
    }
    if (error == BQ_OK)
    {
        bq_packet(&request, BQ_OP_NATIVE_FINISH, UINT64_MAX, body, 72);
        error = bq_transport_request(socket_path, &request, &response);
        if (error == BQ_OK && !bq_public_response_valid(&request, &response)) error = BQ_BAD_REQUEST;
        if (error == BQ_OK && fprintf(output, "program-manifest-sha256=%.64s program-bytes=%" PRIu64 "\n",
            response.bytes + BQ_CONTROL_HEADER + 12, (uint64_t)cursor) < 0) error = BQ_IO;
    }
    if (file >= 0 && close(file) != 0 && error == BQ_OK) error = BQ_IO;
    return error;
}
#else
BUSTER_GLOBAL_LOCAL BqError bq_native_client_upload(char const* socket, char const* filename, FILE* output)
{
    (void)socket; (void)filename; (void)output;
    return BQ_UNSUPPORTED;
}
#endif
