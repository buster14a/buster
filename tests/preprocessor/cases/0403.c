#if __has_include(<x.h>>)
wrong1
#endif
#if __has_include(<x.h>=)
wrong2
#endif
#if __has_include(<x.h>>=)
wrong3
#endif
#if __has_include(<a\\>b.h>)
wrong4
#endif
end
