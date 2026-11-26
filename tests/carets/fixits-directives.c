/* Extra directive tokens are commented out only where line comments are enabled. */
#define PRESENT 1
#ifdef PRESENT trailing
#endif trailing
#ifndef ABSENT trailing
#else trailing
#endif trailing
#if 0
#elifdef PRESENT trailing
#endif trailing
#if 0
#elifndef ABSENT trailing
#endif trailing
#undef PRESENT trailing
#ident "identity" trailing
#pragma clang assume_nonnull begin trailing
#pragma clang assume_nonnull end trailing
#pragma once trailing

/* These diagnostics have no repair. */
#define NO_SPACE+1
#defien INVALID 1

#define IDENT_BODY "identity" trailing
#ident IDENT_BODY
#define PRAGMA_BODY _Pragma("once trailing")
PRAGMA_BODY
#define ARGUMENT(x) x
#define LINE_BODY "directive-body.c" trailing
#line 40 LINE_BODY
#line 50 ARGUMENT("directive-argument.c" trailing)
#line 60 "directive-file.c" trailing
#error hints must not survive suppressed warnings
