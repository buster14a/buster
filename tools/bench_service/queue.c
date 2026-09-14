/* queue.h describes ownership. This translation unit is shared verbatim by
 * the CLI and its native tests; injected failures go through the real writer.
 */
#include "queue.h"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#endif

u32 bq_u32(u8 const* bytes)
{
    u32 value = 0;
    for (u32 i = 0; i < 4; i += 1)
    {
        value |= (u32)bytes[i] << (i * 8);
    }
    return value;
}

u64 bq_u64(u8 const* bytes)
{
    u64 value = 0;
    for (u32 i = 0; i < 8; i += 1)
    {
        value |= (u64)bytes[i] << (i * 8);
    }
    return value;
}

void bq_put32(u8* bytes, u32 value)
{
    for (u32 i = 0; i < 4; i += 1)
    {
        bytes[i] = (u8)(value >> (i * 8));
    }
}

void bq_put64(u8* bytes, u64 value)
{
    for (u32 i = 0; i < 8; i += 1)
    {
        bytes[i] = (u8)(value >> (i * 8));
    }
}

String8 bq_field(BqRequest const* request, u32 index)
{
    String8 result = {0};
    u32 offset = 0;
    bool ok = request->size <= BQ_REQUEST_CAP && index < BQ_FIELD_COUNT;
    for (u32 i = 0; ok && i <= index; i += 1)
    {
        ok = request->size - offset >= 4;
        if (ok)
        {
            u32 size = bq_u32(request->bytes + offset);
            offset += 4;
            ok = size <= request->size - offset;
            if (ok)
            {
                if (i == index)
                {
                    result = (String8){(char8*)request->bytes + offset, size};
                }
                offset += size;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_name(String8 value, u64 capacity)
{
    bool ok = value.length > 0 && value.length <= capacity;
    for (u64 i = 0; ok && i < value.length; i += 1)
    {
        u8 c = (u8)value.pointer[i];
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_source_identity(String8 value)
{
    bool ok = value.length == 40 || value.length == 64;
    for (u64 i = 0; ok && i < value.length; i += 1)
    {
        u8 c = (u8)value.pointer[i];
        ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return ok;
}

bool bq_recipe_fake(BqRequest const* request)
{
    String8 recipe = bq_field(request, 2);
    bool result = string_equal(recipe, S8("fake-success-v1")) || string_equal(recipe, S8("fake-failure-v1"));
    return result;
}

bool bq_recipe_real(BqRequest const* request)
{
    bool result = string_equal(bq_field(request, 2), S8("validate-buster-v1"));
    return result;
}

bool bq_request_valid(BqRequest const* request)
{
    String8 principal = bq_field(request, 0);
    String8 key = bq_field(request, 1);
    String8 recipe = bq_field(request, 2);
    String8 base = bq_field(request, 3);
    String8 candidate = bq_field(request, 4);
    bool ok = bq_name(principal, 32) && bq_name(key, 64) &&
              (string_equal(recipe, S8("fake-success-v1")) || string_equal(recipe, S8("fake-failure-v1")) ||
               string_equal(recipe, S8("validate-buster-v1"))) &&
              bq_source_identity(base) && bq_source_identity(candidate) && base.length == candidate.length &&
              principal.length + key.length + recipe.length + base.length + candidate.length + 20 == request->size;
    return ok;
}

BqError bq_request_make(String8 const fields[BQ_FIELD_COUNT], BqRequest* request)
{
    *request = (BqRequest){0};
    BqError error = BQ_OK;
    for (u32 i = 0; error == BQ_OK && i < BQ_FIELD_COUNT; i += 1)
    {
        if (fields[i].length > BQ_REQUEST_CAP - request->size - 4)
        {
            error = BQ_BAD_REQUEST;
        }
        else
        {
            bq_put32(request->bytes + request->size, (u32)fields[i].length);
            request->size += 4;
            if (fields[i].length)
            {
                memcpy(request->bytes + request->size, fields[i].pointer, (size_t)fields[i].length);
            }
            request->size += (u32)fields[i].length;
            if (i + 1 < BQ_FIELD_COUNT && request->size > BQ_REQUEST_CAP - 4)
            {
                error = BQ_BAD_REQUEST;
            }
        }
    }
    if (error == BQ_OK && !bq_request_valid(request))
    {
        error = BQ_BAD_REQUEST;
    }
    return error;
}

BqJob* bq_job(BqState* state, u64 id)
{
    BqJob* result = NULL;
    for (u32 i = 0; i < state->job_count && !result; i += 1)
    {
        if (state->jobs[i].id == id)
        {
            result = state->jobs + i;
        }
    }
    return result;
}

u32 bq_pending(BqState const* state)
{
    u32 count = 0;
    for (u32 i = 0; i < state->job_count; i += 1)
    {
        count += state->jobs[i].phase != BQ_FINISHED;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL bool bq_same_key(BqRequest const* a, BqRequest const* b)
{
    bool equal = string_equal(bq_field(a, 0), bq_field(b, 0)) && string_equal(bq_field(a, 1), bq_field(b, 1));
    return equal;
}

/* Event validation is shared by tentative append and replay. No in-memory
 * mutation becomes visible to a caller until the corresponding fsync succeeds. */
BUSTER_GLOBAL_LOCAL BqError bq_apply(BqState* state, BqRecordKind kind, u64 sequence, u8 const* body, u32 size)
{
    BqError error = BQ_OK;
    BqJob* job = NULL;
    if (sequence != state->sequence + 1 || state->event_count == BQ_EVENT_CAP || kind < BQ_SUBMIT || kind > BQ_RECONCILE)
    {
        error = BQ_INVALID_TRANSITION;
    }
    else if (kind == BQ_SUBMIT)
    {
        BqRequest request = {0};
        if (size > BQ_REQUEST_CAP)
        {
            error = BQ_BAD_REQUEST;
        }
        else
        {
            request.size = size;
            memcpy(request.bytes, body, size);
            if (!bq_request_valid(&request))
            {
                error = BQ_BAD_REQUEST;
            }
            for (u32 i = 0; error == BQ_OK && i < state->job_count; i += 1)
            {
                if (bq_same_key(&request, &state->jobs[i].request))
                {
                    error = BQ_CONFLICT;
                }
            }
            if (error == BQ_OK && (state->job_count == BQ_JOB_CAP || bq_pending(state) == BQ_PENDING_CAP))
            {
                error = BQ_FULL;
            }
            if (error == BQ_OK)
            {
                job = state->jobs + state->job_count;
                state->job_count += 1;
                *job = (BqJob){.id = sequence, .request = request, .validity = BQ_NOT_EVALUATED};
                Sha256 hash;
                sha256_init(&hash);
                sha256_add(&hash, "BQ-request-v1", 13);
                sha256_add(&hash, body, size);
                sha256_finish_hex(&hash, job->digest);
            }
        }
    }
    else
    {
        u32 expected_size = kind == BQ_CANCEL ? 8 : kind == BQ_ADVANCE ? 24 : 16;
        if (size != expected_size)
        {
            error = BQ_BAD_REQUEST;
        }
        else
        {
            job = bq_job(state, bq_u64(body));
            if (!job)
            {
                error = BQ_NOT_FOUND;
            }
            else if (kind == BQ_RESERVE)
            {
                u64 first = 0;
                for (u32 i = 0; i < state->job_count && !first; i += 1)
                {
                    if (state->jobs[i].phase == BQ_QUEUED)
                    {
                        first = state->jobs[i].id;
                    }
                }
                if (state->active_id || job->id != first || bq_u64(body + 8) != sequence)
                {
                    error = BQ_INVALID_TRANSITION;
                }
                else
                {
                    state->active_id = job->id;
                    job->token = sequence;
                    job->phase = BQ_RESERVED;
                }
            }
            else if (kind == BQ_CANCEL)
            {
                if (job->phase >= BQ_FINALIZING || job->cancel_requested)
                {
                    error = BQ_INVALID_TRANSITION;
                }
                else
                {
                    job->cancel_requested = true;
                    if (job->phase == BQ_QUEUED)
                    {
                        job->phase = BQ_FINISHED;
                        job->outcome = BQ_CANCELLED;
                    }
                }
            }
            else if (state->active_id != job->id || bq_u64(body + 8) != job->token || !job->token)
            {
                error = BQ_INVALID_TRANSITION;
            }
            else if (kind == BQ_RECONCILE)
            {
                job->outcome = job->cancel_requested ? BQ_CANCELLED : job->outcome != BQ_NO_OUTCOME ? job->outcome : BQ_INTERRUPTED;
                job->phase = BQ_FINISHED;
                state->active_id = 0;
            }
            else
            {
                u32 next = bq_u32(body + 16);
                u32 outcome = bq_u32(body + 20);
                BqOutcome expected = job->outcome;
                bool advance = next == (u32)job->phase + 1 && next <= BQ_FINISHED;
                bool cancel_cleanup = job->cancel_requested && job->phase < BQ_CLEANING && next == BQ_CLEANING;
                bool failure_cleanup = bq_recipe_real(&job->request) && job->phase < BQ_CLEANING && next == BQ_CLEANING && outcome == BQ_FAILED;
                if (next == BQ_FINALIZING)
                {
                    expected = string_equal(bq_field(&job->request, 2), S8("fake-success-v1")) ? BQ_SUCCEEDED : BQ_FAILED;
                }
                if (job->cancel_requested && next >= BQ_CLEANING)
                {
                    expected = BQ_CANCELLED;
                }
                if ((!advance && !cancel_cleanup && !failure_cleanup) || (!failure_cleanup && outcome != (u32)expected))
                {
                    error = BQ_INVALID_TRANSITION;
                }
                else
                {
                    job->phase = (BqPhase)next;
                    job->outcome = (BqOutcome)outcome;
                    if (job->phase == BQ_FINISHED)
                    {
                        state->active_id = 0;
                    }
                }
            }
        }
    }
    if (error == BQ_OK)
    {
        state->sequence = sequence;
        state->events[state->event_count] = (BqEvent){sequence, job->id, kind, job->phase, job->outcome};
        state->event_count += 1;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_digest(void const* bytes, u32 size, char8 digest[SHA256_HEX_CAPACITY])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, size);
    sha256_finish_hex(&hash, digest);
}

BUSTER_GLOBAL_LOCAL void bq_header_digest(u8 const header[BQ_HEADER_SIZE], char8 digest[SHA256_HEX_CAPACITY])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, header, 32);
    sha256_add(&hash, header + 96, 64);
    sha256_finish_hex(&hash, digest);
}

BUSTER_GLOBAL_LOCAL void bq_frame(u8 frame[BQ_RECORD_CAP], BqRecordKind kind, u64 sequence, u8 const* body, u32 size)
{
    memset(frame, 0, BQ_HEADER_SIZE);
    memcpy(frame, "BQJNL001", 8);
    bq_put32(frame + 8, BQ_SCHEMA);
    bq_put32(frame + 12, (u32)kind);
    bq_put32(frame + 16, size);
    bq_put64(frame + 24, sequence);
    char8 digest[SHA256_HEX_CAPACITY];
    bq_digest(body, size, digest);
    memcpy(frame + 96, digest, 64);
    bq_header_digest(frame, digest);
    memcpy(frame + 32, digest, 64);
    memcpy(frame + BQ_HEADER_SIZE, body, size);
}

#ifndef _WIN32
BUSTER_GLOBAL_LOCAL bool bq_read(int fd, u8* bytes, u32 size, u64 offset)
{
    u32 done = 0;
    bool ok = true;
    while (ok && done < size)
    {
        ssize_t count = pread(fd, bytes + done, size - done, (off_t)(offset + done));
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        ok = count > 0;
        if (ok)
        {
            done += (u32)count;
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_write(BqQueue* queue, u8 const* bytes, u32 size)
{
    u32 done = 0;
    bool ok = true;
    while (ok && done < size)
    {
        u32 count = size - done;
        if (queue->fault.write_chunk && count > queue->fault.write_chunk)
        {
            count = queue->fault.write_chunk;
        }
        if (queue->fault.fail_write_at)
        {
            u32 limit = queue->fault.fail_write_at - 1;
            if (done >= limit)
            {
                ok = false;
            }
            else if (count > limit - done)
            {
                count = limit - done;
            }
        }
        if (ok)
        {
            ssize_t written = pwrite(queue->journal_fd, bytes + done, count, (off_t)(queue->bytes + done));
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            ok = written > 0;
            if (ok)
            {
                done += (u32)written;
            }
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_replay(BqQueue* queue)
{
    struct stat info;
    BqError error = BQ_OK;
    u64 size = 0;
    u64 offset = 0;
    if (fstat(queue->journal_fd, &info) != 0 || info.st_size < 0)
    {
        error = BQ_IO;
    }
    else
    {
        size = (u64)info.st_size;
        if (size > (u64)BQ_EVENT_CAP * BQ_RECORD_CAP)
        {
            error = BQ_CORRUPT;
        }
    }
    while (error == BQ_OK && offset < size)
    {
        u64 remaining = size - offset;
        if (remaining < BQ_HEADER_SIZE)
        {
            break;
        }
        u8 frame[BQ_RECORD_CAP];
        char8 digest[SHA256_HEX_CAPACITY];
        if (!bq_read(queue->journal_fd, frame, BQ_HEADER_SIZE, offset))
        {
            error = BQ_IO;
        }
        else
        {
            bq_header_digest(frame, digest);
            u32 length = bq_u32(frame + 16);
            u32 kind = bq_u32(frame + 12);
            u64 sequence = bq_u64(frame + 24);
            if (memcmp(frame, "BQJNL001", 8) || bq_u32(frame + 8) != BQ_SCHEMA || bq_u32(frame + 20) ||
                length > BQ_REQUEST_CAP || kind < BQ_SUBMIT || kind > BQ_RECONCILE ||
                sequence != queue->state.sequence + 1 || memcmp(frame + 32, digest, 64))
            {
                error = BQ_CORRUPT;
            }
            else if (remaining - BQ_HEADER_SIZE < length)
            {
                break;
            }
            else if (!bq_read(queue->journal_fd, frame + BQ_HEADER_SIZE, length, offset + BQ_HEADER_SIZE))
            {
                error = BQ_IO;
            }
            else
            {
                bq_digest(frame + BQ_HEADER_SIZE, length, digest);
                if (memcmp(frame + 96, digest, 64) ||
                    bq_apply(&queue->state, (BqRecordKind)kind, sequence, frame + BQ_HEADER_SIZE, length) != BQ_OK)
                {
                    error = BQ_CORRUPT;
                }
                else
                {
                    offset += BQ_HEADER_SIZE + length;
                }
            }
        }
    }
    if (error == BQ_OK && offset < size)
    {
        if (ftruncate(queue->journal_fd, (off_t)offset) != 0 || fsync(queue->journal_fd) != 0)
        {
            error = BQ_IO;
        }
        else
        {
            queue->recovered_tail_bytes = size - offset;
        }
    }
    queue->bytes = offset;
    queue->needs_reconciliation = queue->state.active_id != 0;
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_private_regular(int fd)
{
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              info.st_uid == geteuid() && (info.st_mode & 077) == 0;
    return ok;
}
#endif

void bq_close(BqQueue* queue)
{
#ifndef _WIN32
    if (queue->journal_fd >= 0)
    {
        close(queue->journal_fd);
    }
    if (queue->lock_fd >= 0)
    {
        close(queue->lock_fd);
    }
    if (queue->directory_fd >= 0)
    {
        close(queue->directory_fd);
    }
#endif
    queue->journal_fd = queue->lock_fd = queue->directory_fd = -1;
    queue->poisoned = true;
}

BqError bq_open(BqQueue* queue, char const* existing_private_directory)
{
    *queue = (BqQueue){.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqError error = BQ_UNSUPPORTED;
#ifndef _WIN32
    error = BQ_IO;
    queue->directory_fd = open(existing_private_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    if (queue->directory_fd >= 0 && fstat(queue->directory_fd, &info) == 0 && S_ISDIR(info.st_mode) &&
        info.st_uid == geteuid() && (info.st_mode & 077) == 0)
    {
        queue->lock_fd = openat(queue->directory_fd, "writer.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (queue->lock_fd >= 0 && bq_private_regular(queue->lock_fd))
        {
            if (flock(queue->lock_fd, LOCK_EX | LOCK_NB) != 0)
            {
                error = errno == EWOULDBLOCK || errno == EAGAIN ? BQ_BUSY : BQ_IO;
            }
            else
            {
                queue->journal_fd = openat(queue->directory_fd, "journal", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
                /* Sync every open, including an empty journal left by a prior
                 * creator crash. The parent directory must already be durable. */
                if (queue->journal_fd >= 0 && bq_private_regular(queue->journal_fd) &&
                    fsync(queue->lock_fd) == 0 && fsync(queue->journal_fd) == 0 && fsync(queue->directory_fd) == 0)
                {
                    error = bq_replay(queue);
                }
            }
        }
    }
#else
    (void)existing_private_directory;
#endif
    if (error != BQ_OK)
    {
        queue->poisoned = true;
        bq_close(queue);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_append(BqQueue* queue, BqRecordKind kind, u8 const* body, u32 size)
{
    BqError error = queue->poisoned || queue->journal_fd < 0 ? BQ_IO : BQ_OK;
    BqState next = queue->state;
    if (error == BQ_OK)
    {
        error = bq_apply(&next, kind, next.sequence + 1, body, size);
    }
    if (error == BQ_OK)
    {
        u8 frame[BQ_RECORD_CAP];
        bq_frame(frame, kind, next.sequence, body, size);
#ifndef _WIN32
        if (!bq_write(queue, frame, BQ_HEADER_SIZE + size) || queue->fault.before_sync || queue->fault.sync_error ||
            fsync(queue->journal_fd) != 0 || queue->fault.after_sync)
        {
            error = BQ_IO;
            queue->poisoned = true;
        }
        else
        {
            queue->state = next;
            queue->bytes += BQ_HEADER_SIZE + size;
        }
#else
        error = BQ_UNSUPPORTED;
#endif
    }
    return error;
}

BqError bq_submit(BqQueue* queue, BqRequest const* request, u64* id)
{
    *id = 0;
    BqError error = queue->poisoned ? BQ_IO : bq_request_valid(request) ? BQ_OK : BQ_BAD_REQUEST;
    for (u32 i = 0; error == BQ_OK && !*id && i < queue->state.job_count; i += 1)
    {
        BqJob* job = queue->state.jobs + i;
        if (bq_same_key(request, &job->request))
        {
            if (request->size == job->request.size && !memcmp(request->bytes, job->request.bytes, request->size))
            {
                *id = job->id;
            }
            else
            {
                error = BQ_CONFLICT;
            }
        }
    }
    if (error == BQ_OK && !*id)
    {
        error = bq_append(queue, BQ_SUBMIT, request->bytes, request->size);
        if (error == BQ_OK)
        {
            *id = queue->state.sequence;
        }
    }
    return error;
}

BqError bq_reserve(BqQueue* queue, u64* id, u64* token)
{
    *id = 0;
    *token = 0;
    BqError error = queue->poisoned ? BQ_IO : queue->needs_reconciliation ? BQ_RECONCILIATION_REQUIRED :
                    queue->state.active_id ? BQ_BUSY : BQ_NOT_FOUND;
    if (error == BQ_NOT_FOUND)
    {
        for (u32 i = 0; error == BQ_NOT_FOUND && i < queue->state.job_count; i += 1)
        {
            if (queue->state.jobs[i].phase == BQ_QUEUED)
            {
                u8 body[16];
                u64 selected = queue->state.jobs[i].id;
                u64 selected_token = queue->state.sequence + 1;
                bq_put64(body, selected);
                bq_put64(body + 8, selected_token);
                error = bq_append(queue, BQ_RESERVE, body, sizeof(body));
                if (error == BQ_OK)
                {
                    *id = selected;
                    *token = selected_token;
                }
            }
        }
    }
    return error;
}

BqError bq_cancel(BqQueue* queue, u64 id)
{
    BqJob* job = bq_job(&queue->state, id);
    BqError error = queue->poisoned ? BQ_IO : !job ? BQ_NOT_FOUND : BQ_OK;
    if (error == BQ_OK && job->phase < BQ_FINALIZING && !job->cancel_requested)
    {
        u8 body[8];
        bq_put64(body, id);
        error = bq_append(queue, BQ_CANCEL, body, sizeof(body));
    }
    return error;
}

BqError bq_fake_step(BqQueue* queue, u64 id, u64 token)
{
    BqJob* job = bq_job(&queue->state, id);
    BqError error = queue->poisoned ? BQ_IO : queue->needs_reconciliation ? BQ_RECONCILIATION_REQUIRED :
                    !job ? BQ_NOT_FOUND : !bq_recipe_fake(&job->request) ? BQ_UNSUPPORTED :
                    job->id != queue->state.active_id || job->token != token ? BQ_INVALID_TRANSITION : BQ_OK;
    if (error == BQ_OK)
    {
        BqPhase next = job->cancel_requested && job->phase < BQ_CLEANING ? BQ_CLEANING : (BqPhase)(job->phase + 1);
        BqOutcome outcome = job->outcome;
        if (next == BQ_FINALIZING)
        {
            outcome = string_equal(bq_field(&job->request, 2), S8("fake-success-v1")) ? BQ_SUCCEEDED : BQ_FAILED;
        }
        if (job->cancel_requested && next >= BQ_CLEANING)
        {
            outcome = BQ_CANCELLED;
        }
        u8 body[24];
        bq_put64(body, id);
        bq_put64(body + 8, token);
        bq_put32(body + 16, (u32)next);
        bq_put32(body + 20, (u32)outcome);
        error = bq_append(queue, BQ_ADVANCE, body, sizeof(body));
    }
    return error;
}

BqError bq_fake_run(BqQueue* queue, u64* id)
{
    u64 token = 0;
    BqError error = BQ_NOT_FOUND;
    for (u32 i = 0; i < queue->state.job_count && error == BQ_NOT_FOUND; i += 1)
    {
        if (queue->state.jobs[i].phase == BQ_QUEUED)
        {
            error = bq_recipe_fake(&queue->state.jobs[i].request) ? bq_reserve(queue, id, &token) : BQ_UNSUPPORTED;
        }
    }
    for (u32 step = 0; error == BQ_OK && queue->state.active_id && step < BQ_FINISHED; step += 1)
    {
        error = bq_fake_step(queue, *id, token);
    }
    return error;
}

BqError bq_fake_reconcile(BqQueue* queue, u64 id, u64 token)
{
    BqJob* job = bq_job(&queue->state, id);
    BqError error = queue->poisoned ? BQ_IO : !queue->needs_reconciliation ? BQ_INVALID_TRANSITION :
                    !job ? BQ_NOT_FOUND : !bq_recipe_fake(&job->request) ? BQ_UNSUPPORTED : BQ_OK;
    if (error == BQ_OK)
    {
        /* Valid ONLY for fake recipes, which can never spawn an external
         * worker. A real executor needs boot/unit/lease reconciliation. */
        u8 body[16];
        bq_put64(body, id);
        bq_put64(body + 8, token);
        error = bq_append(queue, BQ_RECONCILE, body, sizeof(body));
        if (error == BQ_OK)
        {
            queue->needs_reconciliation = false;
        }
    }
    return error;
}

char const* bq_error_name(BqError error)
{
    char const* names[] = {"ok", "bad-request", "conflicting-key", "queue-full", "busy", "io-uncertain",
                           "corrupt-journal", "reconciliation-required", "not-found", "unsupported", "invalid-transition",
                           "recipe-mismatch", "source-mismatch", "workspace-mismatch", "cleanup-failed", "configuration-mismatch"};
    char const* result = (u32)error < sizeof(names) / sizeof(names[0]) ? names[error] : "unknown-error";
    return result;
}
