#if __has_embed("hi.txt") == 1 && __has_embed("hi.txt" limit(4)) == 1 && __has_embed("hi.txt" limit(5)) == 2 && __has_embed("hi.txt" limit(100)) == 2
limits
#endif
#if __has_embed("hi.txt" clang::offset(3)) == 1 && __has_embed("hi.txt" clang::offset(4)) == 2 && __has_embed("hi.txt" clang::offset(9)) == 2 && __has_embed("empty.txt") == 2
offsets
#endif
#if __has_embed(<hi.txt>) == 0 && __has_embed("hi.txt" foo) == 0 && __has_embed("hi.txt" foo(1)) == 0 && __has_embed("hi.txt" __if_empty__(x)) == 1 && __has_embed("hi.txt" clang::offset(1) limit(3)) == 1 && __has_embed("hi.txt" clang::offset(1) limit(4)) == 2
others
#endif
