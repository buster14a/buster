/* A `.S` is assembly the C preprocessor runs over first.  The status below
   comes from a macro through the `$` immediate prefix -- the spelling the
   C lexer would glue onto the name -- so an unexpanded macro fails to
   assemble rather than encoding the wrong immediate. */

# GNU-as commentary: a hash line whose word is no directive.

#define BASIC_ASM_PREPROCESSED_STATUS 0

.text
.globl basic_asm_preprocessed
basic_asm_preprocessed:
	mov $BASIC_ASM_PREPROCESSED_STATUS,%eax
	ret
