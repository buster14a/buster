#if _Pragma("ppf a") 1
yes
#endif
#line _Pragma("ppf b") 5
__LINE__
#pragma message(_Pragma("ppf c") "s")
__has_attribute(_Pragma("ppf d") x) after
