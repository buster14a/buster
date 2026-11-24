#if __has_embed("/dev/null") == 2 && __has_embed("ppf_missing") == 0 && __has_embed(<ppf_missing>) == 0
ok
#endif
#if __has_embed("/dev/null" limit(0)) == 2 && __has_embed("/dev/null" prefix(x) suffix(y) if_empty(z)) == 2 && __has_embed("/dev/null" ppf::unknown) == 0
parameters
#endif
#if __has_embed("other.h" clang::offset(1) limit(2)) == 1
found
#endif
