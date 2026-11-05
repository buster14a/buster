// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1192: original constant-switch selection fixtures. Every case compares the actual ELF's functions, objects and
// literals with Clang -O0, executes against Clang, and requires scalar/vector and eager/lazy byte-identical objects.
// Run both ordinary selection and the corpus's unselected-operands model. Included after the selection harness.

static void check_test_program_folded_switches(CheckTest* test, const char* clang)
{
    typedef struct CheckFoldedSwitch CheckFoldedSwitch;
    struct CheckFoldedSwitch
    {
        const char* name;
        const char* source;
        uint32_t functions;
        uint32_t objects;
    };
    static const CheckFoldedSwitch fixtures[] =
    {
        {
            .name = "folded-switch-targeted-unselected-callee",
            .source = "static inline __attribute__((always_inline,target(\"avx2\"))) int h(int x){return x+1;}"
                      "static int vec(int x){return h(x);}int tested(int x){switch(1){case 0:return vec(x);case 1:return x;}return 0;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-sizeof",
            .source = "static int heavy(int x){return x*3;}"
                      "int tested(int x){switch(sizeof(long)==8){case 0:x=heavy(x);break;case 1:x+=1;break;}return x;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-fallthrough-and-case-chain",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:case 1:n+=1;case 2:n+=2;break;"
                      "case 3:n=heavy(n);break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-default",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(9){case 0:n=heavy(n);break;default:n+=1;break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-no-match",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(9){int unused;case 0:n=heavy(n);break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-promoted-unsigned-value",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch((unsigned)-1){case 0:n=heavy(n);break;case -1:n+=1;break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-range-before-match-in-source",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 3 ... 5:n=heavy(n);break;case 1:n+=1;break;}return n;}",
            .functions = 1,
        },
        {
            .name = "unfolded-switch-range-before-match-in-scan",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 1:n+=1;break;case 3 ... 5:n=heavy(n);break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-skipped-label",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:again:n=heavy(n);break;case 1:n+=1;break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-label-after-break",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 1:n+=1;break;case 0:again:n=heavy(n);break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-skipped-declaration",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:;int unused=heavy(n);break;case 1:n+=1;break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-inner-break",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;case 1:if(n)break;n+=1;break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-constant-p-variable",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(__builtin_constant_p(n)){case 0:return n;case 1:return heavy(n);}return 0;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-comma-variable",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch((n,1)){case 0:n=heavy(n);break;case 1:n+=1;break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-selector-side-effect",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch((n++,1)){case 0:n=heavy(n);break;case 1:n+=1;break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-case-inside-if",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;if(n){case 1:n+=1;}break;}return n;}",
            .functions = 2,
        },
        {
            .name = "unfolded-switch-fallthrough-declaration",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;case 1:;int x=2;n+=x;}return n;}",
            .functions = 2,
        },
        {
            .name = "folded-switch-nested-loop-owns-break",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;case 1:while(n<0){n+=1;break;}break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-skipped-compound-declaration",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:{int x=heavy(n);n=x;break;}case 1:n+=1;break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-complete-compound-scope",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;case 1:{int x=2;n+=x;}break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-compound-terminal-break",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;case 1:{int x=2;n+=x;break;}}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-cleanup-at-break",
            .source = "static int total;static void clean(int*p){total+=*p;}static int heavy(int x){return x+3;}"
                      "int tested(int n){total=0;switch(1){case 0:n=heavy(n);break;case 1:{int x __attribute__((cleanup(clean)))=3;n+=x;break;}}return n+total;}",
            .functions = 2,
            .objects = 1,
        },
        {
            .name = "folded-switch-cleanup-at-compound-end",
            .source = "static int total;static void clean(int*p){total+=*p;}static int heavy(int x){return x+3;}"
                      "int tested(int n){total=0;switch(1){case 0:n=heavy(n);break;case 1:{int x __attribute__((cleanup(clean)))=3;n+=x;}break;}return n+total;}",
            .functions = 2,
            .objects = 1,
        },
        {
            .name = "folded-switch-skipped-vla",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:{int a[(n&3)+1];a[0]=heavy(n);n=a[0];break;}case 1:n+=1;break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-kept-vla-break",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;case 1:{int a[(n&3)+1];a[0]=n+1;n=a[0];break;}}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-skipped-statement-expression-static",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=({static int x=3;heavy(n+x);});break;case 1:n+=1;break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-skipped-used-static",
            .source = "int tested(int n){switch(1){case 0:{static int x __attribute__((used))=3;n+=x;break;}case 1:n+=1;break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-nested-switch",
            .source = "static int heavy(int x){return x+3;}int tested(int n){switch(1){case 0:n=heavy(n);break;case 1:switch(2){case 0:n=heavy(n);break;case 2:n+=1;break;}break;}return n;}",
            .functions = 1,
        },
        {
            .name = "folded-switch-without-compound",
            .source = "int tested(int n){switch(1)case 1:return n;return 0;}",
            .functions = 1,
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const CheckFoldedSwitch* fixture = fixtures + i;
        check_test_program_selection_run(test, fixture->source, fixture->name, fixture->functions, fixture->objects,
                                         CHECK_PROGRAM_OK, clang, clang, NULL, false);
        char name[160];
        snprintf(name, sizeof(name), "%s-in-clangs-model", fixture->name);
        check_test_program_selection_run(test, fixture->source, name, fixture->functions, fixture->objects,
                                         CHECK_PROGRAM_OK, clang, clang, NULL, true);
    }
}
