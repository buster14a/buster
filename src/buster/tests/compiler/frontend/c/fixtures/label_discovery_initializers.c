// #2496: named-label discovery around brace contexts. Every colon below belongs to a
// ternary, bit-field width, case label or asm operand list and must not become a
// label, and every real label must still be found. Standard designators stand in for
// the obsolete `member:` form, which the compiler rejects (c_test_obsolete_designator_labels).
struct Pair { int member; int other; };
struct Wrap { struct Pair inner; int tail; };

int designated_local(void) { struct Pair p = { .member = 7 }; return p.member - 7; }
int designated_compound(void) { return ((struct Pair){ .member = 7 }).member - 7; }
int designated_two(void) { struct Pair p = { .member = 3, .other = 4 }; return p.member + p.other - 7; }
int designated_nested(void) { struct Wrap w = { .inner = { .member = 1, .other = 2 }, .tail = 3 }; return w.inner.member + w.inner.other + w.tail - 6; }
int designated_nested_compound(void) { struct Wrap *w = &(struct Wrap){ .inner = (struct Pair){ .other = 5 }, .tail = 1 }; return w->inner.other + w->tail - 6; }
int designated_array(void) { struct Pair a[2] = { { .member = 1 }, { .other = 2 } }; return a[0].member + a[1].other - 3; }
int designated_in_stmt_expr(void) { int v = ({ struct Pair p = { .member = 9 }; p.member; }); return v - 9; }
int designated_in_stmt_expr_label(void) { int n = 0; int v = ({ struct Pair p = { .other = 2 }; again: n += 1; if (n < 3) goto again; p.other + n; }); return v - 5; }
int designated_arg(int x) { return x; }
int designated_as_argument(void) { return designated_arg(((struct Pair){ .member = 4 }).member) - 4; }
int designated_then_real_label(void) { struct Pair p = { .member = 2 }; int n = 0; top: n += 1; if (n < p.member) goto top; return n - 2; }
int real_label_before_init(void) { int n = 0; first: n += 1; if (n < 2) goto first; struct Pair p = { .member = n }; return p.member - 2; }
int ternary_in_init(int c) { struct Pair p = { .member = c ? 1 : 2, .other = c ? 3 : 4 }; return p.member * 10 + p.other; }
int ternary_label(int c) { int r = 0; goto go; go: r = c ? 1 : 2; return r; }
int ternary_after_brace(int c) { int r = 0; { r = c ? 5 : 6; } return r; }
int ternary_brace_ident(int c, int a, int b) { int r = ({ c ? a : b; }); return r; }
int bitfield_local(void) { struct Bits { unsigned a : 3; unsigned b : 5; } bits = { .a = 5, .b = 17 }; return bits.a + bits.b - 22; }
int bitfield_decl_in_body(void) { struct { int x : 4; int y : 4; } s; s.x = 3; s.y = 2; return s.x + s.y - 5; }
int case_labels(int x) { int r = 0; switch (x) { case 1: r = 10; break; case 2: { r = 20; break; } default: r = 30; } return r; }
int case_in_nested_initializer_switch(int x) { struct Pair p = { .member = 1 }; switch (x) { case 0: p.member = 5; break; default: p.member = 6; } return p.member; }
int case_range_label(int x) { switch (x) { case 1 ... 3: return 1; default: return 0; } }
int label_after_case(int x) { switch (x) { case 1: lab: x += 1; if (x < 5) goto lab; return x; default: return -1; } }
int enum_local(void) { enum { first_value = 4 } e = first_value; return e - 4; }
int label_in_nested_braces(int x) { { { inner: x += 1; if (x < 4) goto inner; } } return x; }
int label_after_block(int x) { { x += 1; } after: x += 1; if (x < 5) goto after; return x; }
int label_after_label(int x) { one: two: x += 1; if (x < 2) goto one; if (x < 3) goto two; return x; }
int label_in_if_else(int x) { if (x) goto yes; else goto no; yes: return 1; no: return 0; }
int label_in_loops(int x) { while (x < 3) lp: x += 1; for (;;) { if (x > 5) break; ff: x += 1; } return x; }
int label_after_do(int x) { do again: x += 1; while (x < 4); return x; }
int label_in_compound_literal_scope(void) { int n = 0; again: n += ((struct Pair){ .member = 1 }).member; if (n < 3) goto again; return n; }
int asm_colons(void) { int r = 5;
#if defined(__x86_64__) || defined(__aarch64__)
    __asm__ volatile ("" : "+r"(r) : : "memory");
#endif
    done: return r - 5; }
int asm_goto_free_labels(void) { int r = 1; __asm__ volatile ("" : "=r"(r) : "0"(r) : "cc"); lbl: return r - 1; }
int label_named_like_member(void) { struct Pair p = { .member = 1 }; int n = 0; member: n += 1; if (n < 2) goto member; return p.member + n - 3; }
int label_named_like_member_in_stmt_expr(void) { int n = 0; int v = ({ struct Pair p = { .member = 1 }; other: n += 1; if (n < 2) goto other; p.member + n; }); return v - 3; }
int shadow_label_across_initializer(void) { int n = 0; struct Pair p = { .member = 0, .other = 0 }; loop: p.member += 1; n += p.member; if (p.member < 3) goto loop; return n - 6; }
int computed_goto(void) { void *t = &&here; goto *t; return 1; here: return 0; }
int main(void)
{
    if (designated_local()) return 1;
    if (designated_compound()) return 2;
    if (designated_two()) return 3;
    if (designated_nested()) return 4;
    if (designated_nested_compound()) return 5;
    if (designated_array()) return 6;
    if (designated_in_stmt_expr()) return 7;
    if (designated_in_stmt_expr_label()) return 8;
    if (designated_as_argument()) return 9;
    if (designated_then_real_label()) return 10;
    if (real_label_before_init()) return 11;
    if (ternary_in_init(1) != 13 || ternary_in_init(0) != 24) return 12;
    if (ternary_label(1) != 1) return 13;
    if (ternary_after_brace(0) != 6) return 14;
    if (ternary_brace_ident(1, 8, 9) != 8) return 15;
    if (bitfield_local()) return 16;
    if (bitfield_decl_in_body()) return 17;
    if (case_labels(1) != 10 || case_labels(2) != 20 || case_labels(3) != 30) return 18;
    if (case_in_nested_initializer_switch(0) != 5 || case_in_nested_initializer_switch(1) != 6) return 19;
    if (case_range_label(2) != 1 || case_range_label(9) != 0) return 20;
    if (label_after_case(1) != 5) return 21;
    if (enum_local()) return 22;
    if (label_in_nested_braces(0) != 4) return 23;
    if (label_after_block(0) != 5) return 24;
    if (label_after_label(0) != 3) return 25;
    if (label_in_if_else(1) != 1 || label_in_if_else(0) != 0) return 26;
    if (label_in_loops(0) != 6) return 27;
    if (label_after_do(0) != 4) return 28;
    if (label_in_compound_literal_scope() != 3) return 29;
    if (asm_colons()) return 30;
    if (asm_goto_free_labels()) return 31;
    if (label_named_like_member()) return 32;
    if (label_named_like_member_in_stmt_expr()) return 33;
    if (shadow_label_across_initializer()) return 34;
    if (computed_goto()) return 35;
    return 0;
}
