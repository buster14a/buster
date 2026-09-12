#ifndef BUSTER_TEST_SYSV_SSEUP_H
#define BUSTER_TEST_SYSV_SSEUP_H

typedef unsigned long long SseupWord;
typedef unsigned int SseupVector __attribute__((vector_size(16)));
typedef struct SseupWrapper { SseupVector value; } SseupWrapper;
typedef struct SseupNested { SseupWrapper values[1]; } SseupNested;
typedef union SseupFloatOverlay { SseupVector vector; double halves[2]; } SseupFloatOverlay;
typedef union SseupIntegerOverlay { SseupVector vector; SseupWord halves[2]; } SseupIntegerOverlay;
typedef union SseupHeadOverlay { SseupVector vector; SseupWord head; } SseupHeadOverlay;

SseupWrapper sseup_echo(SseupWrapper value);
SseupNested sseup_nested(SseupNested value);
SseupFloatOverlay sseup_float_overlay(SseupFloatOverlay value);
SseupIntegerOverlay sseup_integer_overlay(SseupIntegerOverlay value);
SseupHeadOverlay sseup_head_overlay(SseupHeadOverlay value);
SseupWrapper sseup_mixed(SseupWord key, SseupWrapper first, double scalar, SseupWrapper second, SseupWord* observed);
SseupWrapper sseup_ninth(SseupWrapper a, SseupWrapper b, SseupWrapper c, SseupWrapper d, SseupWrapper e,
                         SseupWrapper f, SseupWrapper g, SseupWrapper h, SseupWrapper i, double tail);
SseupWrapper sseup_variadic(int count, ...);
SseupWrapper sseup_after_doubles(int count, ...);
int sseup_call_host(void);
int sseup_host_observe(SseupWrapper value, double tail, SseupWord key);
SseupWrapper sseup_host_return(SseupWord low, SseupWord high);
int sseup_host_variadic(int count, ...);

#endif
