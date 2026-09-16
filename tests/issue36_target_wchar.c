/* Header-free target ABI regression for issue #36's AArch64 musl typedef
   mismatch. This must be compiled for the target, not merely run on the host.
   Intended reference lanes: x86_64/AArch64 Linux, Windows, Darwin, Android.
   No change to census applicability or retained-unsupported classification. */
#if defined(_WIN32) || defined(_WIN64)
typedef unsigned short issue36_expected_wchar_t;
#elif (defined(__aarch64__) || defined(__arm64__)) && (defined(__linux__) || defined(__ANDROID__))
typedef unsigned int issue36_expected_wchar_t;
#else
typedef int issue36_expected_wchar_t;
#endif

typedef __WCHAR_TYPE__ issue36_wchar_macro_type;
typedef issue36_expected_wchar_t issue36_wchar_macro_type;

/* Repeat the typedef, just as a resource stddef.h and a target libc do. */
typedef __WCHAR_TYPE__ wchar_t;
typedef issue36_expected_wchar_t wchar_t;

/* Detect a macro-only repair that leaves wide literals with another type. */
typedef __typeof__(L'x') issue36_wide_character_type;
typedef issue36_expected_wchar_t issue36_wide_character_type;
typedef __typeof__(L"x"[0]) issue36_wide_string_element_type;
typedef issue36_expected_wchar_t issue36_wide_string_element_type;

int main(void)
{
    return 0;
}
