// Preprocessor-only build-driver fixture. Clang intentionally defines GNU
// compatibility macros; test its own identity before accepting __GNUC__.
#if defined(__clang__)
BUSTER_BUILD_COMPILER_CLANG
#elif defined(__GNUC__)
BUSTER_BUILD_COMPILER_GNU
#else
BUSTER_BUILD_COMPILER_UNKNOWN
#endif
