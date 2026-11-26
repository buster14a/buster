extern float incompatible;
int incompatible = 1;
extern int visible[-1];
int visible;
struct IncompleteRecord record;
enum IncompleteEnum enumeration;
void expired(void)
{
    extern int expired_object[-1];
}
int expired_object;
