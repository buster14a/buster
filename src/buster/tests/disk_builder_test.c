#include <buster/tests/disk_builder_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/apps/disk_builder_verification.h>
#include <buster/lib/arena.h>
#include <buster/lib/file.h>
#include <buster/lib/os_internal.h>
#include <stddef.h>
#include <string.h>

BUSTER_GLOBAL_LOCAL void disk_builder_test_make_mbr(u8* disk, u32 size_in_lba)
{
    memset(disk, 0, DISK_BUILDER_SECTOR_SIZE);
    ProtectiveMBR* mbr = (ProtectiveMBR*)disk;
    disk_builder_mbr_write_u24_le(mbr->partition_records[0].starting_chs, DISK_BUILDER_SECTOR_SIZE);
    mbr->partition_records[0].os_type = 0xee;
    disk_builder_mbr_write_u24_le(mbr->partition_records[0].ending_chs, 0xffffff);
    disk_builder_mbr_write_u32_le(mbr->partition_records[0].starting_lba, 1);
    disk_builder_mbr_write_u32_le(mbr->partition_records[0].size_in_lba, size_in_lba);
    mbr->signature[0] = 0x55;
    mbr->signature[1] = 0xaa;
}

UnitTestResult disk_builder_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 const one_mib = UINT64_C(1024) * 1024;
    u64 const sixty_four_mib = DISK_BUILDER_IMAGE_SIZE;
    u64 const oversized_size = sixty_four_mib + DISK_BUILDER_SECTOR_SIZE;
    u8* disk = (u8*)arena_allocate_bytes(arguments->arena, oversized_size, DISK_BUILDER_SECTOR_SIZE);
    BUSTER_TEST(arguments, disk != 0);

    // The MBR length calculation follows the builder's count of sectors after
    // LBA 0, including its u32 saturation. CHS's 24-bit sentinel is a separate
    // field and does not cap this length.
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(one_mib) == 2047);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(sixty_four_mib) == 0x1ffff);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(0) == 0);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(((u64)0xffffff + 1) * DISK_BUILDER_SECTOR_SIZE) == 0xffffff);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(((u64)0x1000000 + 1) * DISK_BUILDER_SECTOR_SIZE) == 0x1000000);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(((u64)UINT32_MAX) * DISK_BUILDER_SECTOR_SIZE) == UINT32_MAX - 1);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(((u64)UINT32_MAX + 1) * DISK_BUILDER_SECTOR_SIZE) == UINT32_MAX);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(((u64)UINT32_MAX + 2) * DISK_BUILDER_SECTOR_SIZE) == UINT32_MAX);
    BUSTER_TEST(arguments, disk_builder_protective_mbr_size_in_lba(UINT64_MAX) == UINT32_MAX);

    BUSTER_TEST(arguments, verify_disk(0, 0) == VERIFICATION_ERROR_DISK_EMPTY);
    if (disk != 0)
    {
        ProtectiveMBR* mbr = (ProtectiveMBR*)disk;

        // Test the real 64 MiB allocation and builder-derived MBR length.
        disk_builder_test_make_mbr(disk, 0x1ffff);
        u8 const expected_first_record[16] = {
            0, 0, 2, 0, 0xee, 0xff, 0xff, 0xff, 1, 0, 0, 0, 0xff, 0xff, 1, 0,
        };
        BUSTER_TEST(arguments, memcmp(disk + offsetof(ProtectiveMBR, partition_records), expected_first_record,
                                      sizeof(expected_first_record)) == 0);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_DISK_SUCCESS);

        // The old constant is too long for this extent, and either adjacent
        // sector count is inconsistent with the supplied capacity.
        disk_builder_mbr_write_u32_le(mbr->partition_records[0].size_in_lba, 0xffffff);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA);
        disk_builder_mbr_write_u32_le(mbr->partition_records[0].size_in_lba, 0x1fffe);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA);
        disk_builder_mbr_write_u32_le(mbr->partition_records[0].size_in_lba, 0x20000);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA);

        // A second supported geometry prevents a hard-coded 64 MiB exception.
        disk_builder_test_make_mbr(disk, 2047);
        BUSTER_TEST(arguments, verify_disk(disk, one_mib) == VERIFICATION_ERROR_DISK_SUCCESS);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        BUSTER_TEST(arguments, verify_disk(disk, 511) == VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED);

        // Earlier MBR checks keep their order and their specific diagnoses.
        disk_builder_test_make_mbr(disk, 0x1ffff);
        disk_builder_mbr_write_u32_le(mbr->unique_mbr_disk_signature, 1);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->unknown[0] = 1;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_UNKNOWN_NOT_ZERO);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->partition_records[1].os_type = 1;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_MBR_ZERO_PARTITIONS_NOT_ZEROED);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->signature[0] = 0;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_MBR_BAD_SIGNATURE);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->partition_records[0].boot_indicator = 1;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BOOT_INDICATOR_NOT_ZERO);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        disk_builder_mbr_write_u24_le(mbr->partition_records[0].starting_chs, 0x201);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->partition_records[0].os_type = 0x83;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        disk_builder_mbr_write_u24_le(mbr->partition_records[0].ending_chs, 0xfffffe);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        disk_builder_mbr_write_u32_le(mbr->partition_records[0].starting_lba, 2);
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA);
    }
#if !BUSTER_ANDROID && !BUSTER_IOS
    if (disk != 0)
    {
        Arena* arena = arguments->arena;
        memset(disk, 0x5a, oversized_size);
        String8 missing_path = buster_test_temporary_path(arena, S8("disk-builder-missing"), S8(".img"));
        String8 empty_path = buster_test_temporary_path(arena, S8("disk-builder-empty"), S8(".img"));
        String8 short_path = buster_test_temporary_path(arena, S8("disk-builder-short"), S8(".img"));
        String8 unaligned_path = buster_test_temporary_path(arena, S8("disk-builder-unaligned"), S8(".img"));
        String8 exact_path = buster_test_temporary_path(arena, S8("disk-builder-exact"), S8(".img"));
        String8 sentinel_path = buster_test_temporary_path(arena, S8("disk-builder-output-sentinel"), S8(".img"));
        String8 valid_output_path = buster_test_temporary_path(arena, S8("disk-builder-valid-output"), S8(".img"));
        u8 sentinel_bytes[] = {'k', 'e', 'e', 'p', 0xff};
        bool sentinel_created = file_write(sentinel_path, (ByteSlice){.pointer = sentinel_bytes, .length = sizeof(sentinel_bytes)});
        BUSTER_TEST(arguments, sentinel_created);

        u64 mark = arena->position;
        DiskBuilderReferenceResult missing = disk_builder_reference_read(arena, missing_path);
        BUSTER_TEST(arguments, missing.status == DISK_BUILDER_REFERENCE_OPEN_FAILED && missing.error != 0 &&
                                  !missing.bytes.pointer && arena->position == mark);
        bool match = true;
        bool missing_published = disk_builder_reference_compare_and_publish(
            missing, (ByteSlice){.pointer = disk, .length = sixty_four_mib}, sentinel_path, &match);
        FileReadResult sentinel_read = file_read_checked(arena, sentinel_path, (FileReadOptions){0});
        bool sentinel_unchanged = sentinel_read.status == OS_FILE_READ_OK && sentinel_read.bytes.pointer &&
                                  sentinel_read.bytes.length == sizeof(sentinel_bytes) &&
                                  memcmp(sentinel_read.bytes.pointer, sentinel_bytes, sizeof(sentinel_bytes)) == 0;
        BUSTER_TEST(arguments, !missing_published && !match && sentinel_unchanged);
        arena_set_position(arena, mark);

        u8 empty_byte = 0;
        bool empty_created = file_write(empty_path, (ByteSlice){.pointer = &empty_byte, .length = 0});
        BUSTER_TEST(arguments, empty_created);
        mark = arena->position;
        DiskBuilderReferenceResult empty = disk_builder_reference_read(arena, empty_path);
        BUSTER_TEST(arguments, empty.status == DISK_BUILDER_REFERENCE_SIZE_MISMATCH && !empty.observed_size &&
                                  !empty.bytes.pointer && arena->position == mark);

        bool short_created = file_write(short_path, (ByteSlice){.pointer = disk, .length = DISK_BUILDER_SECTOR_SIZE});
        BUSTER_TEST(arguments, short_created);
        mark = arena->position;
        DiskBuilderReferenceResult short_reference = disk_builder_reference_read(arena, short_path);
        BUSTER_TEST(arguments, short_reference.status == DISK_BUILDER_REFERENCE_SIZE_MISMATCH &&
                                  short_reference.observed_size == DISK_BUILDER_SECTOR_SIZE && !short_reference.bytes.pointer &&
                                  arena->position == mark);

        bool unaligned_created = file_write(unaligned_path, (ByteSlice){.pointer = disk, .length = DISK_BUILDER_SECTOR_SIZE + 1});
        BUSTER_TEST(arguments, unaligned_created);
        mark = arena->position;
        DiskBuilderReferenceResult unaligned = disk_builder_reference_read(arena, unaligned_path);
        BUSTER_TEST(arguments, unaligned.status == DISK_BUILDER_REFERENCE_SIZE_MISMATCH &&
                                  unaligned.observed_size == DISK_BUILDER_SECTOR_SIZE + 1 && !unaligned.bytes.pointer &&
                                  arena->position == mark);

        OsFileTestStep oversized_stat = {OS_FILE_TEST_STATS, OS_FILE_TEST_SIZE, oversized_size};
        os_file_test_begin(short_path, &oversized_stat, 1);
        mark = arena->position;
        DiskBuilderReferenceResult oversized = disk_builder_reference_read(arena, short_path);
        u32 oversized_steps_consumed = os_file_test_end();
        BUSTER_TEST(arguments, oversized_steps_consumed == 1 &&
                                  oversized.status == DISK_BUILDER_REFERENCE_SIZE_MISMATCH &&
                                  oversized.observed_size == oversized_size && !oversized.bytes.pointer &&
                                  arena->position == mark);

        bool exact_created = file_write(exact_path, (ByteSlice){.pointer = disk, .length = sixty_four_mib});
        BUSTER_TEST(arguments, exact_created);
        mark = arena->position;
        DiskBuilderReferenceResult exact = disk_builder_reference_read(arena, exact_path);
        bool exact_bytes_match = disk_builder_reference_is_valid(exact) &&
                                 memcmp(exact.bytes.pointer, disk, sixty_four_mib) == 0;
        BUSTER_TEST(arguments, exact_bytes_match && exact.bytes.length == sixty_four_mib);
        match = false;
        bool exact_published = disk_builder_reference_compare_and_publish(
            exact, (ByteSlice){.pointer = disk, .length = sixty_four_mib}, valid_output_path, &match);
        BUSTER_TEST(arguments, exact_published && match);
        bool output_size_matches = false;
        OsFileOpenResult output_open = os_file_open_checked(valid_output_path, (OpenFlags){0}, (OsFileAccess){.read = 1},
                                                            (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
        if (output_open.file)
        {
            FileStats output_stats = os_file_get_stats(output_open.file, (FileStatsOptions){.size = 1, .identity = 1});
            OsError output_close = os_file_close_checked(output_open.file);
            output_size_matches = output_stats.valid && output_stats.kind == OS_FILE_KIND_REGULAR &&
                                  output_stats.size == sixty_four_mib && !output_close.v;
        }
        BUSTER_TEST(arguments, output_size_matches);
        arena_set_position(arena, mark);

        OsFileTestStep read_error = {OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345};
        os_file_test_begin(exact_path, &read_error, 1);
        mark = arena->position;
        DiskBuilderReferenceResult unreadable = disk_builder_reference_read(arena, exact_path);
        u32 read_steps_consumed = os_file_test_end();
        BUSTER_TEST(arguments, read_steps_consumed == 1 &&
                                  unreadable.status == DISK_BUILDER_REFERENCE_READ_FAILED &&
                                  unreadable.error == 12345 && !unreadable.bytes.pointer && arena->position == mark);
        match = true;
        bool unreadable_published = disk_builder_reference_compare_and_publish(
            unreadable, (ByteSlice){.pointer = disk, .length = sixty_four_mib}, sentinel_path, &match);
        sentinel_read = file_read_checked(arena, sentinel_path, (FileReadOptions){0});
        sentinel_unchanged = sentinel_read.status == OS_FILE_READ_OK && sentinel_read.bytes.pointer &&
                             sentinel_read.bytes.length == sizeof(sentinel_bytes) &&
                             memcmp(sentinel_read.bytes.pointer, sentinel_bytes, sizeof(sentinel_bytes)) == 0;
        BUSTER_TEST(arguments, !unreadable_published && !match && sentinel_unchanged);
        arena_set_position(arena, mark);

        OsFileTestStep size_changed[] = {
            {OS_FILE_TEST_STATS, OS_FILE_TEST_SIZE, sixty_four_mib},
            {OS_FILE_TEST_STATS, OS_FILE_TEST_SIZE, oversized_size},
        };
        os_file_test_begin(exact_path, size_changed, BUSTER_ARRAY_LENGTH(size_changed));
        mark = arena->position;
        DiskBuilderReferenceResult changed = disk_builder_reference_read(arena, exact_path);
        u32 changed_steps_consumed = os_file_test_end();
        BUSTER_TEST(arguments, changed_steps_consumed == BUSTER_ARRAY_LENGTH(size_changed) &&
                                  changed.status == DISK_BUILDER_REFERENCE_SIZE_CHANGED &&
                                  changed.observed_size == oversized_size && changed.transferred == sixty_four_mib &&
                                  !changed.bytes.pointer && arena->position == mark);
        arena_set_position(arena, mark);
    }
#endif
    return result;
}
#endif
