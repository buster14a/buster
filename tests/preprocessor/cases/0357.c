#define BUILTIN_NAME __builtin_expect
#define FEATURE_NAME c_atomic
#define ATTRIBUTE_NAME __const__
#if __has_builtin(__is_target_arch) && __has_builtin(__is_target_os)
builtin_names
#endif
#if __has_builtin(BUILTIN_NAME) || __has_feature(FEATURE_NAME) || __has_extension(FEATURE_NAME)
wrong_expansion
#else
raw_names
#endif
#if __has_attribute(ATTRIBUTE_NAME) && __is_target_arch(x86_64) && __is_target_os(linux)
attribute_and_target
#endif
