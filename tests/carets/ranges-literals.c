// An escape sequence's characters highlighted inside its literal (#2376), through a macro argument and a line splice.
#define ID(x) x
char plain[] = "\q";
char argument[] = ID("ab\q");
char spliced[] = "\
\q";
char character = '\q';
