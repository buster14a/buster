from pathlib import Path


root = Path(__file__).resolve().parents[2]
path = root / "src/buster/lib/string.c"
text = path.read_text(encoding="utf-8")
start_marker = "BUSTER_GLOBAL_LOCAL void string_format_va_prepare(va_list* variable_arguments, u32* gp_register_slots_remaining, u64 size,"
end_marker = "#endif\nBUSTER_GLOBAL_LOCAL bool code_unit_is_binary"
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = """BUSTER_GLOBAL_LOCAL void string_format_va_prepare(va_list* variable_arguments, u32* gp_register_slots_remaining, u64 size,
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
        // Windows ARM64 exposes va_list as a pointer into the contiguous
        // homed-register and incoming-stack argument area. A 16-byte value
        // starts on an even x-register, and an argument that does not fit in
        // the remaining x-register slots moves wholly to the stack.
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
        pointer = (u8*)align_forward((u64)pointer, alignment);
    }

    *variable_arguments = (char8*)pointer;
}
"""
path.write_text(text[:start] + replacement + text[end:], encoding="utf-8")
