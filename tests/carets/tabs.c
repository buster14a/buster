// Tabs to stops 8 columns apart, counted in bytes from the previous tab or the line's start.
	#warning after a tab
  	#warning after two spaces and a tab
		#warning after two tabs
/* 1234567	*/	#warning after a comment of 9 and a tab
int	tabbed	=	'ab';
/*é	*/	#warning a tab counts the bytes before it, not the columns
