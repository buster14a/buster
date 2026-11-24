#if 1
#embed <dir//bytes.bin> limit(1) /* comment
#endif
*/
#else
bad
#endif
good
#ifdef __STDC__
#embed <dir//bytes.bin> limit(1) /* comment
#endif
*/
#elifdef __STDC__
bad
#else
#ifndef X
bad
#elifndef Y
bad
#else
bad
#endif
#if 1
#endif
bad
#endif
good2
