#pragma pack(push, 4)
#pragma weak never_declared
static void undefined_internal(void);
static int unused_static;
const int tentative;
static const int tentative_static;
void use(void) { undefined_internal(); }
