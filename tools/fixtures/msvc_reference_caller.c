// Compiled independently by cl.exe even when the subject is compiled by Buster.
struct MsvcSmall { unsigned a, b; };
struct MsvcLarge { unsigned a, b, c; };

unsigned msvc_subject_mix(struct MsvcSmall, unsigned, struct MsvcLarge, double);
struct MsvcLarge msvc_subject_make(unsigned, unsigned, unsigned);
int msvc_subject_calls_host(void);

unsigned msvc_host_mix(struct MsvcSmall p, unsigned x, struct MsvcLarge q, double y)
{
    return p.a + 2u * p.b + 3u * x + 4u * q.a + 5u * q.b + 6u * q.c + (unsigned)y;
}

struct MsvcLarge msvc_host_make(unsigned a, unsigned b, unsigned c)
{
    struct MsvcLarge result = {a, b, c};
    return result;
}

int main(void)
{
    struct MsvcSmall p = {3u, 5u};
    struct MsvcLarge q = {7u, 11u, 13u};
    struct MsvcLarge answer = msvc_subject_make(19u, 23u, 29u);
    return msvc_subject_mix(p, 17u, q, 2.0) != 227u ||
        answer.a != 19u || answer.b != 23u || answer.c != 29u || msvc_subject_calls_host();
}
