#include <stdbool.h>
#include <stddef.h>

typedef void* BusterWinHandle;
typedef unsigned long BusterWinDword;
typedef int BusterWinBool;

__declspec(dllimport) BusterWinHandle __stdcall GetStdHandle(BusterWinDword handle);
__declspec(dllimport) BusterWinBool __stdcall WriteFile(BusterWinHandle file, void const* buffer, BusterWinDword bytes_to_write, BusterWinDword* bytes_written, void* overlapped);

#define BUSTER_STD_ERROR_HANDLE ((BusterWinDword)-12)

static BusterWinDword
buster_sanitizer_win_string_length(char const* string)
{
    BusterWinDword result = 0;
    while (string[result] != '\0') {
        result += 1;
    }
    return result;
}

__attribute__((used)) bool buster_sanitizer_win_continue_on_error(void) __asm__("?ContinueOnError@__coe_win@@YA_NXZ");
bool
buster_sanitizer_win_continue_on_error(void)
{
    return true;
}

__attribute__((used)) void buster_sanitizer_win_raw_write(char const* message) __asm__("?RawWrite@__coe_win@@YAXPEBD@Z");
void
buster_sanitizer_win_raw_write(char const* message)
{
    if (message == NULL) {
        return;
    }

    BusterWinHandle error_file = GetStdHandle(BUSTER_STD_ERROR_HANDLE);
    if (error_file == NULL) {
        return;
    }

    BusterWinDword length = buster_sanitizer_win_string_length(message);
    while (length > 0) {
        BusterWinDword written = 0;
        BusterWinBool ok = WriteFile(error_file, message, length, &written, NULL);
        if (!ok || written == 0) {
            return;
        }
        message += written;
        length -= written;
    }
}
