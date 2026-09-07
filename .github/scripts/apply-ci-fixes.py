from pathlib import Path


branch_root = Path(__file__).resolve().parents[2]

string_path = branch_root / "src/buster/lib/string.c"
string_text = string_path.read_text(encoding="utf-8")
start_marker = "BUSTER_GLOBAL_LOCAL void string_format_va_prepare(va_list* variable_arguments, u32* gp_register_slots_remaining, u64 size,"
end_marker = "#endif\nBUSTER_GLOBAL_LOCAL bool code_unit_is_binary"
start = string_text.index(start_marker)
end = string_text.index(end_marker, start)
replacement = """BUSTER_GLOBAL_LOCAL void string_format_va_prepare(va_list* variable_arguments, u32* gp_register_slots_remaining, u64 size,
                                                  u64 alignment)
{
    BUSTER_CHECK(*gp_register_slots_remaining <= STRING_FORMAT_VA_GP_REGISTER_COUNT);
    BUSTER_CHECK(size != 0 && size <= 2 * sizeof(u64));
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(alignment) && alignment <= 2 * sizeof(u64));

    // Clang 20's Windows ARM64 va_arg lowering advances the platform's
    // pointer-form va_list correctly, but does not round 16-byte values up to
    // their ABI alignment. Aligning the current pointer before native va_arg
    // is sufficient for both the homed x-register area and the contiguous
    // incoming stack area; native va_arg still performs the load and advance.
    if (alignment > sizeof(u64))
    {
        *variable_arguments = (char8*)align_forward((u64)*variable_arguments, alignment);
    }
}
"""
string_path.write_text(string_text[:start] + replacement + string_text[end:], encoding="utf-8")

driver_path = branch_root / "src/buster/tests/compiler/driver/driver_test.c"
driver_text = driver_path.read_text(encoding="utf-8")
old = 'String8 node_arguments[] = {node, S8("--experimental-wasm-memory64"), S8("tests/wasm_memory_alignment_execution.js"), wasm64_alignment_output};'
new = 'String8 node_arguments[] = {node, S8("tests/wasm_memory_alignment_execution.js"), wasm64_alignment_output};'
if driver_text.count(old) != 1:
    raise SystemExit(f"expected one obsolete Node memory64 flag, found {driver_text.count(old)}")
driver_path.write_text(driver_text.replace(old, new), encoding="utf-8")
