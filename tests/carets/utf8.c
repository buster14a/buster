// Characters past ASCII: each as its columns (a combining mark none, a wide character two), or escaped.
/* Ã©ä¸­æ–‡ */ #warning after a Latin letter and two wide characters
/* eÌ combining */ #warning after a combining mark
/* ğŸ˜€ emoji */ #warning after an emoji
/* â€‹ zero width */ #warning after a zero-width space, which is escaped
/*  */ #warning after controls, escaped as code points
/* ÿÀ€í € */ #warning after malformed bytes, escaped one at a time
/* ä¸ */ #warning after a truncated sequence
/* øˆ€€€ */ #warning after a five-byte form
/* ä¸­æ–‡ */ int too_long = 'abcde';
/* Â­ soft hyphen */ #warning after a soft hyphen
/* ğ–¶€ draft */ #warning after a draft character LLVM calls printable
