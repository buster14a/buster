#pragma weak nothere
#pragma pack(push, 4)
static int unused(void) { return 0; }
static int unused_var;
static void declared(void);
int main(void) { int x; declared(); return 0; }
