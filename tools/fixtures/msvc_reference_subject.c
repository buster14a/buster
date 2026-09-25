// Standard C11, deliberately free of signed overflow, aliasing and extensions.
// The fixed caller in msvc_reference_caller.c checks both compiler directions.
struct MsvcSmall { unsigned a, b; };
struct MsvcLarge { unsigned a, b, c; };

unsigned msvc_host_mix(struct MsvcSmall, unsigned, struct MsvcLarge, double);
struct MsvcLarge msvc_host_make(unsigned, unsigned, unsigned);

unsigned msvc_subject_mix(struct MsvcSmall p, unsigned x, struct MsvcLarge q, double y)
{
    return p.a + 2u * p.b + 3u * x + 4u * q.a + 5u * q.b + 6u * q.c + (unsigned)y;
}

struct MsvcLarge msvc_subject_make(unsigned a, unsigned b, unsigned c)
{
    struct MsvcLarge result = {a, b, c};
    return result;
}

int msvc_subject_calls_host(void)
{
    struct MsvcSmall p = {2u, 4u};
    struct MsvcLarge q = {5u, 8u, 10u};
    struct MsvcLarge answer = msvc_host_make(19u, 23u, 29u);
    char byte = (char)255;
    int promoted_byte = byte;
    signed char signed_byte = (signed char)255;
    unsigned char unsigned_byte = (unsigned char)255;
    return msvc_host_mix(p, 9u, q, 3.0) != 160u ||
        answer.a != 19u || answer.b != 23u || answer.c != 29u || (unsigned)byte != 255u || promoted_byte != 255 ||
        signed_byte != -1 || unsigned_byte != 255u;
}
