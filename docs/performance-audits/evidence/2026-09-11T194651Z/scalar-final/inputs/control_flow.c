/* buster-throughput schema=1 seed=20260907 profile=smoke scale=1 case=control_flow */
unsigned control_0000(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 1721155669u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 1830542459u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 3288486161u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 1244067361u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 145823312u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 1696296653u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 278634284u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 213646579u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 395381057u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 3785322941u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 4205976393u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 2193068836u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 1790329581u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 4288528536u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 3058862271u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 266762429u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
unsigned control_0001(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 3737559141u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 1401595136u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 3799938101u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 3017837417u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 4080189391u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 1034598634u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 1011776401u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 2495112175u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 2155626029u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 4035584977u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 721120393u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 2854717628u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 1026851236u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 218805156u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 884858235u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 1833804978u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
unsigned control_0002(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 3802825663u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 3917111537u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 166091299u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 2531865906u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 3114962869u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 3822528763u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 3753982079u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 3776587544u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 450349412u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 2627480505u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 613150412u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 1931087557u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 1788101412u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 3169253852u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 410275442u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 2800915279u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
unsigned control_0003(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 1744447810u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 2023587147u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 834317553u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 3769852547u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 277600946u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 275268828u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 109763589u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 4232053376u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 2116021640u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 4135266239u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 3273478617u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 1307503626u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 2643682205u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 3455571661u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 3538142746u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 3996824138u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
unsigned control_0004(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 3053871123u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 981704497u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 1708583907u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 2918195574u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 2449999545u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 3375535382u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 3918725918u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 3084942118u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 2820148196u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 2248095126u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 183845424u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 4107370057u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 3088993125u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 1596500060u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 239248621u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 20584898u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
unsigned control_0005(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 38625443u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 1702078063u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 645372080u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 2036314624u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 1826702447u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 3388755321u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 1245374288u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 3436014392u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 1424041549u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 1967093113u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 3918728849u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 2154577041u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 4164809390u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 2338988762u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 689024258u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 2324531226u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
unsigned control_0006(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 1247047293u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 888019888u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 76836860u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 2753479273u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 4129838144u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 1755670544u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 1900434083u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 3646917226u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 709431811u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 153597671u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 10357338u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 4218453168u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 1747377827u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 3450735642u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 2307132300u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 373584182u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
unsigned control_0007(unsigned x, unsigned y)
{
    if ((x ^ y) & 1u) { x += y; } else { y ^= x + 2487049703u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2u) { x += y; } else { y ^= x + 3484804807u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4u) { x += y; } else { y ^= x + 516608337u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8u) { x += y; } else { y ^= x + 3640309505u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16u) { x += y; } else { y ^= x + 1296093580u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32u) { x += y; } else { y ^= x + 3109960884u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 64u) { x += y; } else { y ^= x + 2816014800u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 128u) { x += y; } else { y ^= x + 2763395457u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 256u) { x += y; } else { y ^= x + 3058064002u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 512u) { x += y; } else { y ^= x + 3755124009u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 1024u) { x += y; } else { y ^= x + 3851408082u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 2048u) { x += y; } else { y ^= x + 3848682040u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 4096u) { x += y; } else { y ^= x + 1711558184u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 8192u) { x += y; } else { y ^= x + 400949448u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 16384u) { x += y; } else { y ^= x + 672271831u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    if ((x ^ y) & 32768u) { x += y; } else { y ^= x + 1555298659u; }
    for (unsigned k = 0; k < (y & 3u); ++k) { x = x * 33u + k; }
    switch (x & 3u) { case 0: y += x; break; case 1: y ^= x; break; default: x += y; break; }
    return x ^ y;
}
