// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent Clang callees and callers for late transparent-union classification. Separate nominal tags make each
// expected convention explicit in the oracle; the tested unit applies its attributes later to the same tag instead.
#define BUSTER_CHECK_TEST_TRANSPARENT_LATE \
    "typedef union{struct{float x,y;}p;struct{int a,b;}q;}ExternalLatePlain;" \
    "typedef union __attribute__((aligned(32))){struct{float x,y;}p;struct{int a,b;}q;}ExternalLateOwn;" \
    "typedef union __attribute__((transparent_union)){struct{float x,y;}p;struct{int a,b;}q;}ExternalLateNew;" \
    "typedef union __attribute__((transparent_union,aligned(32))){struct{float x,y;}p;struct{int a,b;}q;}ExternalLateNewOwn;" \
    "unsigned long external_late_plain(ExternalLatePlain u){return (unsigned long)(u.q.a*17+u.q.b*31);}" \
    "unsigned long external_late_own(ExternalLateOwn u){return (unsigned long)(u.q.a*19+u.q.b*37);}" \
    "long external_late_changed(int k,ExternalLateNew u){return k+u.q.a*23+u.q.b*41;}" \
    "long external_late_changed_own(int k,ExternalLateNewOwn u){return k+u.q.a*29+u.q.b*43;}" \
    "unsigned long external_late_plain_apply(unsigned long(*f)(ExternalLatePlain),int n){ExternalLatePlain u={.q={n,n+2}};return f(u);}" \
    "unsigned long external_late_own_apply(unsigned long(*f)(ExternalLateOwn),int n){ExternalLateOwn u={.q={n,n+3}};return f(u);}" \
    "long external_late_changed_apply(long(*f)(int,ExternalLateNew),int n){ExternalLateNew u={.q={n,n+4}};return f(7,u);}" \
    "long external_late_changed_own_apply(long(*f)(int,ExternalLateNewOwn),int n){ExternalLateNewOwn u={.q={n,n+5}};return f(11,u);}" \
    "unsigned long external_late_var_plain(ExternalLatePlain u,...){return external_late_plain(u);}" \
    "unsigned long external_late_var_new(ExternalLateNew u,...){return (unsigned long)(u.q.a*47+u.q.b*53);}" \
    "unsigned long external_late_var_own(ExternalLateOwn u,...){return external_late_own(u);}" \
    "unsigned long external_late_var_new_own(ExternalLateNewOwn u,...){return (unsigned long)(u.q.a*59+u.q.b*61);}" \
    "unsigned long external_late_tail_plain(int k,ExternalLatePlain u){return k+external_late_plain(u);}" \
    "unsigned long external_late_tail_own(int k,ExternalLateOwn u){return k+external_late_own(u);}" \
    "unsigned long external_late_zero(ExternalLateNew u){return (unsigned long)(u.q.a*67+u.q.b*71);}" \
    "unsigned long external_late_zero_own(ExternalLateNewOwn u){return (unsigned long)(u.q.a*73+u.q.b*79);}"
