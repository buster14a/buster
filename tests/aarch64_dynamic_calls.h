#pragma once

typedef struct DynamicCallPair { unsigned long long low, high; } DynamicCallPair;
typedef struct DynamicCallBig { unsigned long long a, b, c; } DynamicCallBig;
long long dynamic_host_scalar(long long, long long, long long, long long,
                              long long, long long, long long, long long,
                              unsigned char, short, int, unsigned char*);
DynamicCallPair dynamic_host_pair(long long, long long, long long, long long,
                                  long long, long long, long long, DynamicCallPair);
DynamicCallBig dynamic_host_big(long long, long long, long long, long long,
                                long long, long long, long long, long long, DynamicCallBig);
double dynamic_host_float(double, double, double, double, double, double, double, double, double);
long long dynamic_host_variadic(int, ...);
int dynamic_calls_scalar(int);
int dynamic_calls_results(int);
int dynamic_calls_variadic(int);
