// The C half of a link that has an assembly translation unit in it.
//
// It is the case where the two kinds of object meet: a compiled module carries
// one section per kind and an assembled one carries only the sections its file
// named, so merging them is what proves the linker reads a section's kind
// rather than its position. It calls into the assembly's text and reads the
// assembly's read-only data, and returns zero only if both arrived.

int basic_asm_unit_helper(void);
extern const unsigned basic_asm_unit_table[];

int main(void)
{
    return basic_asm_unit_helper() + (basic_asm_unit_table[0] != 3) + (basic_asm_unit_table[1] != 5);
}
