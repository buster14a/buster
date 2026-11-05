// Original transient Clang oracle probes for issue 1447.
#if CASE == 1
#pragma weak value
static int value = 7;
int read_value(void) { return value; }
#elif CASE == 2
static int value;
#pragma weak value
int read_value(void) { return value; }
#elif CASE == 3
#pragma weak value
typedef int value;
#elif CASE == 4
typedef int value;
#pragma weak value
#elif CASE == 5
#pragma weak value
const int value = 7;
#elif CASE == 6
extern int value;
#pragma weak value
int value = 7;
#elif CASE == 7
extern int value;
int read_value(void) { return value; }
#pragma weak value
int value = 7;
#elif CASE == 8
extern int value;
int read_value(void) {
#pragma weak value
    return value;
}
int value = 7;
#elif CASE == 9
int value = 7;
#pragma weak alias = value
#elif CASE == 10
#pragma weak alias = value
int value = 7;
#elif CASE == 11
#pragma weak alias = missing
#elif CASE == 12
static int value = 7;
#pragma weak alias = value
#elif CASE == 13
#pragma redefine_extname value renamed
static int value = 7;
int read_value(void) { return value; }
#elif CASE == 14
extern int value;
#pragma redefine_extname value renamed
int value = 7;
#elif CASE == 15
extern int value;
int read_value(void) { return value; }
#pragma redefine_extname value renamed
int value = 7;
#elif CASE == 16
int value;
#pragma redefine_extname value renamed
#elif CASE == 17
int value = 7;
#pragma redefine_extname value renamed
#elif CASE == 18
#pragma redefine_extname value renamed
#pragma redefine_extname value renamed
int value = 7;
#elif CASE == 19
#pragma redefine_extname value renamed
#pragma redefine_extname value other
int value = 7;
#elif CASE == 20
#pragma redefine_extname value renamed
int value asm("other") = 7;
#elif CASE == 21
extern int value asm("other");
#pragma redefine_extname value renamed
int value = 7;
#elif CASE == 22
#define SOURCE value
#define DEST renamed
#pragma redefine_extname SOURCE DEST
int value = 7;
#elif CASE == 23
#pragma weak
int value;
#elif CASE == 24
#pragma weak alias =
int value;
#elif CASE == 25
#pragma redefine_extname value
int value;
#elif CASE == 26
int value = 7;
int alias = 9;
#pragma weak alias = value
#elif CASE == 27
extern int alias;
#pragma weak alias = value
int value = 7;
#elif CASE == 28
#pragma weak function
static int function(void) { return 7; }
int read_value(void) { return function(); }
#elif CASE == 29
int function(void);
#pragma redefine_extname function renamed
int function(void) { return 7; }
#elif CASE == 30
int function(void);
int read_value(void) { return function(); }
#pragma redefine_extname function renamed
int function(void) { return 7; }
#elif CASE == 31
#pragma weak alias = function
int function(void) { return 7; }
#elif CASE == 32
extern int value asm("renamed");
#pragma weak value
int value = 7;
#elif CASE == 33
#pragma weak alias = middle
#pragma weak middle = value
int value = 7;
#elif CASE == 34
#pragma redefine_extname value "renamed"
int value = 7;
#elif CASE == 35
#pragma weak value
#pragma redefine_extname value renamed
int value = 7;
#elif CASE == 36
int value = 7;
int read_value(void) {
    int value = 9;
#pragma weak value
    return value;
}
#elif CASE == 37
int read_value(void) {
#pragma weak value
    return 0;
}
int value = 7;
#elif CASE == 38
int function(void) { return 7; }
#pragma weak function
#endif
