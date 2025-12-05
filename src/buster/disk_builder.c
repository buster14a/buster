#pragma once
#include <lib.h>
#include <stdio.h>

#if BUSTER_UNITY_BUILD
#include <lib.c>
#endif

STRUCT(MBRPartitionRecord)
{
    u32 boot_indicator:8;
    u32 starting_chs:24;
    u32 os_type:8;
    u32 ending_chs:24;
    u32 starting_lba;
    u32 size_in_lba;
} BUSTER_PACKED;
static_assert(sizeof(MBRPartitionRecord) == 16);

STRUCT(ProtectiveMBR)
{
    u8 boot_code[440];
    u32 unique_mbr_disk_signature;
    u16 unknown;
    MBRPartitionRecord partition_records[4];
    u8 signature[2];
} BUSTER_PACKED;

static_assert(sizeof(ProtectiveMBR) == 512);

STRUCT(GPTHeader)
{
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved0;
    u64 header_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8 disk_guid[16];
    u64 partition_entry_lba;
    u32 partition_entry_count;
    u32 partition_entry_size;
    u32 partition_entry_crc32;
    u32 reserved1;
};

static_assert(sizeof(GPTHeader) == 96);
constexpr u64 gpt_header_size = sizeof(GPTHeader) - sizeof(u32);
static_assert(gpt_header_size == 92);

ENUM(VerificationError,
    VERIFICATION_ERROR_DISK_SUCCESS,
    VERIFICATION_ERROR_DISK_EMPTY,
    VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED,
    VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO,
    VERIFICATION_ERROR_UNKNOWN_NOT_ZERO,
    VERIFICATION_ERROR_MBR_ZERO_PARTITIONS_NOT_ZEROED,
    VERIFICATION_ERROR_MBR_BAD_SIGNATURE,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BOOT_INDICATOR_NOT_ZERO,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA,
    VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA,
);

BUSTER_LOCAL VerificationError verify_disk(u8* disk, u64 disk_size)
{
    if (disk_size == 0)
    {
        return VERIFICATION_ERROR_DISK_EMPTY;
    }

    if (disk_size % 512 != 0)
    {
        return VERIFICATION_ERROR_DISK_NOT_SECTOR_SIZED;
    }

    let mbr = (ProtectiveMBR*)disk;
    u64 error_count = 0;

    if (mbr->unique_mbr_disk_signature != 0)
    {
        return VERIFICATION_ERROR_MBR_DISK_SIGNATURE_NOT_ZERO;
    }

    if (mbr->unknown != 0)
    {
        return VERIFICATION_ERROR_UNKNOWN_NOT_ZERO;
    }

    MBRPartitionRecord zero_record;
    memset(&zero_record, 0, sizeof(zero_record));

    for (int i = 1; i < 4; i += 1)
    {
        let record = &mbr->partition_records[i];
        bool is_equal = memcmp(record, &zero_record, sizeof(*record)) == 0;
        if (!is_equal)
        {
            return VERIFICATION_ERROR_MBR_ZERO_PARTITIONS_NOT_ZEROED;
        }
    }

    if (mbr->signature[0] != 0x55 || mbr->signature[1] != 0xaa)
    {
        return VERIFICATION_ERROR_MBR_BAD_SIGNATURE;
    }

    let gpt_partition_record = &mbr->partition_records[0];

    if (gpt_partition_record->boot_indicator != 0)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BOOT_INDICATOR_NOT_ZERO;
    }

    if (gpt_partition_record->starting_chs != 0x200)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_STARTING_CHS_NOT_512;
    }

    if (gpt_partition_record->os_type != 0xee)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_OS_TYPE_NOT_PROTECTIVE;
    }

    if (gpt_partition_record->ending_chs != 0xffffff)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_ENDING_CHS;
    }

    if (gpt_partition_record->starting_lba != 1)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_STARTING_LBA;
    }

    if (gpt_partition_record->size_in_lba != 0xffffff)
    {
        return VERIFICATION_ERROR_GPT_PARTITION_RECORD_BAD_SIZE_IN_LBA;
    }

    return VERIFICATION_ERROR_DISK_SUCCESS;
}

STRUCT(FileWriter)
{
    u8* buffer;
    u64 size;
    u64 index;
};

BUSTER_LOCAL FileWriter file_writer_init(u8* pointer, u64 size)
{
    return (FileWriter){
        .buffer = pointer,
        .size = size,
        .index = 0,
    };
}

BUSTER_LOCAL void* file_allocate_bytes(FileWriter* writer, u64 size)
{
    let pointer = writer->buffer + writer->index;
    writer->index += size;
    return pointer;
}

BUSTER_LOCAL void file_pad(FileWriter* writer, u64 padding)
{
    writer->index += padding;
}

BUSTER_LOCAL void file_align(FileWriter* writer, u64 alignment)
{
    writer->index = align_forward(writer->index, alignment);
}

BUSTER_LOCAL void file_write_byte(FileWriter* writer, u8 byte)
{
    writer->buffer[writer->index] = byte;
    writer->index += 1;
}

BUSTER_LOCAL void file_write_string(FileWriter* writer, String s, bool null_terminate)
{
    memcpy(writer->buffer + writer->index, s.pointer, s.length);
    writer->index += s.length;
}

#define file_allocate(w,  T, count) (T*)file_allocate_bytes(w, sizeof(T) * count)

BUSTER_LOCAL constexpr u64 efi_part = 0x5452415020494645;

BUSTER_LOCAL u32 crc32_table[256] =
{
    0, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

BUSTER_LOCAL u32 crc32_compute(u8 *p, u64 bytelength)
{
	u32 crc = 0xffffffff;
	while (bytelength-- !=0) crc = crc32_table[((u8) crc ^ *(p++))] ^ (crc >> 8);
	// return (~crc); also works
	return (crc ^ 0xffffffff);
}

// BUSTER_LOCAL void crc32_fill(u32 *table)
// {
//     u8 index=0;
//
//     do
//     {
//         table[index]= index;
//
//         for (u8 z = 8; z; z--)
//         {
//             table[index] = (table[index] & 1) ? (table[index] >> 1) ^ 0xEDB88320 : table[index] >> 1;
//         }
//     }while(++index);
// }

STRUCT(GPTPartitionEntry)
{
    u8 partition_type_guid[16];
    u8 unique_partition_guid[16];
    u64 starting_lba;
    u64 ending_lba;
    u64 attributes;
    u8 partition_name[72];
};

static_assert(sizeof(GPTPartitionEntry) == 128);

int main()
{
    let arena = arena_create((ArenaInitialization){});
    let minimal_gpt = file_read(arena, S("build/minimal_gpt_64.img"), (FileReadOptions){});
    let minimal = (u8*)minimal_gpt.pointer;
    u64 sector_size = 512;

    if (minimal_gpt.length) BUSTER_CHECK(minimal_gpt.length % sector_size == 0);

    u32 gpt_partition_entry_count = 128;
    u64 expected_size = BUSTER_MB(64);

    let mine = (u8*)arena_allocate_bytes(arena, expected_size, sector_size);
    let w = file_writer_init(mine, minimal_gpt.length);
    let writer = &w;

    u64 partition_record_count = 4;
    file_pad(writer, sector_size - (partition_record_count * sizeof(MBRPartitionRecord) + 2));
    let mbr_partition_records = file_allocate(writer, MBRPartitionRecord, partition_record_count);
    let size_in_lba_u64_minus_1 = expected_size / sector_size - 1;
    mbr_partition_records[0] = (MBRPartitionRecord) {
        .boot_indicator = 0,
        .starting_chs = sector_size,
        .os_type = 0xee,
        .ending_chs = 0x82028, //0xffffff,
        .starting_lba = 1,
        .size_in_lba = size_in_lba_u64_minus_1 == (u64)(u32)size_in_lba_u64_minus_1 ? (u32)size_in_lba_u64_minus_1 : UINT32_MAX,
    };
    file_write_byte(writer, 0x55);
    file_write_byte(writer, 0xaa);

    let gpt_header = file_allocate(writer, GPTHeader, 1);
    u64 first_usable_lba = 0x22;
    *gpt_header = (GPTHeader) {
        .signature = efi_part,
        .revision = 0x10000,
        .header_size = gpt_header_size,
        .header_crc32 = 0,
        .reserved0 = 0,
        .header_lba = 1,
        .alternate_lba = size_in_lba_u64_minus_1,
        .first_usable_lba = first_usable_lba,
        .last_usable_lba = size_in_lba_u64_minus_1 - first_usable_lba + 1,
        .partition_entry_lba = 2,
        .partition_entry_count = gpt_partition_entry_count,
        .partition_entry_size = sizeof(GPTPartitionEntry),
    };
    u8 guid[] = { 0xE0, 0xBD, 0xA4, 0xCC, 0xB1, 0x29, 0x46, 0x42, 0x9D, 0x7A, 0xF5, 0x74, 0xE9, 0x60, 0x37, 0xB0 };
    memcpy(gpt_header->disk_guid, guid, sizeof(guid));

    file_align(writer, sector_size);

    let gpt_partition_entry = file_allocate(writer, GPTPartitionEntry, 1);

    u64 starting_lba = BUSTER_MB(1) / sector_size;

    let partition_size = BUSTER_MB(64);

    *gpt_partition_entry = (GPTPartitionEntry) {
        .starting_lba = starting_lba,
        .ending_lba = starting_lba + ((partition_size / sector_size) - starting_lba) - 32 - 2,
        .attributes = 0,
    };
    printf("ending lba: %lx\n", gpt_partition_entry->ending_lba);
    u8 partition_type_guid[] = { 0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47, 0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 };
    u8 unique_partition_guid[] = { 0x4a, 0xef, 0x1b, 0xc4, 0x3c, 0xbf, 0x76, 0x40, 0xbf, 0x25, 0x8c, 0x04, 0x14, 0x4a, 0x0c, 0x31 };
    // u8 unique_partition_guid[] = { 0x66, 0x07, 0xc3, 0xf9, 0xe0, 0x4c, 0x19, 0x43, 0x86, 0x0d, 0x6a, 0x28, 0xf6, 0xda, 0x6b, 0xb2 };
    memcpy(gpt_partition_entry->partition_type_guid, partition_type_guid, sizeof(partition_type_guid));
    memcpy(gpt_partition_entry->unique_partition_guid, unique_partition_guid, sizeof(unique_partition_guid));
    bool name = false;
    if (name)
    {
        let partition_name = S16("primary");
        memcpy(gpt_partition_entry->partition_name, partition_name.pointer, str_size(partition_name));
    }

    let gpt_partition_entry_bytes = &mine[gpt_header->partition_entry_lba * sector_size];
    gpt_header->partition_entry_crc32 = crc32_compute(gpt_partition_entry_bytes, gpt_header->partition_entry_count * gpt_header->partition_entry_size);
    gpt_header->header_crc32 = 0;
    gpt_header->header_crc32 = crc32_compute((u8*)gpt_header, gpt_header->header_size);

    let alternate_gpt_header = (GPTHeader*)&mine[gpt_header->alternate_lba * sector_size];
    memcpy(alternate_gpt_header, gpt_header, gpt_header_size);
    alternate_gpt_header->header_lba = gpt_header->alternate_lba;
    alternate_gpt_header->alternate_lba = gpt_header->header_lba;
    alternate_gpt_header->partition_entry_lba = gpt_header->last_usable_lba + 1;

    let alternate_gpt_partition_entry = (GPTPartitionEntry*)&mine[alternate_gpt_header->partition_entry_lba * sector_size];
    memcpy(alternate_gpt_partition_entry, gpt_partition_entry, sizeof(*gpt_partition_entry));
    alternate_gpt_header->header_crc32 = 0;
    alternate_gpt_header->header_crc32 = crc32_compute((u8*)alternate_gpt_header, alternate_gpt_header->header_size);

    let comparison_size = sector_size * 3;

    bool match = true;
    for (u64 i = 0; i < minimal_gpt.length; i += 1)
    {
        let mine_ch = mine[i];
        let minimal_ch = minimal[i];
        if (mine_ch != minimal_ch)
        {
            match = false;
            printf("Failed to match character at [%lu]. Original: %x. Mine: %x\n", i, minimal_ch, mine_ch);
            break;
        }
    }

    printf("MATCH: %s\n", match ? "TRUE" : "FALSE");

    return !(match & file_write(S("build/mine.img"), (String) { mine, .length = minimal_gpt.length }));
}
