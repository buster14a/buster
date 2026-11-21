// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Exercise label, declaration and call recovery together in the same private views.

int known(int);

void first(int x)
{
    __attribute__((suppress(1), minsize)) retry: x++;
#if __STDC_VERSION__ >= 199901L
    __attribute__((unused)) (x)++;
#endif
#if __STDC_VERSION__ < 202311L
    __attribute__((unused)) undeclared_fn(unread_first);
#endif
    int *tail = 1;
    (void)tail;
    after_first;
}

void second(int x)
{
    __attribute__((minsize, suppress(1))) again: x++;
#if __STDC_VERSION__ >= 199901L
    __attribute__((unused)) (x)++;
#endif
#if __STDC_VERSION__ < 202311L
    __attribute__((unused)) known(unread_second);
#endif
    int *tail = 2;
    (void)tail;
    after_second;
}
