// Two diagnostics at one place: the second shows no source line (clang's LastLoc rule).
int too_long = 'abcde';
// The same place's warnings again after another place's: the line shows again.
int other = 'ab';
	int tabbed_too_long = 'abcde';
