static_assert(1, "\N{LATIN SMALL LETTER E WITH ACUT}");

#define MESSAGE_BODY "\N{greek small letter alpha}"
static_assert(1, MESSAGE_BODY);

#define ARGUMENT(x) x
static_assert(1, ARGUMENT("\N{greek small letter alpha}"));

static_assert(1, "\N{LATIN SMALL \
LETTER E WITH ACUT}");
