#include <buster/tests/disk_builder_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/apps/disk_builder_verification.h>
#include <buster/lib/arena.h>
#include <string.h>

BUSTER_GLOBAL_LOCAL void disk_builder_test_make_mbr(u8* disk, u32 size_in_lba)
{
    memset(disk, 0, DISK_BUILDER_SECTOR_SIZE);
    ProtectiveMBR* mbr = (ProtectiveMBR*)disk;
    mbr->partition_records[0] = (MBRPartitionRecord){
        .starting_chs = DISK_BUILDER_SECTOR_SIZE & 0xffffffu,
        .os_type = 0xee,
        .ending_chs = 0xffffff,
        .starting_lba = 1,
        .size_in_lba = size_in_lba,
    };
    mbr->signature[0] = 0x55;
    mbr->signature[1] = 0xaa;
}

UnitTestResult disk_builder_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 const one_mib = UINT64_C(1024) * 1024;
    u64 const sixty_four_mib = UINT64_C(64) * one_mib;
    u8* disk = (u8*)arena_allocate_bytes(arguments->arena, sixty_four_mib, DISK_BUILDER_SECTOR_SIZE);
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
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_DISK_SUCCESS);

        // The old constant is too long for this extent, and either adjacent
        // sector count is inconsistent with the supplied capacity.
        mbr->partition_records[0].size_in_lba = 0xffffff;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA);
        mbr->partition_records[0].size_in_lba = 0x1fffe;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA);
        mbr->partition_records[0].size_in_lba = 0x20000;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA);

        // A second supported geometry prevents a hard-coded 64 MiB exception.
        disk_builder_test_make_mbr(disk, 2047);
        BUSTER_TEST(arguments, verify_disk(disk, one_mib) == VERIFICATION_ERROR_DISK_SUCCESS);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        BUSTER_TEST(arguments, verify_disk(disk, 511) == VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED);

        // Earlier MBR checks keep their order and their specific diagnoses.
        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->unique_mbr_disk_signature = 1;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->unknown = 1;
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
        mbr->partition_records[0].starting_chs = 0x201;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->partition_records[0].os_type = 0x83;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->partition_records[0].ending_chs = 0xfffffe;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS);

        disk_builder_test_make_mbr(disk, 0x1ffff);
        mbr->partition_records[0].starting_lba = 2;
        BUSTER_TEST(arguments, verify_disk(disk, sixty_four_mib) == VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA);
    }
    return result;
}
#endif
