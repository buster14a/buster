from pathlib import Path

string_path = Path("src/buster/lib/string.c")
text = string_path.read_text()
start_marker = "BUSTER_GLOBAL_LOCAL void string_format_va_prepare(va_list* variable_arguments, u32* gp_register_slots_remaining, u64 size,"
end_marker = "#endif\nBUSTER_GLOBAL_LOCAL bool code_unit_is_binary"
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = '''BUSTER_GLOBAL_LOCAL void string_format_va_prepare(va_list* variable_arguments, u32* gp_register_slots_remaining, u64 size,
                                                  u64 alignment)
{
    BUSTER_CHECK(*gp_register_slots_remaining <= STRING_FORMAT_VA_GP_REGISTER_COUNT);
    BUSTER_CHECK(size != 0 && size <= 2 * sizeof(u64));
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(alignment) && alignment <= 2 * sizeof(u64));

    if (alignment < sizeof(u64))
    {
        alignment = sizeof(u64);
    }

    u64 slot_count = (size + sizeof(u64) - 1) / sizeof(u64);
    u8* pointer = (u8*)*variable_arguments;

    if (*gp_register_slots_remaining)
    {
        // The register save area and incoming stack arguments are contiguous,
        // but AAPCS64 still aligns 16-byte values to an even x-register and
        // moves a value wholly to the stack when it does not fit.
        u8* register_end = pointer + (u64)*gp_register_slots_remaining * sizeof(u64);
        u8* aligned_pointer = (u8*)align_forward((u64)pointer, alignment);
        u64 alignment_slots = (u64)(aligned_pointer - pointer) / sizeof(u64);
        u32 available_slots = *gp_register_slots_remaining > alignment_slots
                                  ? *gp_register_slots_remaining - (u32)alignment_slots
                                  : 0;

        if (slot_count <= available_slots)
        {
            pointer = aligned_pointer;
            *gp_register_slots_remaining -= (u32)(alignment_slots + slot_count);
        }
        else
        {
            pointer = (u8*)align_forward((u64)register_end, alignment);
            *gp_register_slots_remaining = 0;
        }
    }
    else
    {
        // Clang's pointer-form Windows ARM64 va_list advances linearly and
        // does not restore the ABI's 16-byte stack alignment for __int128.
        pointer = (u8*)align_forward((u64)pointer, alignment);
    }

    *variable_arguments = (char8*)pointer;
}
'''
text = text[:start] + replacement + text[end:]
string_path.write_text(text)

driver_path = Path("src/buster/tests/compiler/driver/driver_test.c")
text = driver_path.read_text()
old = 'String8 node_arguments[] = {node, S8("--experimental-wasm-memory64"), S8("tests/wasm_memory_alignment_execution.js"), wasm64_alignment_output};'
new = 'String8 node_arguments[] = {node, S8("tests/wasm_memory_alignment_execution.js"), wasm64_alignment_output};'
if text.count(old) != 1:
    raise SystemExit(f"expected one legacy Node flag invocation, found {text.count(old)}")
driver_path.write_text(text.replace(old, new))

Path(".github/scripts/ci-fix-temp.py").unlink()
