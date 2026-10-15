#include <buster/apps/disk_builder_verification.h>
#include <buster/lib/file.h>
#include <stdio.h>
#include <string.h>

// MBR fields are bytes on disk, so read and write their little-endian values
// without relying on host alignment, endianness, bitfield order, or packing.
u32 disk_builder_mbr_read_u24_le(u8 const bytes[3])
{
    u32 result = (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16);
    return result;
}

u32 disk_builder_mbr_read_u32_le(u8 const bytes[4])
{
    u32 result = (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
    return result;
}

void disk_builder_mbr_write_u24_le(u8 bytes[3], u32 value)
{
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
    bytes[2] = (u8)(value >> 16);
    return;
}

void disk_builder_mbr_write_u32_le(u8 bytes[4], u32 value)
{
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
    bytes[2] = (u8)(value >> 16);
    bytes[3] = (u8)(value >> 24);
    return;
}

// The MBR length counts sectors after LBA 0. Keep the builder's u32
// saturation rule in one place; the separate CHS end field remains 0xffffff.
u32 disk_builder_protective_mbr_size_in_lba(u64 disk_size)
{
    u64 sector_count = disk_size / DISK_BUILDER_SECTOR_SIZE;
    u64 size_in_lba = sector_count ? sector_count - 1 : 0;
    u32 result = size_in_lba > UINT32_MAX ? UINT32_MAX : (u32)size_in_lba;
    return result;
}

// The three records after the protective entry must be entirely zero; the scan
// stops at the first non-zero one because that is already the whole answer.
BUSTER_GLOBAL_LOCAL bool verify_disk_reserved_partitions_zeroed(ProtectiveMBR const* mbr)
{
    MBRPartitionRecord zero_record;
    memset(&zero_record, 0, sizeof(zero_record));

    bool result = true;
    for (int i = 1; i < 4 && result; i += 1)
    {
        result = memcmp(&mbr->partition_records[i], &zero_record, sizeof(zero_record)) == 0;
    }

    return result;
}

// Reports the first thing wrong with the image. The checks are ordered so that
// each one only runs once its predecessors have vouched for the bytes it reads:
// the sector-size check guards the MBR cast, and the MBR checks guard the
// protective partition record.
VerificationError verify_disk(u8* disk, u64 disk_size)
{
    VerificationError result = VERIFICATION_ERROR_DISK_SUCCESS;
    if (disk_size == 0)
    {
        result = VERIFICATION_ERROR_DISK_EMPTY;
    }
    else if (disk_size % DISK_BUILDER_SECTOR_SIZE != 0)
    {
        result = VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED;
    }
    else
    {
        ProtectiveMBR* mbr = (ProtectiveMBR*)disk;
        MBRPartitionRecord* gpt_partition_record = &mbr->partition_records[0];

        if (disk_builder_mbr_read_u32_le(mbr->unique_mbr_disk_signature) != 0)
        {
            result = VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO;
        }
        else if (mbr->unknown[0] != 0 || mbr->unknown[1] != 0)
        {
            result = VERIFICATION_ERROR_UNKNOWN_NOT_ZERO;
        }
        else if (!verify_disk_reserved_partitions_zeroed(mbr))
        {
            result = VERIFICATION_ERROR_MBR_ZERO_PARTITIONS_NOT_ZEROED;
        }
        else if (mbr->signature[0] != 0x55 || mbr->signature[1] != 0xaa)
        {
            result = VERIFICATION_ERROR_MBR_BAD_SIGNATURE;
        }
        else if (gpt_partition_record->boot_indicator != 0)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BOOT_INDICATOR_NOT_ZERO;
        }
        else if (disk_builder_mbr_read_u24_le(gpt_partition_record->starting_chs) != 0x200)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512;
        }
        else if (gpt_partition_record->os_type != 0xee)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE;
        }
        else if (disk_builder_mbr_read_u24_le(gpt_partition_record->ending_chs) != 0xffffff)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS;
        }
        else if (disk_builder_mbr_read_u32_le(gpt_partition_record->starting_lba) != 1)
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA;
        }
        else if (disk_builder_mbr_read_u32_le(gpt_partition_record->size_in_lba) != disk_builder_protective_mbr_size_in_lba(disk_size))
        {
            result = VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA;
        }
    }

    return result;
}

DiskBuilderReferenceResult disk_builder_reference_read(Arena* arena, String8 path)
{
    DiskBuilderReferenceResult result = {.status = DISK_BUILDER_REFERENCE_INVALID_ARGUMENT};
    u64 allocation_mark = arena ? arena->position : 0;
    bool allocation_attempted = false;

    if (arena && path.pointer && path.length)
    {
        result.status = DISK_BUILDER_REFERENCE_OPEN_FAILED;
        OsFileOpenResult opened = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.read = 1}, (OsFileCreateMode){0},
                                                       (OsFileShareFlags){.read = 1});
        if (opened.file)
        {
            FileStats before = os_file_get_stats(opened.file, (FileStatsOptions){.size = 1, .identity = 1});
            if (!before.valid)
            {
                result.status = DISK_BUILDER_REFERENCE_STATS_FAILED;
                result.error = before.error.v;
            }
            else
            {
                result.observed_size = before.size;
                if (before.kind != OS_FILE_KIND_REGULAR)
                {
                    result.status = DISK_BUILDER_REFERENCE_NOT_REGULAR;
                }
                else if (before.size != DISK_BUILDER_IMAGE_SIZE)
                {
                    result.status = DISK_BUILDER_REFERENCE_SIZE_MISMATCH;
                }
                else
                {
                    allocation_attempted = true;
                    result.status = DISK_BUILDER_REFERENCE_ALLOCATION_FAILED;
                    u8* bytes = (u8*)arena_allocate_bytes(arena, DISK_BUILDER_IMAGE_SIZE, DISK_BUILDER_SECTOR_SIZE);
                    if (bytes)
                    {
                        result.status = DISK_BUILDER_REFERENCE_READ_FAILED;
                        OsFileReadResult read = os_file_read_exact(opened.file, (ByteSlice){.pointer = bytes,
                                                                                            .length = DISK_BUILDER_IMAGE_SIZE});
                        result.transferred = read.transferred;
                        result.error = read.error.v;
                        if (read.status == OS_FILE_READ_OK && read.transferred == DISK_BUILDER_IMAGE_SIZE)
                        {
                            FileStats after = os_file_get_stats(opened.file, (FileStatsOptions){.size = 1, .identity = 1});
                            result.observed_size = after.size;
                            if (!after.valid)
                            {
                                result.status = DISK_BUILDER_REFERENCE_STATS_FAILED;
                                result.error = after.error.v;
                            }
                            else if (after.kind != OS_FILE_KIND_REGULAR || after.size != DISK_BUILDER_IMAGE_SIZE)
                            {
                                result.status = DISK_BUILDER_REFERENCE_SIZE_CHANGED;
                            }
                            else
                            {
                                result.bytes = (ByteSlice){.pointer = bytes, .length = DISK_BUILDER_IMAGE_SIZE};
                                result.status = DISK_BUILDER_REFERENCE_OK;
                                result.error = 0;
                            }
                        }
                    }
                }
            }

            OsError close_error = os_file_close_checked(opened.file);
            if (close_error.v)
            {
                if (result.status == DISK_BUILDER_REFERENCE_OK)
                {
                    result.status = DISK_BUILDER_REFERENCE_CLOSE_FAILED;
                    result.error = close_error.v;
                }
                else
                {
                    result.cleanup_error = close_error.v;
                }
            }
        }
        else
        {
            result.error = opened.error.v;
        }
    }

    if (result.status != DISK_BUILDER_REFERENCE_OK)
    {
        result.bytes = (ByteSlice){0};
        if (arena && allocation_attempted)
        {
            arena_set_position(arena, allocation_mark);
        }
    }
    return result;
}

bool disk_builder_reference_is_valid(DiskBuilderReferenceResult reference)
{
    bool result = reference.status == DISK_BUILDER_REFERENCE_OK && reference.bytes.pointer &&
                 reference.bytes.length == DISK_BUILDER_IMAGE_SIZE && reference.transferred == DISK_BUILDER_IMAGE_SIZE;
    return result;
}

bool disk_builder_reference_compare_and_publish(DiskBuilderReferenceResult reference, ByteSlice image, String8 output_path,
                                                bool* match_out)
{
    bool valid = disk_builder_reference_is_valid(reference) && image.pointer && image.length == DISK_BUILDER_IMAGE_SIZE &&
                 output_path.pointer && output_path.length;
    bool matches = false;
    bool written = false;
    if (valid)
    {
        matches = true;
        u8 const* reference_bytes = (u8 const*)reference.bytes.pointer;
        u8 const* image_bytes = (u8 const*)image.pointer;
        for (u64 index = 0; index < DISK_BUILDER_IMAGE_SIZE && matches; index += 1)
        {
            if (image_bytes[index] != reference_bytes[index])
            {
                matches = false;
                printf("Failed to match character at [%llu]. Original: %x. Mine: %x\n", (unsigned long long)index,
                       reference_bytes[index], image_bytes[index]);
            }
        }
        written = file_write(output_path, image);
    }
    if (match_out) *match_out = matches;
    bool result = valid && matches && written;
    return result;
}
