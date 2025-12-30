#pragma once

#include <buster/lib.h>

#include <buster/system_headers.h>

BUSTER_IMPL THREAD_LOCAL_DECL Thread* thread;

STRUCT(ProtectionFlags)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
    u64 reserved:61;
};

STRUCT(MapFlags)
{
    u64 private:1;
    u64 anonymous:1;
    u64 no_reserve:1;
    u64 populate:1;
    u64 reserved:60;
};

BUSTER_LOCAL void string16_reverse(String16 s)
{
    let restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        let ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

BUSTER_IMPL String8 string8_format_integer_stack(String8 buffer, FormatIntegerOptions options)
{
    if (options.treat_as_signed)
    {
        BUSTER_CHECK(!options.prefix);
        BUSTER_CHECK(options.format == INTEGER_FORMAT_DECIMAL);
    }

    u64 prefix_digit_count = 2;

    String8 result = {};
    if (options.prefix)
    {
        u8 prefix_ch;
        switch (options.format)
        {
            break; case INTEGER_FORMAT_HEXADECIMAL: prefix_ch = 'x';
            break; case INTEGER_FORMAT_DECIMAL: prefix_ch = 'd';
            break; case INTEGER_FORMAT_OCTAL: prefix_ch = 'o';
            break; case INTEGER_FORMAT_BINARY: prefix_ch = 'b';
            break; default: BUSTER_UNREACHABLE();
        }
        buffer.pointer[0] = '0';
        buffer.pointer[1] = prefix_ch;
        buffer.pointer += prefix_digit_count;
        buffer.length += prefix_digit_count;
    }

    switch (options.format)
    {
        break; case INTEGER_FORMAT_HEXADECIMAL:
        {
            bool upper = false;
            result = string8_format_integer_hexadecimal(buffer, options.value, upper);
        }
        break; case INTEGER_FORMAT_DECIMAL:
        {
            result = string8_format_integer_decimal(buffer, options.value, options.treat_as_signed);
        }
        break; case INTEGER_FORMAT_OCTAL:
        {
            result = string8_format_integer_octal(buffer, options.value);
        }
        break; case INTEGER_FORMAT_BINARY:
        {
            result = string8_format_integer_binary(buffer, options.value);
        }
        break; default: BUSTER_UNREACHABLE();
    }

    if (options.prefix)
    {
        result.pointer -= prefix_digit_count;
        result.length += prefix_digit_count;
    }

    return result;
}

BUSTER_IMPL bool arena_destroy(Arena* arena, u64 count)
{
    count = count == 0 ? 1 : count;
    let reserved_size = arena->reserved_size;
    let size = reserved_size * count;
    return os_unreserve(arena, size);
}

BUSTER_IMPL void* arena_current_pointer(Arena* arena, u64 alignment)
{
    return (u8*)arena + align_forward(arena->position, alignment);
}

BUSTER_IMPL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    let aligned_offset = align_forward(arena->position, alignment);
    let aligned_size_after = aligned_offset + size;
    let arena_byte_pointer = (u8*)arena;
    let os_position = arena->os_position;

    if (BUSTER_UNLIKELY(aligned_size_after > os_position))
    {
        let target_committed_size = align_forward(aligned_size_after, arena->granularity);
        let size_to_commit = target_committed_size - os_position;
        let commit_pointer = arena_byte_pointer + os_position;
        os_commit(commit_pointer, size_to_commit, (ProtectionFlags) { .read = 1, .write = 1 }, arena_lock_pages);
        arena->os_position = target_committed_size;
    }

    let result = arena_byte_pointer + aligned_offset;
    arena->position = aligned_size_after;
    BUSTER_CHECK(arena->position <= arena->os_position);

    return result;
}

BUSTER_IMPL String16 arena_join_string16(Arena* arena, String16Slice strings, bool zero_terminate)
{
    u64 length = 0;

    for (u64 i = 0; i < strings.length; i += 1)
    {
        let string = strings.pointer[i];
        length += string.length;
    }

    let char_size = sizeof(*strings.pointer[0].pointer);

    let pointer = (typeof(strings.pointer[0].pointer))arena_allocate_bytes(arena, (length + zero_terminate) * char_size, alignof(typeof(*strings.pointer[0].pointer)));

    u64 i = 0;

    for (u64 index = 0; index < strings.length; index += 1)
    {
        let string = strings.pointer[index];
        memcpy(pointer + i, string.pointer, string_size(string));
        i += string.length;
    }

    BUSTER_CHECK(i == length);
    if (zero_terminate)
    {
        pointer[i] = 0;
    }

    return (String16){pointer, length};
}

BUSTER_IMPL String16 arena_duplicate_string16(Arena* arena, String16 str, bool zero_terminate)
{
    let pointer = arena_allocate(arena, u16, str.length + zero_terminate);
    let result = (String16){pointer, str.length};
    memcpy(pointer, str.pointer, string_size(str));
    if (zero_terminate)
    {
        pointer[str.length] = 0;
    }
    return result;
}

BUSTER_IMPL TimeDataType take_timestamp()
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec ts;
    let result = clock_gettime(CLOCK_MONOTONIC, &ts);
    BUSTER_CHECK(result == 0);
    return *(u128*)&ts;
#elif defined(_WIN32)
    LARGE_INTEGER c;
    BOOL result = QueryPerformanceCounter(&c);
    BUSTER_CHECK(result);
    return c.QuadPart;
#endif
}

BUSTER_IMPL u64 ns_between(TimeDataType start, TimeDataType end)
{
#if defined(__linux__) || defined(__APPLE__)
    let start_ts = *(struct timespec*)&start;
    let end_ts = *(struct timespec*)&end;
    let second_diff = end_ts.tv_sec - start_ts.tv_sec;
    let ns_diff = end_ts.tv_nsec - start_ts.tv_nsec;

    let result = (u64)second_diff * 1000000000ULL + (u64)ns_diff;
    return result;
#elif defined(_WIN32)
    let ns = (f64)((end - start) * 1000 * 1000 * 1000) / frequency;
    return ns;
#endif
}

BUSTER_LOCAL void thread_initialize()
{
}

#if BUSTER_USE_IO_RING
BUSTER_LOCAL bool io_ring_init(IoRing* ring, u32 entry_count)
{
    bool result = true;
#ifdef __linux__
    int io_uring_queue_creation_result = io_uring_queue_init(entry_count, &ring->linux_impl, 0);
    result &= io_uring_queue_creation_result == 0;
#else
    BUSTER_UNUSED(ring);
    BUSTER_UNUSED(entry_count);
#endif
    return result;
}

BUSTER_LOCAL IoRingSubmission io_ring_get_submission(IoRing* ring)
{
    return (IoRingSubmission) {
        .sqe = io_uring_get_sqe(&ring->linux_impl),
    };
}

BUSTER_IMPL IoRingSubmission io_ring_prepare_open(char* path, u64 user_data)
{
    let ring = &thread->ring;
    let submission = io_ring_get_submission(ring);
    submission.sqe->user_data = user_data;
    io_uring_prep_openat(submission.sqe, AT_FDCWD, path, O_RDONLY, 0);
    ring->submitted_entry_count += 1;
    return submission;
}

STRUCT(StatOptions)
{
    u32 modified_time:1;
    u32 size:1;
};

BUSTER_IMPL IoRingSubmission io_ring_prepare_stat(int fd, struct statx* statx_buffer, u64 user_data, StatOptions options)
{
    let ring = &thread->ring;
    let submission = io_ring_get_submission(ring);
    submission.sqe->user_data = user_data;
    unsigned mask = 0;
    if (options.modified_time) mask |= STATX_MTIME;
    if (options.size) mask |= STATX_SIZE;
    io_uring_prep_statx(submission.sqe, fd, "", AT_EMPTY_PATH, mask, statx_buffer);
    ring->submitted_entry_count += 1;
    return submission;
}

BUSTER_LOCAL constexpr u64 max_rw_count = 0x7ffff000;

BUSTER_IMPL IoRingSubmission io_ring_prepare_read_and_close(int fd, u8* buffer, u64 size, u64 user_data, u64 read_mask, u64 close_mask)
{
    let ring = &thread->ring;
    for (u64 i = 0; i != size;)
    {
        let offset = i;
        let read_byte_count = BUSTER_MIN(size, max_rw_count);
        let submission = io_ring_get_submission(ring);
        io_uring_prep_read(submission.sqe, fd, buffer + offset, read_byte_count, offset);
        submission.sqe->user_data = read_mask | user_data;
        submission.sqe->flags |= IOSQE_IO_LINK;
        i += read_byte_count;
        ring->submitted_entry_count += 1;
    }

    let close = io_ring_get_submission(ring);
    close.sqe->user_data = user_data | close_mask;
    io_uring_prep_close(close.sqe, fd);
    ring->submitted_entry_count += 1;

    return close;
}

BUSTER_IMPL IoRingSubmission io_ring_prepare_waitid(pid_t pid, siginfo_t* siginfo, u64 user_data)
{
    let ring = &thread->ring;
    let submission = io_ring_get_submission(ring);
    submission.sqe->user_data = user_data;
    io_uring_prep_waitid(submission.sqe, P_PID, pid, siginfo, WEXITED, 0);
    ring->submitted_entry_count += 1;
    return submission;
}

BUSTER_IMPL u32 io_ring_submit_and_wait_all()
{
    let ring = &thread->ring;
    u32 result = 0;
    let submitted_entry_count = ring->submitted_entry_count;
    BUSTER_CHECK(submitted_entry_count);
    let submit_wait_result = io_uring_submit_and_wait(&ring->linux_impl, submitted_entry_count);
    if ((submit_wait_result >= 0) & (submitted_entry_count == (u32)submit_wait_result))
    {
        result = submitted_entry_count;
        ring->submitted_entry_count = 0;
    }
    else if (program_state->input.verbose)
    {
        printf("Waiting for io_ring tasks failed\n");
    }
    return result;
}

BUSTER_IMPL u32 io_ring_submit()
{
    u32 result = 0;
    let ring = &thread->ring;
    let submitted_entry_count = ring->submitted_entry_count;
    BUSTER_CHECK(submitted_entry_count);
    let submit_result = io_uring_submit(&ring->linux_impl);
    if ((submit_result >= 0) & ((u32)submit_result == submitted_entry_count))
    {
        result = (u32)submit_result;
        ring->submitted_entry_count = 0;
    }
    return result;
}

BUSTER_IMPL IoRingCompletion io_ring_wait_completion()
{
    IoRingCompletion result = {};
    let ring = &thread->ring.linux_impl;
    struct io_uring_cqe* cqe;
    let wait_result = io_uring_wait_cqe(ring, &cqe);
    if (wait_result == 0)
    {
        result = (IoRingCompletion){
            .user_data = cqe->user_data,
            .result = cqe->res,
        };
        io_uring_cqe_seen(ring, cqe);
    }

    return result;
}

BUSTER_IMPL IoRingCompletion io_ring_peek_completion()
{
    IoRingCompletion completion = {};
    let ring = &thread->ring.linux_impl;
    struct io_uring_cqe* cqe;
    int peek_result = io_uring_peek_cqe(ring, &cqe);
    if (peek_result == 0)
    {
        completion = (IoRingCompletion){
            .user_data = cqe->user_data,
            .result = cqe->res,
        };
        io_uring_cqe_seen(ring, cqe);
    }
    else
    {
        printf("Peek failed\n");
    }

    return completion;
}
#endif

BUSTER_IMPL String8 string8_format_integer(Arena* arena, FormatIntegerOptions options, bool zero_terminate)
{
    char8 buffer[128];
    let stack_string = string8_format_integer_stack((String8){ buffer, BUSTER_ARRAY_LENGTH(buffer) }, options);
    return arena_duplicate_string8(arena, stack_string, zero_terminate);
}

#if 0
BUSTER_IMPL IntegerParsing parse_hexadecimal_vectorized(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;
    BUSTER_UNUSED(p);

    while (1)
    {
        // let ch = p[i];
        //
        // if (!is_hexadecimal(ch))
        // {
        //     break;
        // }
        //
        // i += 1;
        // value = accumulate_hexadecimal(value, ch);
        BUSTER_TRAP();
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing parse_decimal_vectorized(const char* restrict p)
{
    let zero = _mm512_set1_epi8('0');
    let nine = _mm512_set1_epi8('9');
    let chunk = _mm512_loadu_epi8(&p[0]);
    let lower_limit = _mm512_cmpge_epu8_mask(chunk, zero);
    let upper_limit = _mm512_cmple_epu8_mask(chunk, nine);
    let is = _kand_mask64(lower_limit, upper_limit);

    let digit_count = _tzcnt_u64(~_cvtmask64_u64(is));

    let digit_mask = _cvtu64_mask64((1ULL << digit_count) - 1);
    let digit2bin = _mm512_maskz_sub_epi8(digit_mask, chunk, zero);
    //let lo0 = _mm512_castsi512_si128(digit2bin);
    //let a = _mm512_cvtepu8_epi64(lo0);
    let digit_count_splat = _mm512_set1_epi8((u8)digit_count);

    let to_sub = _mm512_set_epi8(
            64, 63, 62, 61, 60, 59, 58, 57,
            56, 55, 54, 53, 52, 51, 50, 49,
            48, 47, 46, 45, 44, 43, 42, 41,
            40, 39, 38, 37, 36, 35, 34, 33,
            32, 31, 30, 29, 28, 27, 26, 25,
            24, 23, 22, 21, 20, 19, 18, 17,
            16, 15, 14, 13, 12, 11, 10, 9,
            8, 7, 6, 5, 4, 3, 2, 1);
    let ib = _mm512_maskz_sub_epi8(digit_mask, digit_count_splat, to_sub);
    let asds = _mm512_maskz_permutexvar_epi8(digit_mask, ib, digit2bin);

    let a128_0_0 = _mm512_extracti64x2_epi64(asds, 0);
    let a128_1_0 = _mm512_extracti64x2_epi64(asds, 1);

    let a128_0_1 = _mm_srli_si128(a128_0_0, 8);
    //let a128_1_1 = _mm_srli_si128(a128_1_0, 8);

    let a8_0_0 = _mm512_cvtepu8_epi64(a128_0_0);
    let a8_0_1 = _mm512_cvtepu8_epi64(a128_0_1);
    let a8_1_0 = _mm512_cvtepu8_epi64(a128_1_0);

    let powers_of_ten_0_0 = _mm512_set_epi64(
            10000000,
            1000000,
            100000,
            10000,
            1000,
            100,
            10,
            1);
    let powers_of_ten_0_1 = _mm512_set_epi64(
            1000000000000000,
            100000000000000,
            10000000000000,
            1000000000000,
            100000000000,
            10000000000,
            1000000000,
            100000000
            );
    let powers_of_ten_1_0 = _mm512_set_epi64(
            0,
            0,
            0,
            0,
            10000000000000000000ULL,
            1000000000000000000,
            100000000000000000,
            10000000000000000
            );

    let a0_0 = _mm512_mullo_epi64(a8_0_0, powers_of_ten_0_0);
    let a0_1 = _mm512_mullo_epi64(a8_0_1, powers_of_ten_0_1);
    let a1_0 = _mm512_mullo_epi64(a8_1_0, powers_of_ten_1_0);

    let add = _mm512_add_epi64(_mm512_add_epi64(a0_0, a0_1), a1_0);
    let reduce_add = _mm512_reduce_add_epi64(add);
    let value = reduce_add;

    return (IntegerParsing){ .value = value, .i = digit_count };
}

BUSTER_IMPL IntegerParsing parse_octal_vectorized(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;
    BUSTER_UNUSED(p);

    while (1)
    {
        // let chunk = _mm512_loadu_epi8(&p[i]);
        // let lower_limit = _mm512_cmpge_epu8_mask(chunk, _mm512_set1_epi8('0'));
        // let upper_limit = _mm512_cmple_epu8_mask(chunk, _mm512_set1_epi8('7'));
        // let is_octal = _kand_mask64(lower_limit, upper_limit);
        // let octal_mask = _cvtu64_mask64(_tzcnt_u64(~_cvtmask64_u64(is_octal)));

        BUSTER_TRAP();

        // if (!is_octal(ch))
        // {
        //     break;
        // }
        //
        // i += 1;
        // value = accumulate_octal(value, ch);
    }

    return (IntegerParsing) { .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing parse_binary_vectorized(const char* restrict f)
{
#if 0
    u64 value = 0;

    let chunk = _mm512_loadu_epi8(f);
    let zero = _mm512_set1_epi8('0');
    let is0 = _mm512_cmpeq_epu8_mask(chunk, zero);
    let is1 = _mm512_cmpeq_epu8_mask(chunk, _mm512_set1_epi8('1'));
    let is_binary_chunk = _kor_mask64(is0, is1);
    u64 i = _tzcnt_u64(~_cvtmask64_u64(is_binary_chunk));
    let digit2bin = _mm512_maskz_sub_epi8(is_binary_chunk, chunk, zero);
    let rotated = _mm512_permutexvar_epi8(digit2bin,
            _mm512_set_epi8(
                0, 1, 2, 3, 4, 5, 6, 7,
                8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23,
                24, 25, 26, 27, 28, 29, 30, 31,
                32, 33, 34, 35, 36, 37, 38, 39,
                40, 41, 42, 43, 44, 45, 46, 47,
                48, 49, 50, 51, 52, 53, 54, 55,
                56, 57, 58, 59, 60, 61, 62, 63
                ));
    let mask = _mm512_test_epi8_mask(rotated, rotated);
    let mask_int = _cvtmask64_u64(mask);

    return (IntegerParsing) { .value = value, .i = i };
#else
    BUSTER_UNUSED(f);
    BUSTER_TRAP();
#endif
}
#endif

BUSTER_LOCAL u64 accumulate_hexadecimal(u64 accumulator, u8 ch)
{
    u8 value;

    if (character_is_decimal(ch))
    {
        value = ch - '0';
    }
    else if (character_is_hexadecimal_alpha_upper(ch))
    {
        value = ch - 'A' + 10;
    }
    else if (character_is_hexadecimal_alpha_lower(ch))
    {
        value = ch - 'a' + 10;
    }
    else
    {
        BUSTER_UNREACHABLE();
    }

    return (accumulator * 16) + value;
}

BUSTER_LOCAL u64 accumulate_decimal(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(character_is_decimal(ch));
    return (accumulator * 10) + (ch - '0');
}

BUSTER_LOCAL u64 accumulate_octal(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(character_is_octal(ch));

    return (accumulator * 8) + (ch - '0');
}

BUSTER_LOCAL u64 accumulate_binary(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(character_is_binary(ch));

    return (accumulator * 2) + (ch - '0');
}

BUSTER_IMPL u64 parse_integer_decimal_assume_valid(String8 string)
{
    u64 value = 0;

    for (u64 i = 0; i < string.length; i += 1)
    {
        let ch = string.pointer[i];
        value = accumulate_decimal(value, ch);
    }

    return value;
}

BUSTER_IMPL IntegerParsing string8_parse_hexadecimal(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_hexadecimal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_hexadecimal(value, ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string8_parse_octal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_octal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_octal(value, ch);
    }

    return (IntegerParsing) { .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string16_parse_binary(const char16* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_binary(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_binary(value, (u8)ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL u64 next_power_of_two(u64 n)
{
    n -= 1;

    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;

    n += 1;

    return n;
}

BUSTER_IMPL String8 string8_from_pointer(const char8* pointer)
{
    return (String8){(char8*)pointer, string8_length(pointer)};
}

BUSTER_IMPL String16 string16_from_pointer(const char16* pointer)
{
    return (String16){(char16*)pointer, string16_length(pointer)};
}

BUSTER_IMPL String16 string16_from_pointer_length(const char16* pointer, u64 length)
{
    return (String16){(char16*)pointer, length};
}

BUSTER_IMPL String8 string8_from_pointers(const char8* start, const char8* end)
{
    BUSTER_CHECK(end >= start);
    let len = (u64)(end - start);
    return (String8) { (char*)start, len };
}

BUSTER_IMPL String8 string8_from_pointer_length(const char8* pointer, u64 len)
{
    return (String8) { (char8*)pointer, len };
}

BUSTER_IMPL String8 string8_from_pointer_unsigned_length(const char8* pointer, u64 len)
{
    return (String8) { (char8*)pointer, len };
}

BUSTER_IMPL String8 string8_from_pointer_start_end(const char8* pointer, u64 start, u64 end)
{
    return (String8) { (char8*)pointer + start, end - start };
}

BUSTER_IMPL String8 string8_slice_start(String8 s, u64 start)
{
    s.pointer += start;
    s.length -= start;
    return s;
}

BUSTER_IMPL String16 string16_slice_start(String16 s, u64 start)
{
    s.pointer += start;
    s.length -= start;
    return s;
}

BUSTER_IMPL String8 string8_slice(String8 s, u64 start, u64 end)
{
    s.pointer += start;
    s.length = end - start;
    return s;
}

BUSTER_IMPL String16 string16_slice(String16 s, u64 start, u64 end)
{
    s.pointer += start;
    s.length = end - start;
    return s;
}

BUSTER_IMPL bool string8_equal(String8 s1, String8 s2)
{
    return string_generic_equal(s1.pointer, s2.pointer, s1.length, s2.length, sizeof(*s1.pointer));
}

BUSTER_IMPL bool string16_equal(String16 s1, String16 s2)
{
    return string_generic_equal(s1.pointer, s2.pointer, s1.length, s2.length, sizeof(*s1.pointer));
}

BUSTER_IMPL u64 string8_first_character(String8 s, char8 ch)
{
    let result = string_no_match;

    for (u64 i = 0; i < s.length; i += 1)
    {
        if (ch == s.pointer[i])
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_IMPL u64 string8_first_ocurrence(String8 s, String8 sub)
{
    u64 result = string_no_match;

    if (sub.length && s.length >= sub.length)
    {
        u64 i = 0;

        while (true)
        {
            let s_sub = string8_slice_start(s, i);
            let index = string8_first_character(s_sub, sub.pointer[0]);
            if (index == string_no_match)
            {
                break;
            }

            let candidate_result = i + index;
            let s_sub_sub = string8_slice_start(s, candidate_result);
            if (s_sub_sub.length < sub.length)
            {
                break;
            }
            s_sub_sub = string8_slice(s_sub_sub, 0, sub.length);
            
            if (string8_equal(s_sub_sub, sub))
            {
                result = candidate_result;
                break;
            }

            i = candidate_result + 1;
        }
    }

    return result;
}

BUSTER_IMPL u64 string16_first_ocurrence(String16 s, String16 sub)
{
    u64 result = string_no_match;

    if (sub.length && s.length >= sub.length)
    {
        u64 i = 0;

        while (true)
        {
            let s_sub = string16_slice_start(s, i);
            let index = string16_first_character(s_sub, sub.pointer[0]);
            if (index == string_no_match)
            {
                break;
            }

            let candidate_result = i + index;
            let s_sub_sub = string16_slice_start(s, candidate_result);
            if (s_sub_sub.length < sub.length)
            {
                break;
            }
            s_sub_sub = string16_slice(s_sub_sub, 0, sub.length);
            
            if (string16_equal(s_sub_sub, sub))
            {
                result = candidate_result;
                break;
            }

            i = candidate_result + 1;
        }
    }

    return result;
}

BUSTER_IMPL u64 string16_first_character(String16 s, char16 ch)
{
    let result = string_no_match;

    for (u64 i = 0; i < s.length; i += 1)
    {
        if (ch == s.pointer[i])
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_IMPL u64 raw_string16_first_character(const char16* s, char16 ch)
{
    let result = string_no_match;

    for (u64 i = 0; s[i]; i += 1)
    {
        if (ch == s[i])
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_IMPL u64 string8_last_character(String8 s, char8 ch)
{
    let result = string_no_match;

    let pointer = s.pointer + s.length;

    do
    {
        pointer -= 1;
        if (*pointer == ch)
        {
            result = (u64)(pointer - s.pointer);
            break;
        }
    } while (pointer - s.pointer);

    return result;
}

BUSTER_IMPL bool string8_ends_with(String8 s, String8 ending)
{
    bool result = (ending.length <= s.length);
    if (result)
    {
        String8 last_chunk = { s.pointer + (s.length - ending.length), ending.length };
        result = string8_equal(last_chunk, ending);
    }

    return result;
}

BUSTER_IMPL bool string16_starts_with(String16 s, String16 beginning)
{
    bool result = (beginning.length <= s.length);

    if (result)
    {
        String16 first_chunk = { s.pointer , beginning.length };
        result = string16_equal(first_chunk, beginning);
    }

    return result;
}

BUSTER_IMPL void print_raw(String8 str)
{
    os_file_write(os_get_stdout(), string_to_byte_slice(str));
}

BUSTER_IMPL void argument_builder_destroy(ArgumentBuilder* restrict builder)
{
    let arena = builder->arena;
    let position = builder->arena_offset;
    arena->position = position;
}

BUSTER_IMPL u64 string16_length(const char16* s)
{
    u64 result = 0;

    if (s)
    {
        let it = s;

        while (*it)
        {
            it += 1;
        }

        result = (u64)(it - s);
    }

    return result;
}

BUSTER_IMPL u64 string32_length(const char32* s)
{
    u64 result = 0;

    if (s)
    {
        let it = s;

        while (*it)
        {
            it += 1;
        }

        result = (u64)(it - s);
    }

    return result;
}

BUSTER_IMPL String8 string16_to_string8(Arena* arena, String16 s)
{
    String8 error_result = {};
    let original_position = arena->position;
    let pointer = (char8*)arena + original_position;

    for (u64 i = 0; i < s.length; i += 1)
    {
        let ch8_pointer = arena_allocate(arena, char8, 1);
        let ch16 = s.pointer[i];
        if (ch16 <= 0x7f)
        {
            *ch8_pointer = (char8)ch16;
        }
        else
        {
            // TODO
            return error_result;
        }
    }

    String8 result = {pointer, arena->position - original_position};
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_IMPL void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...)
{
    print(S8("{S8} failed at {S8}:{S8}:{u32}\n"), format, file_path, function, line);

    if (is_debugger_present())
    {
        fail();
    }
}

BUSTER_IMPL bool lib_tests(TestArguments* restrict arguments)
{
    print(S8("Running lib tests...\n"));
    bool result = 1;
    let arena = arguments->arena;
    let position = arena->position;

    //string8_format_integer
    {
        OsString strings[] = {
            OsS("clang"),
            OsS("-c"),
            OsS("-o"),
            OsS("--help"),
        };

        let builder = argument_builder_start(arena, strings[0]);
        for (u64 i = 1; i < BUSTER_ARRAY_LENGTH(strings); i += 1)
        {
            argument_add(builder, strings[i]);
        }

        let argv = argument_builder_end(builder);
        let it = os_string_list_iterator_initialize(argv);

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(strings); i += 1)
        {
            BUSTER_OS_STRING_TEST(arguments, os_string_list_iterator_next(&it), strings[i]);
        }
    }

    // string8_format_integer
    {
        BUSTER_STRING8_TEST(arguments, S8("123"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 123, .format = INTEGER_FORMAT_DECIMAL, }, true));
        BUSTER_STRING8_TEST(arguments, S8("1000"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 1000, .format = INTEGER_FORMAT_DECIMAL }, true));
        BUSTER_STRING8_TEST(arguments, S8("12839128391258192419"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 12839128391258192419ULL, .format = INTEGER_FORMAT_DECIMAL}, true));
        BUSTER_STRING8_TEST(arguments, S8("-1"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 1, .format = INTEGER_FORMAT_DECIMAL, .treat_as_signed = true}, true));
        BUSTER_STRING8_TEST(arguments, S8("-1123123123"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 1123123123, .format = INTEGER_FORMAT_DECIMAL, .treat_as_signed = true}, true));
        BUSTER_STRING8_TEST(arguments, S8("0d0"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_DECIMAL, .prefix = true }, true));
        BUSTER_STRING8_TEST(arguments, S8("0d123"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 123, .format = INTEGER_FORMAT_DECIMAL, .prefix = true, }, true));
        BUSTER_STRING8_TEST(arguments, S8("0"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_HEXADECIMAL, }, true));
        BUSTER_STRING8_TEST(arguments, S8("af"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0xaf, .format = INTEGER_FORMAT_HEXADECIMAL, }, true));
        BUSTER_STRING8_TEST(arguments, S8("0x0"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_HEXADECIMAL, .prefix = true }, true));
        BUSTER_STRING8_TEST(arguments, S8("0x8591baefcb"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0x8591baefcb, .format = INTEGER_FORMAT_HEXADECIMAL, .prefix = true }, true));
        BUSTER_STRING8_TEST(arguments, S8("0o12557"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 012557, .format = INTEGER_FORMAT_OCTAL, .prefix = true }, true));
        BUSTER_STRING8_TEST(arguments, S8("12557"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 012557, .format = INTEGER_FORMAT_OCTAL, }, true));
        BUSTER_STRING8_TEST(arguments, S8("0b101101"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0b101101, .format = INTEGER_FORMAT_BINARY, .prefix = true }, true));
        BUSTER_STRING8_TEST(arguments, S8("101101"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0b101101, .format = INTEGER_FORMAT_BINARY, }, true));
    }

    // string8_format
    {
        ENUM_T(UnsignedFormatTestCase, u8, 
            UNSIGNED_FORMAT_TEST_CASE_DEFAULT,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL,
            UNSIGNED_FORMAT_TEST_CASE_BINARY,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_COUNT,
        );

        // u8
        {
            ENUM_T(UnsignedTestCaseId, u8,
                UNSIGNED_TEST_CASE_U8,
                UNSIGNED_TEST_CASE_U16,
                UNSIGNED_TEST_CASE_U32,
                UNSIGNED_TEST_CASE_U64,
                UNSIGNED_TEST_CASE_COUNT,
            );

            String8 format_strings[UNSIGNED_TEST_CASE_COUNT][UNSIGNED_FORMAT_TEST_CASE_COUNT] = {
                [UNSIGNED_TEST_CASE_U8] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u8}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u8:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u8:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u8:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u8:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u8:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u8:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u8:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u8:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u8:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u8:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u8:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u8:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u8:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u8:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u8:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u8:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u8:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u8:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u8:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u8:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u8:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u8:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u8:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u8:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u8:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u8:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u8:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u8:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u8:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u8:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u8:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u8:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u8:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u8:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u8:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u8:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u8:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u8:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u8:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u8:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u8:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u8:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u8:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u8:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u8:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u8:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u8:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u8:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u8:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u8:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u8:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u8:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u8:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u8:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u8:digit_group,width=[0,x],b,no_prefix}"),
                },
                [UNSIGNED_TEST_CASE_U16] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u16}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u16:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u16:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u16:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u16:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u16:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u16:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u16:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u16:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u16:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u16:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u16:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u16:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u16:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u16:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u16:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u16:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u16:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u16:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u16:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u16:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u16:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u16:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u16:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u16:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u16:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u16:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u16:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u16:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u16:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u16:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u16:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u16:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u16:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u16:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u16:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u16:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u16:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u16:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u16:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u16:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u16:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u16:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u16:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u16:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u16:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u16:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u16:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u16:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u16:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u16:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u16:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u16:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u16:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u16:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u16:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u16:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u16:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u16:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u16:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u16:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u16:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u16:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u16:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u16:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u16:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u16:digit_group,width=[0,x],b,no_prefix}"),
                },
                [UNSIGNED_TEST_CASE_U32] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u32}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u32:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u32:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u32:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u32:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u32:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u32:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u32:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u32:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u32:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u32:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u32:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u32:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u32:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u32:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u32:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u32:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u32:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u32:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u32:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u32:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u32:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u32:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u32:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u32:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u32:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u32:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u32:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u32:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u32:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u32:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u32:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u32:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u32:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u32:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u32:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u32:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u32:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u32:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u32:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u32:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u32:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u32:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u32:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u32:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u32:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u32:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u32:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u32:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u32:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u32:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u32:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u32:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u32:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u32:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u32:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u32:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u32:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u32:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u32:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u32:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u32:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u32:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u32:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u32:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u32:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u32:digit_group,width=[0,x],b,no_prefix}"),
                },
                [UNSIGNED_TEST_CASE_U64] =
                {
                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u64}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u64:d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u64:x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u64:X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u64:o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u64:b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u64:no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u64:d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u64:x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u64:X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u64:o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u64:b,no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u64:width=[ ,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u64:d,width=[ ,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u64:x,width=[ ,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u64:X,width=[ ,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u64:o,width=[ ,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u64:b,width=[ ,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u64:width=[0,2]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u64:d,width=[0,4]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u64:x,width=[0,8]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u64:X,width=[0,16]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u64:o,width=[0,32]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u64:b,width=[0,64]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u64:width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u64:d,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u64:x,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u64:X,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u64:o,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u64:b,width=[0,x]}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u64:width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u64:d,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u64:x,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u64:X,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u64:o,width=[ ,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u64:b,width=[ ,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u64:width=[0,2],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u64:d,width=[0,4],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u64:x,width=[0,8],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u64:X,width=[0,16],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u64:o,width=[0,32],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u64:b,width=[0,64],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u64:width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u64:d,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u64:x,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u64:X,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u64:o,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u64:b,width=[0,x],no_prefix}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("{u64:digit_group}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("{u64:digit_group,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("{u64:digit_group,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("{u64:digit_group,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("{u64:digit_group,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("{u64:digit_group,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("{u64:digit_group,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("{u64:digit_group,no_prefix,d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("{u64:digit_group,no_prefix,x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("{u64:digit_group,no_prefix,X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("{u64:digit_group,no_prefix,o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("{u64:digit_group,no_prefix,b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u64:digit_group,width=[0,x]}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("{u64:digit_group,width=[0,x],d}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],x}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],X}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("{u64:digit_group,width=[0,x],o}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("{u64:digit_group,width=[0,x],b}"),

                    [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("{u64:digit_group,width=[0,x],d,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],x,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],X,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("{u64:digit_group,width=[0,x],o,no_prefix}"),
                    [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("{u64:digit_group,width=[0,x],b,no_prefix}"),
                },
            };

            // 0, 1, 2, 4, 8, 16, UINT_MAX / 2, UINT_MAX

            STRUCT(UnsignedTestCase)
            {
                String8 expected_results[UNSIGNED_FORMAT_TEST_CASE_COUNT];
                u64 value;
            };

            ENUM_T(UnsignedTestCaseNumber, u8,
                UNSIGNED_TEST_CASE_NUMBER_ZERO,
                UNSIGNED_TEST_CASE_NUMBER_ONE,
                UNSIGNED_TEST_CASE_NUMBER_TWO,
                UNSIGNED_TEST_CASE_NUMBER_FOUR,
                UNSIGNED_TEST_CASE_NUMBER_EIGHT,
                UNSIGNED_TEST_CASE_NUMBER_SIXTEEN,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX,
                UNSIGNED_TEST_CASE_NUMBER_COUNT,
            );

            UnsignedTestCase cases[UNSIGNED_TEST_CASE_COUNT][UNSIGNED_TEST_CASE_NUMBER_COUNT] =
            {
                [UNSIGNED_TEST_CASE_U8] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000000"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000001"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("      10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000010"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00000100"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8(" 10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("    1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00001000"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8(" 20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("   10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b00010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("00010000"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT8_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                         1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x0000007f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x000000000000007F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000001111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b01111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8(" 1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("0000007f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("000000000000007F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000001111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("01111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("1111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0x7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b01111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("127"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("177"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("01111111"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT8_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("250"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("372"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111010"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT8_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("251"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("373"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111011"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT8_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("252"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("374"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111100"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT8_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("253"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("375"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111101"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT8_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      fe") ,
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xfe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("254"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("376"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111110"),
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT8_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                        11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x000000ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x00000000000000FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000011111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("000000ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("00000000000000FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000011111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                                  S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                                  S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                        S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                        S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                    S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                                   S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                        S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                        S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =              S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =              S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                          S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                         S8("11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                     S8("0d255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =           S8("0xFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                       S8("0o377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                      S8("0b11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =           S8("255"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =             S8("377"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =            S8("11111111"),
                        },
                    },
                },
                
// ==================== U16 ====================

                [UNSIGNED_TEST_CASE_U16] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000001")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("     4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("    8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("    8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("    10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000001000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("   16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("   16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("  10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("  10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("    20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000010000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT16_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                           77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                 111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00007fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000007FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8(" 77777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00007fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000007FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("32767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("077777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o77_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("77_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o077_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("32.767"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("077_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT16_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.530"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT16_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.531"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111011")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT16_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.532"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT16_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.533"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111101")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT16_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.534"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111110")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT16_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("    ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("            FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                          177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x0000ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000000000FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000001111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("0000ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000000000FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000001111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("65535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("177777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("65.535"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("177_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111")
                        },
                    },
                },

// ==================== U32 ====================

                [UNSIGNED_TEST_CASE_U32] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000001")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("          4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000000100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("         8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("         8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("         10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000001000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b00000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("        16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("        16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("         20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b00000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("0000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("00000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("00000000000000000000000000010000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT32_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                 1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x000000007FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000017777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000001111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b01111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 1111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("000000007FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000017777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000001111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("2147483647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("17777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("01111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("2.147.483.647"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("17_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111_11111111_11111111")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT32_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.290"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT32_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.291"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111011")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT32_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.292"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT32_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.293"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111101")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT32_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.294"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111110")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT32_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("        FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                     37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x00000000FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000037777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000011111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("00000000FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000037777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000011111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("4294967295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("37777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("11111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("4.294.967.295"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("37_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111")
                        },
                    },
                },

// ==================== U64 ====================

                [UNSIGNED_TEST_CASE_U64] =
                {
                    [UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                    {
                        .value = 0,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                               0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("00"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("0"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("0"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000000"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_ONE] =
                    {
                        .value = 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                               1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("01"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000001"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000001")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_TWO] =
                    {
                        .value = 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                              10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("02"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("2"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000002"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                    {
                        .value = 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                     4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                             100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("04"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("4"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000004"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000000100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                    {
                        .value = 8,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8(" 8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("       8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                   8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("               8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                    10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                            1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("08"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("8"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000001000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                    {
                        .value = 16,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("      10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("                              20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("                  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("                  16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("              10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("                    20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("                                                           10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("0016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("00000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("16"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("10"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("20"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("10000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("00000000000000000016"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0000000000000000000020"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("0000000000000000000000000000000000000000000000000000000000010000")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                    {
                        .value = UINT64_MAX / 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0x7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0x7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("           777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0x7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0x7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000000777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0x7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0x7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o0777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8(" 9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8(" 9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8(" 777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("9223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000000777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("09223372036854775807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7fffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("7FFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("0777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("0111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("9.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o0777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("09.223.372.036.854.775.807"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("0777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                    {
                        .value = UINT64_MAX - 5,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.610"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_772"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                    {
                        .value = UINT64_MAX - 4,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.611"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_773"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                    {
                        .value = UINT64_MAX - 3,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.612"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_774"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                    {
                        .value = UINT64_MAX - 2,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.613"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_775"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                    {
                        .value = UINT64_MAX - 1,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xfffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xfffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xfffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("fffffffffffffffe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.614"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_776"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110")
                        },
                    },
                    [UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                    {
                        .value = UINT64_MAX,
                        .expected_results =
                        {
                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                       S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                       S8("0d18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                             S8("0xffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                             S8("0xFFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                         S8("0o1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                        S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                             S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                             S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                   S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                   S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                               S8("1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                              S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                         S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                         S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =               S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =              S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                          S8("          1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                         S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                          S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                          S8("0d18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                S8("0xffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =               S8("0xFFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                           S8("0o00000000001777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                          S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                          S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                          S8("0d18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                S8("0xffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                S8("0xFFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                            S8("0o1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                           S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =               S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =     S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                 S8("1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =      S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =     S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                 S8("00000000001777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                S8("18446744073709551615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =      S8("ffffffffffffffff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =      S8("FFFFFFFFFFFFFFFF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                  S8("1777777777777777777777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                 S8("1111111111111111111111111111111111111111111111111111111111111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] =                              S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] =                              S8("0d18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] =                    S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] =                    S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] =                                S8("0o1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =                               S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] =                    S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] =          S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] =          S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] =                      S8("1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] =                 S8("0d18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] =       S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] =                   S8("0o1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =                  S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                            [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =       S8("18.446.744.073.709.551.615"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                            [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                            [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =         S8("1_777_777_777_777_777_777_777"),
                            [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =        S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")
                        },
                    },
                },
            };

            for (UnsignedTestCaseId type_i = 0; type_i < UNSIGNED_TEST_CASE_COUNT; type_i += 1)
            {
                for (UnsignedTestCaseNumber case_value_i = 0; case_value_i < UNSIGNED_TEST_CASE_NUMBER_COUNT; case_value_i += 1)
                {
                    let uint_case = &cases[type_i][case_value_i];
                    let value = uint_case->value;

                    for (UnsignedFormatTestCase case_i = 0; case_i < UNSIGNED_FORMAT_TEST_CASE_COUNT; case_i += 1)
                    {
                        let format_string = format_strings[type_i][case_i];
                        let expected_string = uint_case->expected_results[case_i];
                        let test_type = (UnsignedTestCaseId)type_i;

                        String8 result_string;
                        switch (test_type)
                        {
                            break; case UNSIGNED_TEST_CASE_U8: result_string = arena_string8_format(arena, 0, format_string, (u8)value);
                            break; case UNSIGNED_TEST_CASE_U16: result_string = arena_string8_format(arena, 0, format_string, (u16)value);
                            break; case UNSIGNED_TEST_CASE_U32: result_string = arena_string8_format(arena, 0, format_string, (u32)value);
                            break; case UNSIGNED_TEST_CASE_U64: result_string = arena_string8_format(arena, 0, format_string, (u64)value);
                            break; case UNSIGNED_TEST_CASE_COUNT: BUSTER_UNREACHABLE();
                        }

                        if (!string8_equal(result_string, expected_string))
                        {
                            BUSTER_TEST_ERROR(S8(""), (u32)0);
                        }
                    }
                }
            }
        }
    }
    
    arena->position = position;
    print(S8("Lib tests {S8}!\n"), result ? S8("passed") : S8("failed"));
    return result;
}
#endif
