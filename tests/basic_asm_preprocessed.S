/* A `.S` is assembly the C preprocessor runs over first.  The status below
   comes from an ordered command-line macro through the `$` immediate prefix
   -- the spelling the C lexer would glue onto the name -- so a regrouped or
   unexpanded macro fails to assemble rather than encoding the wrong value. */

# GNU-as commentary: a hash line whose word is no directive.

.text
.globl basic_asm_preprocessed
basic_asm_preprocessed:
	mov $BASIC_ASM_PREPROCESSED_STATUS,%eax
	ret
