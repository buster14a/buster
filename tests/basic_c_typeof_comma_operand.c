// `__typeof__`'s argument is a full expression (C23 6.7.2.5), so an
// unparenthesized comma names the type of its last operand, at file scope
// and in a block (#3493).
int value = 7;
int* pointer = &value;
__typeof__(0, pointer) file_scope = &value;
int main(void)
{
    __typeof__(1, pointer) block_scope = file_scope;
    __typeof__(value ? 1 : 2, pointer) conditional_first = block_scope;
    __typeof__(value, 2l) last_operand = 3;
    return *block_scope == 7 && *conditional_first == 7 && sizeof(last_operand) == sizeof(long) &&
                   sizeof(__typeof__(0, pointer)) == sizeof(int*)
               ? 0
               : 1;
}
