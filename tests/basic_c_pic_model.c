// The -fPIC code model, in the four references it has to tell apart. A
// definition with external linkage and an undefined one are both symbols
// another object in the image could supply, so their addresses come out of
// the GOT; a static definition is this module's alone and stays rip-relative;
// a direct call to an interposable function asks for its procedure linkage
// entry. libc-test's functional/dlopen_dso is the first of these four in one
// line, and it is what `ld -shared` refused before the flag meant anything.
int buster_pic_model_global = 1;
static int buster_pic_model_static = 2;
extern int buster_pic_model_external;
extern void buster_pic_model_callee(void);

void buster_pic_model_bump(void)
{
    buster_pic_model_global += 1;
}

int buster_pic_model_read(void)
{
    return buster_pic_model_global + buster_pic_model_static + buster_pic_model_external;
}

void buster_pic_model_call(void)
{
    buster_pic_model_bump();
    buster_pic_model_callee();
}

void (*buster_pic_model_taken)(void) = 0;

void buster_pic_model_take(void)
{
    buster_pic_model_taken = buster_pic_model_callee;
}
