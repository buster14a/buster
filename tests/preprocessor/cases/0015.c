#define quote(v) # v
#define xquote(v) quote(v)
#define trace(a, b) log("v" # a " is %d, v" # b " is %s", \
 v ## a, v ## b)
#define PART_OF(n) part ## n
#define cat(a, b) a ## b
#define xcat(a, b) cat(a, b)
#define UPDOWN "top"
#define DOWN DOWN " bottom"
trace(3, 4);
puts(quote(strcmp("a\0b", "a", '\5') // dropped
 != 0) quote(! #\n), out);
xquote(PART_OF(7).inc)
cat(UP, DOWN);
xcat(UP, DOWN)
