// UEFI firmware execution gate, included by build.c. uefi_boot_main owns
// command dispatch; uefi_boot_lane compiles and boots each allocator/target;
// uefi_boot_command retains bounded child evidence; uefi_boot_accept is the
// execution oracle. Firmware/package pins are documented in uefi-target.md.
#define BUSTER_UEFI_BOOT_TIMEOUT_US (120ull * 1000000)
#define BUSTER_UEFI_BOOT_MARKER "BUSTER_UEFI_BOOT_PASS "
#define BUSTER_UEFI_BOOT_FAILURE "BUSTER_UEFI_BOOT_FAIL"

typedef struct UefiBootContext UefiBootContext;
struct UefiBootContext
{
    Arena *arena;
    String8 output;
    bool io_failed;
};
typedef struct UefiBootTarget UefiBootTarget;
struct UefiBootTarget
{
    String8 name;
    String8 triple;
    String8 boot_file;
    String8 emulator;
    String8 machine;
    String8 cpu;
    String8 code;
    String8 variables;
    String8 code_sha256;
    String8 variables_sha256;
};

BUSTER_GLOBAL_LOCAL void uefi_boot_write(UefiBootContext *context, String8 path, String8 content)
{
    String8 path_z = string_duplicate_arena(context->arena, path, true);
    FILE *file = fopen((char *)path_z.pointer, "wb");
    if (!file) { context->io_failed = true; }
    else
    {
        if (content.length) { context->io_failed |= fwrite(content.pointer, 1, (size_t)content.length, file) != content.length; }
        context->io_failed |= fclose(file) != 0;
    }
}

BUSTER_GLOBAL_LOCAL ProcessWaitResult uefi_boot_command(UefiBootContext *context, SliceString8 command, String8 prefix, u64 timeout_us)
{
    Arena *arena = context->arena;
    String8List argv = {0};
    for (u64 index = 0; index < command.length; index += 1)
    {
        string8_list_push(arena, &argv, string_format(arena, S8("{u64}:{S8}\n"), command.pointer[index].length, command.pointer[index]));
    }
    String8 path = path_join(arena, context->output, prefix);
    uefi_boot_write(context, string_format(arena, S8("{S8}.argv.txt"), path), string_join_arena(arena, string8_list_to_slice(arena, argv), false));
    command_print(command);
    ProcessWaitResult wait = {.result = PROCESS_RESULT_NOT_EXISTENT};
    if (!context->io_failed)
    {
        ProcessSpawnResult spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR), .use_process_environment = 1});
        if (spawn.handle)
        {
            wait = os_process_wait_deadline(arena, spawn, timeout_us);
        }
    }
    String8 output = {.pointer = (char8 *)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length};
    String8 error = {.pointer = (char8 *)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length};
    uefi_boot_write(context, string_format(arena, S8("{S8}.stdout.log"), path), output);
    uefi_boot_write(context, string_format(arena, S8("{S8}.stderr.log"), path), error);
    uefi_boot_write(context, string_format(arena, S8("{S8}.status.txt"), path),
        string_format(arena, S8("result={u32} platform_status={u32} timed_out={u32}\n"), (u32)wait.result, wait.platform_status, (u32)wait.timed_out));
    return wait;
}

BUSTER_GLOBAL_LOCAL bool uefi_boot_success(ProcessWaitResult wait)
{
    return wait.result == PROCESS_RESULT_SUCCESS && wait.platform_status == 0 && !wait.timed_out;
}

BUSTER_GLOBAL_LOCAL bool uefi_boot_accept(ProcessWaitResult wait, String8 serial, String8 marker)
{
    u64 count = 0;
    for (u64 index = 0; index + marker.length <= serial.length; index += 1)
    {
        if ((index == 0 || serial.pointer[index - 1] == '\n') && memcmp(serial.pointer + index, marker.pointer, (size_t)marker.length) == 0)
        {
            count += 1;
        }
    }
    return uefi_boot_success(wait) && count == 1 &&
           string_first_sequence(serial, S8(BUSTER_UEFI_BOOT_FAILURE)) == BUSTER_STRING_NO_MATCH;
}

BUSTER_GLOBAL_LOCAL bool uefi_boot_digest(UefiBootContext *context, String8 file, String8 expected, String8 prefix)
{
    String8 command[] = {S8("sha256sum"), file};
    ProcessWaitResult wait = uefi_boot_command(context, (SliceString8)BUSTER_ARRAY_TO_SLICE(command), prefix, BUSTER_UEFI_BOOT_TIMEOUT_US);
    ByteSlice output = wait.streams[STANDARD_STREAM_OUTPUT];
    bool valid = uefi_boot_success(wait) && output.length >= 65 && output.pointer[64] == ' ';
    if (valid && expected.length)
    {
        valid = expected.length == 64 && memcmp(output.pointer, expected.pointer, 64) == 0;
    }
    return valid && !context->io_failed;
}

// Fixed MBR + FAT16 EFI System Partition. No host directory timestamps,
// formatting programs, filesystem mounts, or QEMU virtual-FAT backend enter
// the boot oracle. Only the generated PE file bytes vary between images.
BUSTER_GLOBAL_LOCAL void uefi_boot_u16(u8 *bytes, u32 value)
{
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
}

BUSTER_GLOBAL_LOCAL void uefi_boot_u32(u8 *bytes, u32 value)
{
    uefi_boot_u16(bytes, value);
    uefi_boot_u16(bytes + 2, value >> 16);
}

BUSTER_GLOBAL_LOCAL void uefi_boot_directory_entry(u8 *entry, String8 name, u8 attribute, u32 cluster, u32 size)
{
    memcpy(entry, name.pointer, 11);
    entry[11] = attribute;
    // A fixed valid FAT date: 2024-02-01. Times stay midnight.
    uefi_boot_u16(entry + 16, (44u << 9) | (2u << 5) | 1u);
    uefi_boot_u16(entry + 18, (44u << 9) | (2u << 5) | 1u);
    uefi_boot_u16(entry + 24, (44u << 9) | (2u << 5) | 1u);
    uefi_boot_u16(entry + 26, cluster);
    uefi_boot_u32(entry + 28, size);
}

BUSTER_GLOBAL_LOCAL bool uefi_boot_media(UefiBootContext *context, String8 image, String8 media, bool x86_64)
{
    enum
    {
        SECTOR_BYTES = 512, PARTITION_SECTOR = 2048, VOLUME_SECTORS = 32768,
        FAT_SECTORS = 128, ROOT_SECTORS = 32, ROOT_SECTOR = 1 + 2 * FAT_SECTORS,
        DATA_SECTOR = ROOT_SECTOR + ROOT_SECTORS, EFI_CLUSTER = 2, BOOT_CLUSTER = 3, FILE_CLUSTER = 4,
    };
    ByteSlice file = file_read(context->arena, image, (FileReadOptions){0});
    bool valid = file.length > 0 && file.length <= (VOLUME_SECTORS - DATA_SECTOR - (FILE_CLUSTER - 2)) * SECTOR_BYTES;
    if (valid)
    {
        u64 size = (PARTITION_SECTOR + VOLUME_SECTORS) * SECTOR_BYTES;
        u8 *disk = arena_allocate(context->arena, u8, size);
        memset(disk, 0, (size_t)size);
        disk[446 + 4] = 0xef; // EFI System Partition, addressed with LBA.
        disk[446 + 1] = disk[446 + 5] = 0xfe;
        disk[446 + 2] = disk[446 + 3] = disk[446 + 6] = disk[446 + 7] = 0xff;
        uefi_boot_u32(disk + 446 + 8, PARTITION_SECTOR);
        uefi_boot_u32(disk + 446 + 12, VOLUME_SECTORS);
        disk[510] = 0x55; disk[511] = 0xaa;
        u8 *volume = disk + PARTITION_SECTOR * SECTOR_BYTES;
        volume[0] = 0xeb; volume[1] = 0x3c; volume[2] = 0x90;
        memcpy(volume + 3, "BUSTER  ", 8);
        uefi_boot_u16(volume + 11, SECTOR_BYTES);
        volume[13] = 1; // One sector per cluster.
        uefi_boot_u16(volume + 14, 1);
        volume[16] = 2; // Two identical FATs.
        uefi_boot_u16(volume + 17, ROOT_SECTORS * SECTOR_BYTES / 32);
        uefi_boot_u16(volume + 19, VOLUME_SECTORS);
        volume[21] = 0xf8;
        uefi_boot_u16(volume + 22, FAT_SECTORS);
        uefi_boot_u16(volume + 24, 63);
        uefi_boot_u16(volume + 26, 255);
        uefi_boot_u32(volume + 28, PARTITION_SECTOR);
        volume[36] = 0x80; volume[38] = 0x29;
        uefi_boot_u32(volume + 39, 0x42555354);
        memcpy(volume + 43, "BUSTER UEFI", 11);
        memcpy(volume + 54, "FAT16   ", 8);
        volume[510] = 0x55; volume[511] = 0xaa;
        u8 *fat = volume + SECTOR_BYTES;
        uefi_boot_u16(fat, 0xfff8);
        uefi_boot_u16(fat + 2, 0xffff);
        uefi_boot_u16(fat + EFI_CLUSTER * 2, 0xffff);
        uefi_boot_u16(fat + BOOT_CLUSTER * 2, 0xffff);
        u32 clusters = (u32)((file.length + SECTOR_BYTES - 1) / SECTOR_BYTES);
        for (u32 index = 0; index < clusters; index += 1)
        {
            uefi_boot_u16(fat + (FILE_CLUSTER + index) * 2, index + 1 == clusters ? 0xffff : FILE_CLUSTER + index + 1);
        }
        memcpy(fat + FAT_SECTORS * SECTOR_BYTES, fat, FAT_SECTORS * SECTOR_BYTES);
        u8 *root = volume + ROOT_SECTOR * SECTOR_BYTES;
        u8 *efi = volume + DATA_SECTOR * SECTOR_BYTES;
        u8 *boot = efi + SECTOR_BYTES;
        uefi_boot_directory_entry(root, S8("EFI        "), 0x10, EFI_CLUSTER, 0);
        uefi_boot_directory_entry(efi, S8(".          "), 0x10, EFI_CLUSTER, 0);
        uefi_boot_directory_entry(efi + 32, S8("..         "), 0x10, 0, 0);
        uefi_boot_directory_entry(efi + 64, S8("BOOT       "), 0x10, BOOT_CLUSTER, 0);
        uefi_boot_directory_entry(boot, S8(".          "), 0x10, BOOT_CLUSTER, 0);
        uefi_boot_directory_entry(boot + 32, S8("..         "), 0x10, EFI_CLUSTER, 0);
        uefi_boot_directory_entry(boot + 64, x86_64 ? S8("BOOTX64 EFI") : S8("BOOTAA64EFI"), 0x20, FILE_CLUSTER, (u32)file.length);
        memcpy(efi + (FILE_CLUSTER - 2) * SECTOR_BYTES, file.pointer, (size_t)file.length);
        uefi_boot_write(context, media, (String8){.pointer = (char8 *)disk, .length = size});
    }
    return valid && !context->io_failed;
}

BUSTER_GLOBAL_LOCAL bool uefi_boot_lane(UefiBootContext *context, String8 ide, UefiBootTarget target, String8 firmware_root, String8 mode, bool negative)
{
    Arena *arena = context->arena;
    String8 name = string_format(arena, S8("{S8}-{S8}{S8}"), target.name, mode, negative ? S8("-negative") : S8(""));
    String8 directory = path_join(arena, context->output, name);
    String8 esp = path_join(arena, directory, S8("esp"));
    String8 boot = path_join(arena, esp, S8("EFI/BOOT"));
    make_directory_recursive(arena, boot);
    String8 image = path_join(arena, boot, target.boot_file);
    String8 compile[] = {ide, S8("cc"), S8("-target"), target.triple, S8("-g0"), S8("-fverify-codegen"),
        string_format(arena, S8("-fregister-allocator={S8}"), mode),
        negative ? S8("-DBUSTER_UEFI_NEGATIVE=1") : S8("-DBUSTER_UEFI_NEGATIVE=0"),
        S8("tests/uefi_boot.c"), S8("-o"), image};
    ProcessWaitResult compiled = uefi_boot_command(context, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile),
        path_join(arena, name, S8("compile")), BUSTER_UEFI_BOOT_TIMEOUT_US);
    bool passed = uefi_boot_success(compiled) && uefi_boot_digest(context, image, (String8){0}, path_join(arena, name, S8("image-sha256")));
    // The fixed 512 MiB machines have no RAM at the linker's preferred 5 GiB
    // base. Pin that PE field too: pointer checks then require real rebasing.
    if (passed)
    {
        ByteSlice bytes = file_read(arena, image, (FileReadOptions){0});
        u32 pe_offset = 0;
        u64 image_base = 0;
        if (bytes.length >= 64)
        {
            memcpy(&pe_offset, bytes.pointer + 60, 4);
        }
        passed = bytes.length >= 64 && (u64)pe_offset + 56 <= bytes.length;
        if (passed)
        {
            memcpy(&image_base, bytes.pointer + pe_offset + 48, 8);
            passed = image_base == UINT64_C(0x140000000);
        }
    }
    String8 media = path_join(arena, directory, S8("esp.img"));
    if (passed)
    {
        passed = uefi_boot_media(context, image, media, string_equal(target.name, S8("x86_64"))) &&
            uefi_boot_digest(context, media, (String8){0}, path_join(arena, name, S8("media-sha256")));
    }
    String8 variables = path_join(arena, directory, S8("VARS.fd"));
    if (passed)
    {
        passed = file_copy((CopyFileArguments){.original_path = path_join(arena, firmware_root, target.variables), .new_path = variables});
    }
    ProcessWaitResult observed = {.result = PROCESS_RESULT_NOT_EXISTENT};
    String8 serial = {0};
    if (passed)
    {
        String8 serial_path = path_join(arena, directory, S8("serial.log"));
        String8 command[] = {target.emulator, S8("-machine"), target.machine, S8("-accel"), S8("tcg,thread=single"),
            S8("-cpu"), target.cpu, S8("-smp"), S8("1"), S8("-m"), S8("512M"), S8("-nodefaults"),
            S8("-display"), S8("none"), S8("-monitor"), S8("none"), S8("-serial"), string_format(arena, S8("file:{S8}"), serial_path),
            S8("-net"), S8("none"), S8("-no-reboot"), S8("-boot"), S8("order=c,strict=on"),
            S8("-rtc"), S8("base=2024-02-01T00:00:00,clock=vm"),
            S8("-drive"), string_format(arena, S8("if=pflash,format=raw,unit=0,readonly=on,file={S8}"), path_join(arena, firmware_root, target.code)),
            S8("-drive"), string_format(arena, S8("if=pflash,format=raw,unit=1,file={S8}"), variables),
            S8("-drive"), string_format(arena, S8("if=none,id=esp,format=raw,readonly=on,file={S8}"), media),
            S8("-device"), S8("virtio-blk-pci,drive=esp")};
        observed = uefi_boot_command(context, (SliceString8)BUSTER_ARRAY_TO_SLICE(command), path_join(arena, name, S8("qemu")), BUSTER_UEFI_BOOT_TIMEOUT_US);
        ByteSlice bytes = file_read(arena, serial_path, (FileReadOptions){0});
        serial = (String8){.pointer = (char8 *)bytes.pointer, .length = bytes.length};
        String8 marker = string_format(arena, S8(BUSTER_UEFI_BOOT_MARKER "{S8}\r\n"), target.name);
        bool accepted = uefi_boot_accept(observed, serial, marker);
        passed = negative ? (uefi_boot_success(observed) && !accepted &&
            string_first_sequence(serial, S8(BUSTER_UEFI_BOOT_FAILURE)) != BUSTER_STRING_NO_MATCH &&
            string_first_sequence(serial, S8(BUSTER_UEFI_BOOT_MARKER)) == BUSTER_STRING_NO_MATCH) : accepted;
    }
    passed &= !context->io_failed;
    String8 verdict = string_format(arena, S8("UEFI_BOOT target={S8} allocator={S8} control={S8} status={S8} result={u32} platform_status={u32} timed_out={u32}\n"),
        target.name, mode, negative ? S8("negative") : S8("positive"), passed ? S8("pass") : S8("fail"),
        (u32)observed.result, observed.platform_status, (u32)observed.timed_out);
    uefi_boot_write(context, path_join(arena, directory, S8("result.txt")), verdict);
    string_print(S8("{S8}"), verdict);
    return passed && !context->io_failed;
}

BUSTER_GLOBAL_LOCAL bool uefi_boot_self_test(UefiBootContext *context, String8 driver)
{
    String8 marker = S8(BUSTER_UEFI_BOOT_MARKER "x86_64\r\n");
    ProcessWaitResult zero = {.result = PROCESS_RESULT_SUCCESS};
    ProcessWaitResult failed = {.result = PROCESS_RESULT_FAILED, .platform_status = 256};
    ProcessWaitResult timeout = {.result = PROCESS_RESULT_FAILED, .timed_out = 1};
    bool passed = uefi_boot_accept(zero, marker, marker) &&
        !uefi_boot_accept(zero, (String8){0}, marker) &&
        !uefi_boot_accept(failed, marker, marker) && !uefi_boot_accept(timeout, marker, marker) &&
        !uefi_boot_accept(zero, S8(BUSTER_UEFI_BOOT_MARKER "aarch64\r\n"), marker) &&
        !uefi_boot_accept(zero, S8("prefix" BUSTER_UEFI_BOOT_MARKER "x86_64\r\n"), marker) &&
        !uefi_boot_accept(zero, S8(BUSTER_UEFI_BOOT_MARKER "x86_64\r\n" BUSTER_UEFI_BOOT_MARKER "x86_64\r\n"), marker) &&
        !uefi_boot_accept(zero, S8(BUSTER_UEFI_BOOT_MARKER "x86_64\r\n" BUSTER_UEFI_BOOT_FAILURE), marker);
    String8 hang[] = {driver, S8("test_uefi"), S8("--timeout-child")};
    ProcessWaitResult hung = uefi_boot_command(context, (SliceString8)BUSTER_ARRAY_TO_SLICE(hang), S8("timeout-control"), 50000);
    passed &= hung.timed_out && !uefi_boot_accept(hung, marker, marker);
    string_print(S8("UEFI_BOOT_SELF_TEST status={S8}\n"), passed && !context->io_failed ? S8("pass") : S8("fail"));
    return passed && !context->io_failed;
}

BUSTER_GLOBAL_LOCAL ProcessResult uefi_boot_main(Arena *arena, SliceString8 arguments, String8 driver)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--timeout-child")))
    {
        for (;;) { os_now_microseconds(); }
    }
    else if (arguments.length != 2)
    {
        string_print(S8("usage: build/build test_uefi <built-ide|--self-test> <fresh-output-directory>\n"));
    }
    else if (path_exists(arena, arguments.pointer[1]))
    {
        string_print(S8("error: test_uefi output directory already exists; use a fresh path to exclude stale evidence\n"));
    }
    else
    {
        make_directory_recursive(arena, path_parent(arena, arguments.pointer[1]));
        // Claim the leaf atomically as well as rejecting an existing path above.
        // Concurrent invocations must never share serial logs or variable stores.
        SummaryDirectoryClaimResult claim = summary_self_test_directory_claim(arguments.pointer[1]);
        UefiBootContext context = {.arena = arena};
        if (claim == SUMMARY_DIRECTORY_CLAIMED)
        {
            context.output = os_path_absolute(arena, arguments.pointer[1], true);
        }
        context.io_failed = context.output.length == 0;
        if (context.io_failed)
        {
            string_print(S8("error: test_uefi could not create the output directory\n"));
        }
        else if (string_equal(arguments.pointer[0], S8("--self-test")))
        {
            result = uefi_boot_self_test(&context, driver) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
        else
        {
            String8 ide = os_path_absolute(arena, arguments.pointer[0], true);
            String8 firmware_root = os_get_environment_variable(S8("BUSTER_UEFI_FIRMWARE_ROOT"));
            if (!firmware_root.length) { firmware_root = S8("/usr/share"); }
            UefiBootTarget targets[] = {
                {S8("x86_64"), S8("x86_64-unknown-uefi"), S8("BOOTX64.EFI"), S8("qemu-system-x86_64"), S8("pc-q35-8.2"), S8("qemu64"),
                    S8("OVMF/OVMF_CODE_4M.fd"), S8("OVMF/OVMF_VARS_4M.fd"),
                    S8("949bfa5389c4c48582737481e7d24f46b3a16b276ef44c4089a56858c6a0a446"),
                    S8("5d2ac383371b408398accee7ec27c8c09ea5b74a0de0ceea6513388b15be5d1e")},
                {S8("aarch64"), S8("aarch64-unknown-uefi"), S8("BOOTAA64.EFI"), S8("qemu-system-aarch64"), S8("virt-8.2"), S8("cortex-a57"),
                    S8("AAVMF/AAVMF_CODE.fd"), S8("AAVMF/AAVMF_VARS.fd"),
                    S8("4a4cb7f6d8106bb2a7dd8c763fab14b1810152136fc4304e5b728f0043e84f12"),
                    S8("b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f")},
            };
            String8 revision[] = {S8("git"), S8("rev-parse"), S8("HEAD")};
            ProcessWaitResult recorded = uefi_boot_command(&context, (SliceString8)BUSTER_ARRAY_TO_SLICE(revision), S8("revision"), BUSTER_UEFI_BOOT_TIMEOUT_US);
            bool valid = uefi_boot_success(recorded) && path_exists(arena, ide) &&
                uefi_boot_digest(&context, ide, (String8){0}, S8("compiler-sha256")) &&
                uefi_boot_digest(&context, S8("tests/uefi_boot.c"), (String8){0}, S8("fixture-sha256"));
            u64 passed = 0;
            u64 unavailable = 0;
            for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(targets); index += 1)
            {
                UefiBootTarget target = targets[index];
                bool available = executable_resolve_in_path(arena, target.emulator).length != 0 &&
                    path_exists(arena, path_join(arena, firmware_root, target.code)) &&
                    path_exists(arena, path_join(arena, firmware_root, target.variables));
                if (!available)
                {
                    unavailable += 1;
                    string_print(S8("UEFI_BOOT target={S8} status=unavailable runtime_validated=0\n"), target.name);
                }
                else if (valid)
                {
                    String8 version[] = {target.emulator, S8("--version")};
                    ProcessWaitResult version_result = uefi_boot_command(&context, (SliceString8)BUSTER_ARRAY_TO_SLICE(version),
                        string_format(arena, S8("{S8}-version"), target.name), BUSTER_UEFI_BOOT_TIMEOUT_US);
                    ByteSlice version_bytes = version_result.streams[STANDARD_STREAM_OUTPUT];
                    String8 version_text = {.pointer = (char8 *)version_bytes.pointer, .length = version_bytes.length};
                    bool pinned = uefi_boot_success(version_result) && string_starts_with_sequence(version_text,
                        S8("QEMU emulator version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)\n")) &&
                        uefi_boot_digest(&context, path_join(arena, firmware_root, target.code), target.code_sha256,
                            string_format(arena, S8("{S8}-code-sha256"), target.name)) &&
                        uefi_boot_digest(&context, path_join(arena, firmware_root, target.variables), target.variables_sha256,
                            string_format(arena, S8("{S8}-vars-sha256"), target.name));
                    if (pinned)
                    {
                        String8 modes[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
                        for (u64 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
                        {
                            passed += uefi_boot_lane(&context, ide, target, firmware_root, modes[mode], false);
                        }
                        passed += uefi_boot_lane(&context, ide, target, firmware_root, S8("fast"), true);
                    }
                    else
                    {
                        string_print(S8("UEFI_BOOT target={S8} status=pin-mismatch runtime_validated=0\n"), target.name);
                    }
                }
            }
            bool success = valid && !context.io_failed && !unavailable && passed == 10;
            String8 summary = string_format(arena, S8("UEFI_BOOT_RESULT checks={u64}/10 unavailable_targets={u64} status={S8}\n"),
                passed, unavailable, success ? S8("pass") : S8("fail"));
            uefi_boot_write(&context, path_join(arena, context.output, S8("summary.txt")), summary);
            string_print(S8("{S8}"), summary);
            result = success && !context.io_failed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
    }
    return result;
}
