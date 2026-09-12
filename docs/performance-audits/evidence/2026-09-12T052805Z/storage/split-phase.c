static unsigned long sp_mix(unsigned long v) { v ^= v >> 31; v *= 0x9e3779b97f4a7c15UL; v ^= v >> 27; return v; }
unsigned long split_phase(unsigned long seed, unsigned long rounds) {
    unsigned long k0 = seed + 1; unsigned long k1 = seed + 2; unsigned long k2 = seed + 3; unsigned long k3 = seed + 4;
    unsigned long k4 = seed + 5; unsigned long k5 = seed + 6; unsigned long k6 = seed + 7; unsigned long k7 = seed + 8;
    unsigned long k8 = seed + 9; unsigned long k9 = seed + 10; unsigned long k10 = seed + 11; unsigned long k11 = seed + 12;
    unsigned long k12 = seed + 13; unsigned long k13 = seed + 14; unsigned long k14 = seed + 15; unsigned long k15 = seed + 16;
    unsigned long k16 = seed + 17; unsigned long k17 = seed + 18; unsigned long k18 = seed + 19; unsigned long k19 = seed + 20;
    unsigned long k20 = seed + 21; unsigned long k21 = seed + 22; unsigned long k22 = seed + 23; unsigned long k23 = seed + 24;
    unsigned long acc = seed;
    for (unsigned long round = 0; round < rounds; round += 1) {
        acc += k0 ^ (acc << 1); acc += k1 ^ (acc << 2); acc += k2 ^ (acc << 3); acc += k3 ^ (acc << 4);
        acc += k4 ^ (acc << 5); acc += k5 ^ (acc << 6); acc += k6 ^ (acc << 7); acc += k7 ^ (acc << 1);
        acc += k8 ^ (acc << 2); acc += k9 ^ (acc << 3); acc += k10 ^ (acc << 4); acc += k11 ^ (acc << 5);
        acc += k12 ^ (acc << 6); acc += k13 ^ (acc << 7); acc += k14 ^ (acc << 1); acc += k15 ^ (acc << 2);
        acc += k16 ^ (acc << 3); acc += k17 ^ (acc << 4); acc += k18 ^ (acc << 5); acc += k19 ^ (acc << 6);
        acc += k20 ^ (acc << 7); acc += k21 ^ (acc << 1); acc += k22 ^ (acc << 2); acc += k23 ^ (acc << 3);
        unsigned long lane[(rounds & 3) + 1]; lane[rounds & 3] = acc; acc += lane[rounds & 3];
    }
    for (unsigned long round = 0; round < rounds; round += 1) { acc += sp_mix(acc ^ k0 ^ k23); }
    return acc ^ k1 ^ k2 ^ k3 ^ k4 ^ k5 ^ k6 ^ k7 ^ k8 ^ k9 ^ k10 ^ k11 ^ k12 ^ k13 ^ k14 ^ k15 ^ k16 ^ k17 ^ k18 ^ k19 ^
           k20 ^ k21 ^ k22;
}
