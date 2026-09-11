#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
void predicate_mask_chain(unsigned char*, unsigned char const*);
unsigned long long predicate_word_boundary(unsigned char const*);
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
int main(void) {
    unsigned char input[64], output[64];
    for (unsigned i = 0; i < 64; ++i) input[i] = (unsigned char)(i % 16);
    memset(output, 0xa5, sizeof(output));
    uint64_t begin = now_ns();
    for (unsigned i = 0; i < 10000000; ++i) predicate_mask_chain(output, input);
    uint64_t elapsed = now_ns() - begin;
    unsigned checksum = 0;
    for (unsigned i = 0; i < 64; ++i) {
        unsigned char expected = input[i] == 3 || input[i] == 7 ? input[i] : 0xa5;
        if (output[i] != expected) return 1;
        checksum += output[i];
    }
    if (predicate_word_boundary(input) != 65535) return 2;
    printf("{\"elapsed_ns\":%llu,\"calls\":10000000,\"checksum\":%u}\n", (unsigned long long)elapsed, checksum);
    return 0;
}
