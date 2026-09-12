long helper(void) __asm__("bpf_helper#7");
long exported = 5; static long hidden = 3;
long *exported_address = &exported; static long *hidden_address = &hidden;
static long sub(void) { return hidden + exported; }
long entry(void) { return sub() + helper() + *hidden_address + *exported_address; }
