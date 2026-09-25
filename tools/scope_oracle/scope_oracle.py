#!/usr/bin/env python3
"""scope_oracle.py -- independent C17 oracle: constant name binding in type-level contexts.

Scope of the oracle: *constant name binding in type-level and constant contexts*
on x86-64 System V (LP64).

Stdlib-only Python 3.  A program is generated from (family, seed) as an ABSTRACT
syntax tree (classes below).  The tree is (a) rendered to C text and (b) run
through `Model`, an independent interpreter of the C17 scope rules and of the
x86-64 psABI layout rules, which yields the expected `label=value` stdout lines.
The model never parses the rendered C and never runs a compiler; compilers are
used only by `audit`, to check the model against clang and gcc.

USAGE
  scope_oracle.py --list-families
  scope_oracle.py render --family D1 --seed 7 --out prog.c --expect prog.expect
  scope_oracle.py corpus --families all --seeds 0-199 --out DIR
  scope_oracle.py audit  --families D1,D2,H1,H2,H3,H4,H5,H6 --seeds 0-199 \
                         --cc clang --cc gcc --work DIR [--opts O0,O2] [--jobs N]
  (--families accepts a comma list of family ids and the groups design, heldout, all)

OUTPUT CONTRACT OF EVERY GENERATED PROGRAM
  * includes only <stddef.h> (offsetof) and <stdio.h> (printf);
  * prints every observable as one `label=value` line, via
      printf("%s=%zu\n", "label", <size_t expr>)   sizes, alignments, offsets, bounds
      printf("%s=%d\n",  "label", <int expr>)      enumeration constants
      printf("%s=%u\n",  "label", (unsigned int)v.m)  bit-field read-back (H1)
  * returns 0 from main; nothing is encoded in the exit status;
  * labels are unique: <function>.<site>.<consumer>.<target>.

ABSTRACT GRAMMAR (common to all families)
  TU        := prelude FileItem* Helper* Main
  prelude   := #include <stddef.h>  #include <stdio.h>
               [static unsigned int ones_(void) { return ~0u; }]        (H1 only)
  FileItem  := EnumDecl | RecDecl | ObjDecl(static) | Snapshot
  Helper    := static void fK(void) { Item* }         called exactly once from main
  Main      := int main(void) { (Item | fK();)* return 0; }
  Item      := EnumDecl | RecDecl | ObjDecl | Snapshot | Observe | Block
  Block     := { Item* }
             | if (1) { Item* }
             | do { Item* } while (0);
             | for (int itK = 0; itK < 1; ++itK) { Item* }
             | switch (1) { case 1: { Item* } break; default: break; }
             | if (sizeof(EnumSpec)) { Item* }                          (H4, H5)
             | if (sizeof(EnumSpec)) Print                              (H4)
             | switch (sizeof(EnumSpec)) { default: { Item* } }         (H4, H5)
  EnumDecl  := EnumSpec ;
  EnumSpec  := enum [enK] { Enumerator (, Enumerator)* }
  Enumerator:= ID [= CExpr]
  RecDecl   := (struct|union) TAG { Field+ } ;
  Field     := (_Alignas(CExpr))* Base ID ['[' CExpr ']'] [: CExpr] ;
  Base      := prim | char * | (struct|union) TAG
             | (struct|union) TAG { Field+ }          nested definition (H6)
             | EnumSpec *                             enum declared inside a member (H5, H6)
  prim      := char | short | int | unsigned int | long | long long | float | double
  ObjDecl   := [static] (prim | char * | prim ID '[' CExpr ']') ID [= 0 | = {0}] ;  (H3)
  Snapshot  := enum { snapK = CExpr };     (value captured at the declaration point,
                                            printed later, possibly in another scope)
  CExpr     := INT | ID | sizeof ID | sizeof(ID) | sizeof(TypeName) | _Alignof(TypeName)
             | offsetof(TAGTYPE, m(.m)*) | CExpr (+|-|*) CExpr
  TypeName  := prim | char[CExpr] | (struct|union) TAG | typedef-name
  Observe   := a run of Print lines, the consumers being
      val     NAME                                   printf %d   (value seen by an expression)
      tn      sizeof(char[NAME])                     type-name context
      td      typedef char tdK[NAME]; sizeof(tdK)    typedef context
      sizeof  sizeof NAME | sizeof(NAME)             (H3: objects / mixed names)
      sz      sizeof(struct T)
      al      _Alignof(struct T)
      off     offsetof(struct T, m[.m])
      esz     enum { eszK = sizeof(struct T) }; then eszK (%d)
      bound   sizeof(((struct T*)0)->m) / sizeof(((struct T*)0)->m[0])
      snap    a Snapshot constant
      bf      struct T bvK = {0}; bvK.m = ones_(); (unsigned int)bvK.m   (H1)

FAMILIES (each selectable by --families)
  design
   D1  One (sometimes two) enumeration-constant names, declared at file scope, at
       function scope and in nested blocks (depth <= 2); at every scope 1-2 structs
       whose member array bounds use the name (NAME, NAME+k, NAME-k, NAME*2,
       sizeof(char[NAME])).  Within a scope, declarations/structs/observations are
       shuffled, so a struct may precede the local enum and must bind the outer
       constant.  A nested block is always the LAST item of its scope (no
       observation after a block closes; that is H4).  main may shadow the name
       itself before/after calling the helper.
   D2  2-4 sibling functions, each declaring a same-named local enumeration
       constant (various enumerator-list shapes, implicit values) and a local
       struct -- often with the SAME tag in every sibling -- whose bound uses it.
       Some siblings declare no local constant (bind the file-scope one) or declare
       it after their first struct.  The file-scope constant may be declared
       between siblings (earlier siblings cannot see it).  main calls the siblings
       in shuffled order.
  held-out
   H1  D1 skeleton; structs contain unsigned int bit-fields whose widths are
       NAME, NAME+k, NAME*2, 32-NAME or sizeof(char[NAME]).  Widths are
       observed only by storing all-ones and printing the value back
       (2^w - 1); size/alignment/offsets of bit-field structs are never printed.
   H2  D1 skeleton; members carry _Alignas(NAME), _Alignas(NAME*2),
       _Alignas(sizeof(char[NAME])), sometimes two specifiers; NAME takes values
       in {1,2,4,8,16}.  Observed via _Alignof, offsetof, sizeof.
   H3  D1 skeleton; the shadowed names are OBJECTS (scalars, pointers, arrays,
       self-sized arrays `char obj[sizeof obj + k]` whose bound sees the OUTER obj)
       and optionally a MIXED name that is an enumeration constant in some scopes
       and an object in others.  Member bounds use sizeof obj / sizeof(obj) /
       sizeof obj + k / sizeof obj * 2.
   H4  Scope end.  A function performs 2-4 segments: observe, open a block of any
       kind (incl. declarations in if/switch controlling expressions, C17 6.8.4p3),
       shadow and use the name inside, close it, then observe and define structs
       AFTER it (must bind the outer declaration again); nested twice sometimes.
   H5  D1 skeleton with self-referential enumerators in inner scopes:
       enum { N = N + k }, { N = N * 2 }, { X = N, N = X + k }, { N = N + k, M = N + j },
       { X = N + k - 1, N }, { N = sizeof(char[N]) + k }; also inside a struct
       member (enum { N = N + k } *p;) and in if/switch controlling expressions.
   H6  D1 skeleton with unions, nested struct/union definitions inside members,
       members of previously defined record types (and arrays of them), and
       enumeration constants declared inside a member declaration
       (enum { N = v } *p;), whose scope is the ENCLOSING scope.

PRECONDITIONS ENFORCED (the generator constructs them, the model re-checks them
and raises ModelError on violation)
  * every identifier used is visible at its use; no identifier is declared twice in
    one scope (6.7p3); tags are unique per program except D2 siblings, whose scopes
    are disjoint; no struct is redefined in a scope (6.7.2.3p1);
  * array bounds are integer constant expressions > 0 (6.7.6.2p1) -- so no member
    is variably modified (6.7.2.1p9) and no zero-length array exists;
  * enumerator values fit in int (6.7.2.2p2), all small and positive;
  * bit-fields are `unsigned int` (6.7.2.1p5), widths 1..32 (6.7.2.1p4); no
    signedness of plain-int bit-fields is relied upon; never offsetof'd;
  * _Alignas values are powers of two <= 16 (fundamental alignments,
    _Alignof(max_align_t) == 16) and >= the member type's natural alignment
    (6.7.5p4, p5); never on bit-fields (6.7.5p2);
  * objects are only ever operands of sizeof (not evaluated, 6.5.3.4p2) or are
    zero-initialised; no member array is written; `((struct T*)0)->m` appears only
    inside sizeof;
  * the only implementation-defined facts relied upon are the LP64 psABI sizes and
    alignments (char 1, short 2, int/unsigned/float 4, long/long long/double/
    pointer 8), psABI member placement, and _Alignof(max_align_t) == 16.

WHY THE LESS COMMON CONSTRUCTS ARE VALID C17
  * `struct T { enum { N = 3 } *p; char a[N]; };` -- a struct-declaration's
    specifier-qualifier-list may hold an enum-specifier with a list (6.7.2.1p1,
    6.7.2.2p1); its constants get the enclosing block/file scope (6.2.1p4), so N is
    visible after the struct and must not already be declared in that scope.
  * `struct O { struct I { ... } m; };` -- likewise the tag I is declared in the
    enclosing scope and is usable after O (6.7.2.3, 6.2.1p4).
  * `if (sizeof(enum { N = 9 })) ...` / `switch (sizeof(enum { N = 9 }))` -- a
    type-name may declare enumeration constants (6.7.7); the selection statement is
    a block (6.8.4p3), so N ends with the statement; the value of sizeof(enum type)
    is implementation-defined but only its non-zeroness / `default:` matters.  A
    redeclaration of N inside the compound substatement is a different (inner)
    block in C (unlike C++'s condition rule) and is generated in H4/H5.
  * `char obj[sizeof obj + 2] = {0};` -- the inner obj's scope starts after its
    declarator (6.2.1p7), so the bound reads the outer, non-VLA obj: an integer
    constant expression (6.6p6), hence not a VLA.
  * `enum { N = N + 1 };` -- the enumerator's own scope starts after it (6.2.1p7),
    so the right-hand N is the outer constant (6.6p6 ICE).
  * `sizeof(((struct T *)0)->m) / sizeof(((struct T *)0)->m[0])` -- operands of
    sizeof are not evaluated unless VLA (6.5.3.4p2), so no null dereference.
  * `typedef char tdK[N];` at block scope -- N is an ICE, so not a VLA typedef.
  * block-scope `enum { eszK = sizeof(struct T) };` after statements -- C99+ mixed
    declarations and code; sizeof yields an ICE that fits int (6.7.2.2p2).

NOT GENERATED (implementation-defined, constraint violations, or out of subset)
  * size/alignment/offsets of records containing bit-fields (allocation unit is
    implementation-defined, 6.7.2.1p11), plain-int bit-fields (signedness,
    6.7.2.1p10), bit-field records as member/object types;
  * _Alignas > 16 (extended alignment support is implementation-defined, 6.2.8p3);
  * the size of an enumerated type itself (6.7.2.2p4) -- enum types appear only
    behind pointers or as a non-zero sizeof operand;
  * enumeration constants in a for-clause declaration (constraint 6.8.5p3);
  * VLAs, zero/negative bounds, flexible array members, anonymous members,
    long double, _Bool bit-fields, division in constant expressions;
  * tag shadowing across nested scopes (tags are unique; only D2 siblings reuse
    a tag, in disjoint scopes) -- the subset is about ordinary identifiers;
  * reading objects (they are only sizeof operands) and writing member arrays;
  * in D1/D2/H1/H2/H3/H5/H6 any use after a nested block closes in the same
    function (reserved for H4 so that scope END stays a held-out behaviour).

MODEL (C17 clauses)
  6.2.1p2,p4  block scope ends at the closing brace; an inner declaration hides an
              outer one; struct member lists are not scopes, so tags and
              enumeration constants declared inside a member belong to the
              enclosing block/file scope.
  6.2.1p7     a tag's scope begins right after the tag; an enumeration constant's
              right after its enumerator (so `enum { N = N + 1 }` reads the outer N
              and later enumerators of the same list see the new one); any other
              identifier's right after its declarator (so `char obj[sizeof obj]`
              reads the outer obj).
  6.8.2, 6.8.4p3, 6.8.5p5  compound statements, selection and iteration statements
              and their substatements are blocks.
  6.7.2.2p3   enumeration constants have type int; an enumerator without = is the
              previous value + 1 (first: 0).
  6.5.3.4     sizeof yields the size in bytes (of int for an enumeration constant),
              an array's size is n * element size; operand not evaluated.
  6.7.2.1p15-17 members in declaration order, union members at offset 0; plus
              psABI 3.1.2: each member at the next multiple of its alignment,
              aggregate alignment = max member alignment, size rounded up to it.
  6.7.5p6     several alignment specifiers: the strictest one applies.
  6.6         every bound/width/alignment/enumerator is an integer constant
              expression evaluated at its declaration point.
  6.3.1.3p2   storing ~0u into an unsigned bit-field of width w yields 2^w - 1.
"""

import argparse
import collections
import concurrent.futures
import hashlib
import os
import random
import shutil
import subprocess
import sys
import time

# ============================================================================
# x86-64 System V psABI (LP64) scalar sizes/alignments (psABI Fig. 3.1)
# ============================================================================
PSABI = {
    'char': (1, 1), 'short': (2, 2), 'int': (4, 4), 'unsigned int': (4, 4),
    'long': (8, 8), 'long long': (8, 8), 'float': (4, 4), 'double': (8, 8),
}
PTR_SIZE_ALIGN = (8, 8)
FUNDAMENTAL_ALIGNS = (1, 2, 4, 8, 16)  # <= _Alignof(max_align_t) == 16
INT_MAX = 2 ** 31 - 1
ELEMS = ['char', 'short', 'int', 'long', 'long long', 'float', 'double']
MEMBER_NAMES = 'abcdefgh'

# ============================================================================
# Abstract syntax
# ============================================================================


class Lit:
    def __init__(self, v):
        self.v = v


class Name:
    def __init__(self, n):
        self.n = n


class SizeofName:
    def __init__(self, n, paren=False):
        self.n, self.paren = n, paren


class TN:
    """Type name: ('prim', p) | ('arr', p, Expr) | ('rec', kind, tag) | ('td', name)."""

    def __init__(self, *t):
        self.t = t


class SizeofType:
    def __init__(self, tn):
        self.tn = tn


class AlignofType:
    def __init__(self, tn):
        self.tn = tn


class OffsetofE:
    def __init__(self, kind, tag, path):
        self.kind, self.tag, self.path = kind, tag, list(path)


class Bin:
    def __init__(self, op, a, b):
        self.op, self.a, self.b = op, a, b


class Enumerator:
    def __init__(self, name, expr=None):
        self.name, self.expr = name, expr


class EnumSpec:
    def __init__(self, tag, items):
        self.tag, self.items = tag, items


class EnumDecl:
    def __init__(self, spec):
        self.spec = spec


class Field:
    """base: ('prim', p) | ('ptr', p) | ('recref', kind, tag) | ('recdef', RecSpec)
    | ('enumptr', EnumSpec)."""

    def __init__(self, base, name, bound=None, alignas=(), width=None):
        self.base, self.name, self.bound = base, name, bound
        self.alignas, self.width = list(alignas), width


class RecSpec:
    def __init__(self, kind, tag, fields):
        self.kind, self.tag, self.fields = kind, tag, fields


class RecDecl:
    def __init__(self, spec):
        self.spec = spec


class ObjDecl:
    """base: ('prim', p) | ('ptr', p); bound: optional array bound expression."""

    def __init__(self, static, base, name, bound=None):
        self.static, self.base, self.name, self.bound = static, base, name, bound


class TypedefDecl:
    def __init__(self, name, prim, bound):
        self.name, self.prim, self.bound = name, prim, bound


class BfObj:
    def __init__(self, var, kind, tag, member):
        self.var, self.kind, self.tag, self.member = var, kind, tag, member


class Print:
    """what: ('name', n) | ('sizeofname', n, paren) | ('sizeoftype', TN)
    | ('alignof', TN) | ('offsetof', kind, tag, path) | ('bound', kind, tag, path)
    | ('bf', var, member)"""

    def __init__(self, label, fmt, what):
        self.label, self.fmt, self.what = label, fmt, what


class Block:
    def __init__(self, kind, items, cond=None, var=None):
        self.kind, self.items, self.cond, self.var = kind, items, cond, var


class Call:
    def __init__(self, fname):
        self.fname = fname


class FuncDef:
    def __init__(self, name, items):
        self.name, self.items = name, items


class TU:
    def __init__(self, family, seed):
        self.family, self.seed = family, seed
        self.items = []
        self.need_ones = False


# ============================================================================
# Renderer (AST -> C17 text)
# ============================================================================
PREC = {'+': 1, '-': 1, '*': 2}


def r_expr(e, ctx=0):
    if isinstance(e, Lit):
        return str(e.v)
    if isinstance(e, Name):
        return e.n
    if isinstance(e, SizeofName):
        return 'sizeof(%s)' % e.n if e.paren else 'sizeof %s' % e.n
    if isinstance(e, SizeofType):
        return 'sizeof(%s)' % r_tn(e.tn)
    if isinstance(e, AlignofType):
        return '_Alignof(%s)' % r_tn(e.tn)
    if isinstance(e, OffsetofE):
        return 'offsetof(%s %s, %s)' % (e.kind, e.tag, '.'.join(e.path))
    if isinstance(e, Bin):
        p = PREC[e.op]
        s = '%s %s %s' % (r_expr(e.a, p), e.op, r_expr(e.b, p + 1))
        return '(%s)' % s if p < ctx else s
    raise TypeError(e)


def r_tn(tn):
    t = tn.t
    if t[0] == 'prim':
        return t[1]
    if t[0] == 'arr':
        return '%s[%s]' % (t[1], r_expr(t[2]))
    if t[0] == 'rec':
        return '%s %s' % (t[1], t[2])
    if t[0] == 'td':
        return t[1]
    raise TypeError(t)


def r_enum_spec(spec):
    parts = [en.name if en.expr is None else '%s = %s' % (en.name, r_expr(en.expr))
             for en in spec.items]
    head = 'enum %s ' % spec.tag if spec.tag else 'enum '
    return head + '{ ' + ', '.join(parts) + ' }'


def r_rec_spec(spec):
    """Lines of `kind tag { ... }` (no trailing ';'); member lines indented 4."""
    lines = ['%s %s {' % (spec.kind, spec.tag)]
    for f in spec.fields:
        lines.extend('    ' + ln for ln in r_field(f))
    lines.append('}')
    return lines


def r_field(f):
    pre = ''.join('_Alignas(%s) ' % r_expr(a) for a in f.alignas)
    decl = f.name
    if f.bound is not None:
        decl += '[%s]' % r_expr(f.bound)
    if f.width is not None:
        decl += ' : %s' % r_expr(f.width)
    b = f.base
    if b[0] == 'prim':
        return ['%s%s %s;' % (pre, b[1], decl)]
    if b[0] == 'ptr':
        return ['%s%s *%s;' % (pre, b[1], decl)]
    if b[0] == 'recref':
        return ['%s%s %s %s;' % (pre, b[1], b[2], decl)]
    if b[0] == 'enumptr':
        return ['%s%s *%s;' % (pre, r_enum_spec(b[1]), decl)]
    if b[0] == 'recdef':
        lines = r_rec_spec(b[1])
        lines[0] = pre + lines[0]
        lines[-1] += ' %s;' % decl
        return lines
    raise TypeError(b)


def r_print(p):
    w = p.what
    k = w[0]
    if k == 'name':
        ex = w[1]
    elif k == 'sizeofname':
        ex = r_expr(SizeofName(w[1], w[2]))
    elif k == 'sizeoftype':
        ex = 'sizeof(%s)' % r_tn(w[1])
    elif k == 'alignof':
        ex = '_Alignof(%s)' % r_tn(w[1])
    elif k == 'offsetof':
        ex = 'offsetof(%s %s, %s)' % (w[1], w[2], '.'.join(w[3]))
    elif k == 'bound':
        acc = '((%s %s *)0)->%s' % (w[1], w[2], '.'.join(w[3]))
        ex = 'sizeof(%s) / sizeof(%s[0])' % (acc, acc)
    elif k == 'bf':
        ex = '(unsigned int)%s.%s' % (w[1], w[2])
    else:
        raise TypeError(w)
    return 'printf("%%s=%%%s\\n", "%s", %s);' % (p.fmt, p.label, ex)


def r_items(items, ind, out):
    for it in items:
        r_item(it, ind, out)


def r_item(it, ind, out):
    sp = '    ' * ind
    if isinstance(it, EnumDecl):
        out.append(sp + r_enum_spec(it.spec) + ';')
    elif isinstance(it, RecDecl):
        lines = r_rec_spec(it.spec)
        lines[-1] += ';'
        out.extend(sp + ln for ln in lines)
    elif isinstance(it, ObjDecl):
        st = 'static ' if it.static else ''
        b = it.base
        decl = it.name
        if it.bound is not None:
            decl += '[%s]' % r_expr(it.bound)
        init = ' = {0}' if it.bound is not None else ' = 0'
        ty = b[1] + (' *' if b[0] == 'ptr' else ' ')
        out.append('%s%s%s%s%s;' % (sp, st, ty, decl, init))
    elif isinstance(it, TypedefDecl):
        out.append('%stypedef %s %s[%s];' % (sp, it.prim, it.name, r_expr(it.bound)))
    elif isinstance(it, BfObj):
        out.append('%s%s %s %s = {0};' % (sp, it.kind, it.tag, it.var))
        out.append('%s%s.%s = ones_();' % (sp, it.var, it.member))
    elif isinstance(it, Print):
        out.append(sp + r_print(it))
    elif isinstance(it, Call):
        out.append('%s%s();' % (sp, it.fname))
    elif isinstance(it, Block):
        r_block(it, ind, out)
    else:
        raise TypeError(it)


def r_block(b, ind, out):
    sp = '    ' * ind
    k = b.kind
    if k == 'plain':
        out.append(sp + '{')
        r_items(b.items, ind + 1, out)
        out.append(sp + '}')
    elif k == 'if1':
        out.append(sp + 'if (1) {')
        r_items(b.items, ind + 1, out)
        out.append(sp + '}')
    elif k == 'do0':
        out.append(sp + 'do {')
        r_items(b.items, ind + 1, out)
        out.append(sp + '} while (0);')
    elif k == 'for1':
        out.append('%sfor (int %s = 0; %s < 1; ++%s) {' % (sp, b.var, b.var, b.var))
        r_items(b.items, ind + 1, out)
        out.append(sp + '}')
    elif k == 'switch1':
        out.append(sp + 'switch (1) {')
        out.append(sp + 'case 1: {')
        r_items(b.items, ind + 1, out)
        out.append(sp + '}')
        out.append(sp + '    break;')
        out.append(sp + 'default:')
        out.append(sp + '    break;')
        out.append(sp + '}')
    elif k == 'ifdecl':
        out.append('%sif (sizeof(%s)) {' % (sp, r_enum_spec(b.cond)))
        r_items(b.items, ind + 1, out)
        out.append(sp + '}')
    elif k == 'ifdecl1':
        assert len(b.items) == 1 and isinstance(b.items[0], Print)
        out.append('%sif (sizeof(%s))' % (sp, r_enum_spec(b.cond)))
        r_items(b.items, ind + 1, out)
    elif k == 'switchdecl':
        out.append('%sswitch (sizeof(%s)) {' % (sp, r_enum_spec(b.cond)))
        out.append(sp + 'default: {')
        r_items(b.items, ind + 1, out)
        out.append(sp + '}')
        out.append(sp + '}')
    else:
        raise TypeError(k)


def render(tu):
    out = ['/* generated by scope_oracle.py: family=%s seed=%d */' % (tu.family, tu.seed),
           '#include <stddef.h>',
           '#include <stdio.h>',
           '']
    if tu.need_ones:
        out += ['static unsigned int ones_(void) { return ~0u; }', '']
    for it in tu.items:
        if isinstance(it, FuncDef):
            if it.name == 'main':
                out.append('int main(void)')
            else:
                out.append('static void %s(void)' % it.name)
            out.append('{')
            r_items(it.items, 1, out)
            if it.name == 'main':
                out.append('    return 0;')
            out.append('}')
            out.append('')
        else:
            r_item(it, 0, out)
    return '\n'.join(out).rstrip('\n') + '\n'


# ============================================================================
# Model: C17 scope rules + psABI layout, evaluated on the abstract tree
# ============================================================================
class ModelError(Exception):
    pass


class TPrim:
    cat = 'prim'

    def __init__(self, name):
        self.name = name
        self.size, self.align = PSABI[name]


class TPtr:
    cat = 'ptr'
    size, align = PTR_SIZE_ALIGN


class TArr:
    cat = 'arr'

    def __init__(self, elem, n):
        if elem.size is None:
            raise ModelError('array of incomplete/unsized element')
        self.elem, self.n = elem, n
        self.size, self.align = elem.size * n, elem.align


class TRec:
    cat = 'rec'

    def __init__(self, kind, tag, members, size, align, has_bf):
        self.kind, self.tag, self.members = kind, tag, members  # name -> (type, off, width)
        self.size, self.align, self.has_bf = size, align, has_bf


class Env:
    __slots__ = ('parent', 'ords', 'tags')

    def __init__(self, parent):
        self.parent, self.ords, self.tags = parent, {}, {}


def roundup(x, a):
    return (x + a - 1) // a * a


MUTANTS = ('flat', 'filescope-types', 'lazy-layout', 'member-scope',
           'selfref-zero', 'enum-end')
MUTANT_DOC = {
    'flat': 'block scopes never end (textually last declaration wins)',
    'filescope-types': 'member bounds/widths/_Alignas resolve names at file scope',
    'lazy-layout': 'struct layout re-evaluated with the names visible at each use',
    'member-scope': 'a struct member list is a scope (C++-like)',
    'selfref-zero': 'enumerator is in scope inside its own initializer (value 0)',
    'enum-end': 'enumerators come into scope only after the whole enumerator list',
}


class Model:
    def __init__(self, mutant=None):
        assert mutant is None or mutant in MUTANTS
        self.mut = mutant

    # ---------------------------------------------------------------- scopes
    def new_env(self, parent):
        if self.mut == 'flat' and parent is not None:
            return parent
        return Env(parent)

    def bind(self, env, name, b):
        if name in env.ords and not self.mut:
            raise ModelError('%s declared twice in one scope (6.7p3)' % name)
        env.ords[name] = b

    def bind_tag(self, env, tag, b):
        if tag in env.tags and not self.mut:
            raise ModelError('tag %s declared twice in one scope' % tag)
        env.tags[tag] = b

    def lookup(self, env, name):
        e = env
        while e is not None:
            if name in e.ords:
                return e.ords[name]
            e = e.parent
        raise ModelError('%s not visible' % name)

    def lookup_tag(self, env, tag):
        e = env
        while e is not None:
            if tag in e.tags:
                return e.tags[tag]
            e = e.parent
        raise ModelError('tag %s not visible' % tag)

    # ------------------------------------------------------------ constants
    def ice(self, e, env):
        """Integer constant expression value (6.6)."""
        if isinstance(e, Lit):
            return e.v
        if isinstance(e, Name):
            b = self.lookup(env, e.n)
            if b[0] != 'enum':
                raise ModelError('%s is not an enumeration constant' % e.n)
            return b[1]
        if isinstance(e, SizeofName):
            b = self.lookup(env, e.n)
            if b[0] == 'enum':
                return PSABI['int'][0]          # 6.7.2.2p3: type int
            if b[0] == 'obj':
                if b[1].size is None:
                    raise ModelError('sizeof of object without known size')
                return b[1].size
            raise ModelError('sizeof %s: not an expression' % e.n)
        if isinstance(e, SizeofType):
            t = self.type_of(e.tn, env)
            if t.size is None:
                raise ModelError('sizeof unknown')
            return t.size
        if isinstance(e, AlignofType):
            t = self.type_of(e.tn, env)
            if t.align is None:
                raise ModelError('alignof unknown')
            return t.align
        if isinstance(e, OffsetofE):
            return self.offset(e.kind, e.tag, e.path, env)
        if isinstance(e, Bin):
            a, b = self.ice(e.a, env), self.ice(e.b, env)
            if e.op == '+':
                return a + b
            if e.op == '-':
                return a - b
            if e.op == '*':
                return a * b
        raise ModelError('bad constant expression %r' % (e,))

    # ------------------------------------------------------------- types
    def type_of(self, tn, env):
        t = tn.t
        if t[0] == 'prim':
            return TPrim(t[1])
        if t[0] == 'arr':
            n = self.ice(t[2], env)
            if n < 1:
                raise ModelError('array bound %d <= 0 (6.7.6.2p1)' % n)
            return TArr(TPrim(t[1]), n)
        if t[0] == 'rec':
            return self.rec_type(t[1], t[2], env)
        if t[0] == 'td':
            b = self.lookup(env, t[1])
            if b[0] != 'typedef':
                raise ModelError('%s is not a typedef name' % t[1])
            return b[1]
        raise ModelError('bad type name')

    def rec_type(self, kind, tag, env):
        b = self.lookup_tag(env, tag)
        if b[0] != 'rec' or b[1].kind != kind:
            raise ModelError('%s %s: tag kind mismatch' % (kind, tag))
        if self.mut == 'lazy-layout':
            return self.layout(b[1], Env(env))
        if b[2] is None:
            raise ModelError('%s %s incomplete' % (kind, tag))
        return b[2]

    def decl_rec(self, spec, env):
        entry = ['rec', spec, None]
        self.bind_tag(env, spec.tag, entry)       # 6.2.1p7: tag in scope from here
        fenv = Env(env) if self.mut == 'member-scope' else env
        t = self.layout(spec, fenv)
        entry[2] = t
        return t

    def layout(self, spec, env):
        cenv = self.file if self.mut == 'filescope-types' else env
        members = []
        for f in spec.fields:
            b = f.base
            if b[0] == 'prim':
                t = TPrim(b[1])
            elif b[0] == 'ptr':
                t = TPtr()
            elif b[0] == 'recref':
                t = self.rec_type(b[1], b[2], env)
                if t.has_bf:
                    raise ModelError('bit-field record used as member type')
            elif b[0] == 'recdef':
                # 6.2.1p4: the nested tag belongs to the enclosing block/file scope
                t = self.decl_rec(b[1], env)
                if t.has_bf:
                    raise ModelError('bit-field record used as member type')
            elif b[0] == 'enumptr':
                # enumeration constants declared in a member: enclosing scope
                self.decl_enum(b[1], env)
                t = TPtr()
            else:
                raise ModelError('bad member base')
            if f.bound is not None:
                n = self.ice(f.bound, cenv)
                if n < 1:
                    raise ModelError('member array bound %d <= 0' % n)
                t = TArr(t, n)
            extra = 0
            for a in f.alignas:
                v = self.ice(a, cenv)
                if v not in FUNDAMENTAL_ALIGNS:
                    raise ModelError('_Alignas(%d) not a fundamental alignment' % v)
                extra = max(extra, v)              # 6.7.5p6: strictest wins
            if f.alignas and extra < t.align:
                raise ModelError('_Alignas weaker than natural alignment (6.7.5p5)')
            w = None
            if f.width is not None:
                if b != ('prim', 'unsigned int') or f.bound is not None or f.alignas:
                    raise ModelError('only unsigned int bit-fields are generated')
                w = self.ice(f.width, cenv)
                if not 1 <= w <= 32:
                    raise ModelError('bit-field width %d out of 1..32' % w)
            if any(m[0] == f.name for m in members):
                raise ModelError('duplicate member %s' % f.name)
            members.append((f.name, t, max(t.align, extra), w))
        if not members:
            raise ModelError('record without named members')
        if any(m[3] is not None for m in members):
            mem = {n: (t, None, w) for n, t, a, w in members}
            return TRec(spec.kind, spec.tag, mem, None, None, True)
        mem = {}
        off = size = 0
        al = 1
        for n, t, a, w in members:
            if spec.kind == 'struct':
                off = roundup(off, a)
                mem[n] = (t, off, None)
                off += t.size
            else:
                mem[n] = (t, 0, None)
                size = max(size, t.size)
            al = max(al, a)
        total = roundup(off if spec.kind == 'struct' else size, al)
        return TRec(spec.kind, spec.tag, mem, total, al, False)

    def member_at(self, kind, tag, path, env):
        t = self.rec_type(kind, tag, env)
        off = 0
        for m in path:
            if t.cat != 'rec' or m not in t.members:
                raise ModelError('no member %s' % m)
            mt, mo, mw = t.members[m]
            if mo is None:
                off = None
            elif off is not None:
                off += mo
            t = mt
        return t, off

    def offset(self, kind, tag, path, env):
        t, off = self.member_at(kind, tag, path, env)
        if off is None:
            raise ModelError('offsetof into a bit-field record')
        return off

    # ------------------------------------------------------------- decls
    def decl_enum(self, spec, env):
        if spec.tag:
            self.bind_tag(env, spec.tag, ('enum',))
        prev = None
        pending = []
        for en in spec.items:
            if en.expr is None:
                v = 0 if prev is None else prev + 1
            else:
                tmp = False
                if self.mut == 'selfref-zero' and en.name not in env.ords:
                    env.ords[en.name] = ('enum', 0)
                    tmp = True
                try:
                    v = self.ice(en.expr, env)
                finally:
                    if tmp:
                        del env.ords[en.name]
            if not -INT_MAX - 1 <= v <= INT_MAX:
                raise ModelError('enumerator out of int range')
            if self.mut == 'enum-end':
                pending.append((en.name, v))
            else:
                self.bind(env, en.name, ('enum', v))   # 6.2.1p7
            prev = v
        for n, v in pending:
            self.bind(env, n, ('enum', v))

    def exec_items(self, items, env, out):
        for it in items:
            self.exec_item(it, env, out)

    def exec_item(self, it, env, out):
        if isinstance(it, EnumDecl):
            self.decl_enum(it.spec, env)
        elif isinstance(it, RecDecl):
            self.decl_rec(it.spec, env)
        elif isinstance(it, ObjDecl):
            b = it.base
            t = TPrim(b[1]) if b[0] == 'prim' else TPtr()
            if it.bound is not None:
                n = self.ice(it.bound, env)       # declarator not complete yet
                if n < 1:
                    raise ModelError('object array bound <= 0')
                t = TArr(t, n)
            self.bind(env, it.name, ('obj', t))    # scope starts after declarator
        elif isinstance(it, TypedefDecl):
            n = self.ice(it.bound, env)
            if n < 1:
                raise ModelError('typedef array bound <= 0')
            self.bind(env, it.name, ('typedef', TArr(TPrim(it.prim), n)))
        elif isinstance(it, BfObj):
            self.bind(env, it.var, ('obj', self.rec_type(it.kind, it.tag, env)))
        elif isinstance(it, Print):
            out.append('%s=%s' % (it.label, self.print_value(it, env)))
        elif isinstance(it, Call):
            if it.fname not in self.fn_lines:
                raise ModelError('call of undefined function')
            out.extend(self.fn_lines[it.fname])
        elif isinstance(it, Block):
            senv = self.new_env(env)               # the statement itself (6.8.4p3)
            if it.cond is not None:
                self.decl_enum(it.cond, senv)
            if it.var is not None:
                self.bind(senv, it.var, ('obj', TPrim('int')))
            body = self.new_env(senv)              # its (compound) substatement
            self.exec_items(it.items, body, out)
        else:
            raise ModelError('bad item')

    def print_value(self, p, env):
        w = p.what
        k = w[0]
        if k == 'name':
            if p.fmt != 'd':
                raise ModelError('format mismatch')
            return self.ice(Name(w[1]), env)
        if p.fmt == 'd':
            raise ModelError('format mismatch')
        if k == 'sizeofname':
            return self.ice(SizeofName(w[1], w[2]), env)
        if k == 'sizeoftype':
            return self.ice(SizeofType(w[1]), env)
        if k == 'alignof':
            return self.ice(AlignofType(w[1]), env)
        if k == 'offsetof':
            return self.offset(w[1], w[2], w[3], env)
        if k == 'bound':
            t, _ = self.member_at(w[1], w[2], w[3], env)
            if t.cat != 'arr':
                raise ModelError('bound of non-array member')
            return t.n
        if k == 'bf':
            b = self.lookup(env, w[1])
            if b[0] != 'obj' or b[1].cat != 'rec':
                raise ModelError('bad bit-field object')
            mt, mo, mw = b[1].members[w[2]]
            if mw is None:
                raise ModelError('not a bit-field')
            return (1 << mw) - 1
        raise ModelError('bad print')

    def run(self, tu):
        self.file = Env(None)
        self.fn_lines = {}
        if tu.need_ones:
            self.bind(self.file, 'ones_', ('func',))
        for it in tu.items:
            if isinstance(it, FuncDef):
                self.bind(self.file, it.name, ('func',))
                out = []
                # the body is walked at its definition point, so it sees exactly
                # the file-scope declarations that precede it
                self.exec_items(it.items, self.new_env(self.file), out)
                self.fn_lines[it.name] = out
            else:
                self.exec_item(it, self.file, None)
        if 'main' not in self.fn_lines:
            raise ModelError('no main')
        return self.fn_lines['main']


# ============================================================================
# Generator (structural bookkeeping only: which names/tags exist in which
# open scope, plus conservative value ranges so every construct is valid)
# ============================================================================
class GScope:
    def __init__(self, parent):
        self.parent = parent
        self.names = {}
        self.tags = {}


class RecInfo:
    def __init__(self, kind, tag):
        self.kind, self.tag = kind, tag
        self.bf = False
        self.arr_paths = []
        self.off_paths = []
        self.bf_members = []


NAME_POOL = ['N', 'K', 'W', 'LEN', 'CNT', 'SZ', 'DIM', 'NUM']
OBJ_POOL = ['obj', 'buf', 'cell', 'blob']


class Gen:
    def __init__(self, family, seed):
        self.family, self.seed = family, seed
        self.rng = random.Random('scope-oracle/%s/%d' % (family, seed))
        self.cnt = collections.Counter()
        self.tu = TU(family, seed)
        self.scope = GScope(None)
        self.items = self.tu.items
        self.stack = []
        self.fn = 'file'
        self.names = []
        self.mode = {}
        self.lo, self.hi = {}, {}
        self.used = collections.defaultdict(set)
        self.extras = []
        self.snaps = []
        self.cond_fn = None
        self.nprints = 0

    # ------------------------------------------------------------ basics
    def fresh(self, prefix):
        n = self.cnt[prefix]
        self.cnt[prefix] += 1
        return '%s%d' % (prefix, n)

    def push(self, items):
        self.stack.append((self.scope, self.items))
        self.scope = GScope(self.scope)
        self.items = items

    def pop(self):
        self.scope, self.items = self.stack.pop()

    def visible(self, name):
        s = self.scope
        while s is not None:
            if name in s.names:
                return True
            s = s.parent
        return False

    def here(self, name):
        return name in self.scope.names

    def declare_name(self, name, cat):
        assert name not in self.scope.names, name
        self.scope.names[name] = cat

    def vis_recs(self):
        seen, out = set(), []
        s = self.scope
        while s is not None:
            for tag in reversed(list(s.tags)):
                if tag not in seen:
                    seen.add(tag)
                    if isinstance(s.tags[tag], RecInfo):
                        out.append(s.tags[tag])
            s = s.parent
        out.reverse()
        return out

    def note(self, name, lo, hi):
        self.lo[name] = min(self.lo.get(name, lo), lo)
        self.hi[name] = max(self.hi.get(name, hi), hi)

    def pick_value(self, name, pool):
        cand = [v for v in pool if v not in self.used[name]]
        v = self.rng.choice(cand or list(pool))
        self.used[name].add(v)
        return v

    def begin_func(self, name):
        f = FuncDef(name, [])
        self.tu.items.append(f)
        self.declare_name(name, 'func')
        self.push(f.items)
        self.fn = name
        return f

    def end_func(self):
        self.pop()
        self.fn = 'file'

    def pr(self, label, fmt, what):
        self.items.append(Print(label, fmt, what))
        self.nprints += 1

    # ------------------------------------------------------------ enums
    def register_enum(self, spec):
        if spec.tag:
            self.scope.tags[spec.tag] = 'enum'
        for en in spec.items:
            self.declare_name(en.name, 'enum')
            if en.name not in self.names:
                self.extras.append(en.name)

    def enum_literal_spec(self, name, v):
        r = self.rng.random()
        if r < 0.5:
            items = [Enumerator(name, Lit(v))]
        elif r < 0.65:
            items = [Enumerator(self.fresh('P'), Lit(v - 1)), Enumerator(name)]
        elif r < 0.75 and v >= 2:
            items = [Enumerator(self.fresh('P'), Lit(v - 2)), Enumerator(self.fresh('P')),
                     Enumerator(name)]
        elif r < 0.9:
            items = [Enumerator(name, Lit(v)), Enumerator(self.fresh('Q'))]
        else:
            items = [Enumerator(self.fresh('R'), Lit(self.rng.randint(20, 40))),
                     Enumerator(name, Lit(v))]
        tag = self.fresh('en') if self.rng.random() < 0.2 else None
        return EnumSpec(tag, items)

    def emit_enum(self, spec):
        self.items.append(EnumDecl(spec))
        self.register_enum(spec)

    def declare_literal(self, name, pool):
        v = self.pick_value(name, pool)
        self.emit_enum(self.enum_literal_spec(name, v))
        self.note(name, v, v)

    def selfref_spec(self, name):
        """Enumerator list redefining `name` from the visible (outer) `name`."""
        lo, hi = self.lo[name], self.hi[name]
        rng = self.rng
        k = rng.randint(1, 4)
        forms = ['plus', 'tn', 'pre', 'post', 'impl']
        if hi * 2 <= 48:
            forms += ['mul', 'mul']
        form = rng.choice(forms)
        N = Name(name)
        if form == 'plus':
            items = [Enumerator(name, Bin('+', N, Lit(k)))]
            f = lambda x: x + k
        elif form == 'tn':
            items = [Enumerator(name, Bin('+', SizeofType(TN('arr', 'char', N)), Lit(k)))]
            f = lambda x: x + k
        elif form == 'pre':
            x = self.fresh('X')
            items = [Enumerator(x, N), Enumerator(name, Bin('+', Name(x), Lit(k)))]
            f = lambda v: v + k
        elif form == 'post':
            m = self.fresh('M')
            items = [Enumerator(name, Bin('+', N, Lit(k))),
                     Enumerator(m, Bin('+', N, Lit(rng.randint(1, 3))))]
            f = lambda v: v + k
        elif form == 'impl':
            x = self.fresh('X')
            e = N if k == 1 else Bin('+', N, Lit(k - 1))
            items = [Enumerator(x, e), Enumerator(name)]
            f = lambda v: v + k
        else:
            e = Bin('*', N, Lit(2)) if rng.random() < 0.5 else Bin('*', Lit(2), N)
            items = [Enumerator(name, e)]
            f = lambda v: 2 * v
        tag = self.fresh('en') if rng.random() < 0.15 else None
        return EnumSpec(tag, items), f(lo), f(hi)

    # --------------------------------------------------------- expressions
    def bound_expr(self, name, allow_minus=True):
        lo, hi = self.lo[name], self.hi[name]
        rng = self.rng
        if self.mode[name] == 'enum':
            opts = ['id'] * 5 + ['plus'] * 2 + ['tn']
            if hi * 2 <= 64:
                opts.append('mul')
            if allow_minus and lo >= 3:
                opts.append('minus')
            o = rng.choice(opts)
            N = Name(name)
            if o == 'id':
                return N
            if o == 'plus':
                return Bin('+', N, Lit(rng.randint(1, 4)))
            if o == 'mul':
                return Bin('*', N, Lit(2))
            if o == 'minus':
                return Bin('-', N, Lit(rng.randint(1, lo - 1)))
            return SizeofType(TN('arr', 'char', N))
        # object / mixed names: sizeof NAME
        opts = ['id'] * 4 + ['plus'] * 2
        if hi * 2 <= 64:
            opts.append('mul')
        o = rng.choice(opts)
        S = SizeofName(name, rng.random() < 0.4)
        if o == 'id':
            return S
        if o == 'plus':
            return Bin('+', S, Lit(rng.randint(1, 4)))
        return Bin('*', S, Lit(2))

    def width_expr(self, name):
        lo, hi = self.lo[name], self.hi[name]
        rng = self.rng
        N = Name(name)
        opts = ['id'] * 3 + ['tn']
        if hi + 1 <= 32:
            opts += ['plus'] * 2
        if hi * 2 <= 32:
            opts.append('mul')
        if hi <= 31:
            opts.append('rsub')
        o = rng.choice(opts)
        if o == 'id':
            return N
        if o == 'tn':
            return SizeofType(TN('arr', 'char', N))
        if o == 'plus':
            return Bin('+', N, Lit(rng.randint(1, min(8, 32 - hi))))
        if o == 'mul':
            return Bin('*', N, Lit(2))
        return Bin('-', Lit(32), N)

    def align_expr(self, name):
        hi = self.hi[name]
        rng = self.rng
        N = Name(name)
        r = rng.random()
        if r < 0.15:
            return SizeofType(TN('arr', 'char', N))
        if r < 0.3 and hi <= 8:
            return Bin('*', N, Lit(2))
        return N

    def vis_names(self, modes=('enum',)):
        return [n for n in self.names if self.visible(n) and self.mode[n] in modes]

    # ------------------------------------------------------------ records
    def make_record(self, kind, prefix, fieldfn, tag=None):
        tag = tag or self.fresh(prefix)
        ri = RecInfo(kind, tag)
        fields = fieldfn(self, ri)
        if fields is None:
            return False
        self.items.append(RecDecl(RecSpec(kind, tag, fields)))
        self.scope.tags[tag] = ri
        return True

    # --------------------------------------------------------- observations
    def observe(self, max_recs=3):
        rng = self.rng
        pre = '%s.%s' % (self.fn, self.fresh('s'))
        start = self.nprints
        for name in self.names:
            if not self.visible(name):
                continue
            if self.mode[name] == 'enum':
                self.pr('%s.val.%s' % (pre, name), 'd', ('name', name))
                if rng.random() < 0.3:
                    self.pr('%s.tn.%s' % (pre, name), 'zu',
                            ('sizeoftype', TN('arr', 'char', Name(name))))
                if rng.random() < 0.15:
                    td = self.fresh('td')
                    self.items.append(TypedefDecl(td, 'char', Name(name)))
                    self.declare_name(td, 'typedef')
                    self.pr('%s.td.%s' % (pre, name), 'zu', ('sizeoftype', TN('td', td)))
            else:
                self.pr('%s.sizeof.%s' % (pre, name), 'zu',
                        ('sizeofname', name, rng.random() < 0.5))
        for x in self.extras:
            if self.visible(x) and rng.random() < 0.3:
                self.pr('%s.val.%s' % (pre, x), 'd', ('name', x))
        for s in self.snaps:
            if self.visible(s) and rng.random() < 0.45:
                self.pr('%s.snap.%s' % (pre, s), 'd', ('name', s))
        recs = self.vis_recs()
        if recs:
            chosen = [recs[-1]] + rng.sample(recs[:-1], min(len(recs) - 1, max_recs - 1))
            for ri in chosen:
                self.observe_rec(ri, pre)
        return self.nprints > start

    def observe_rec(self, ri, pre):
        rng = self.rng
        k, t = ri.kind, ri.tag
        tn = TN('rec', k, t)
        n0 = self.nprints
        if not ri.bf:
            if rng.random() < 0.8:
                self.pr('%s.sz.%s' % (pre, t), 'zu', ('sizeoftype', tn))
            if rng.random() < 0.45:
                self.pr('%s.al.%s' % (pre, t), 'zu', ('alignof', tn))
            if ri.off_paths and rng.random() < 0.45:
                p = rng.choice(ri.off_paths)
                self.pr('%s.off.%s.%s' % (pre, t, '.'.join(p)), 'zu', ('offsetof', k, t, p))
            if rng.random() < 0.3:
                e = self.fresh('esz')
                self.items.append(EnumDecl(EnumSpec(None, [Enumerator(e, SizeofType(tn))])))
                self.declare_name(e, 'enum')
                self.pr('%s.esz.%s' % (pre, t), 'd', ('name', e))
        if ri.arr_paths and rng.random() < 0.65:
            p = rng.choice(ri.arr_paths)
            self.pr('%s.bound.%s.%s' % (pre, t, '.'.join(p)), 'zu', ('bound', k, t, p))
        if ri.bf:
            for m in rng.sample(ri.bf_members, min(2, len(ri.bf_members))):
                v = self.fresh('bv')
                self.items.append(BfObj(v, k, t, m))
                self.declare_name(v, 'obj')
                self.tu.need_ones = True
                self.pr('%s.bf.%s.%s' % (pre, t, m), 'u', ('bf', v, m))
        if self.nprints == n0:
            if not ri.bf:
                self.pr('%s.sz.%s' % (pre, t), 'zu', ('sizeoftype', tn))
            elif ri.arr_paths:
                p = ri.arr_paths[0]
                self.pr('%s.bound.%s.%s' % (pre, t, '.'.join(p)), 'zu', ('bound', k, t, p))

    def snapshot(self):
        choices = []
        for ri in self.vis_recs():
            if ri.bf:
                continue
            tn = TN('rec', ri.kind, ri.tag)
            choices += [SizeofType(tn), SizeofType(tn), AlignofType(tn)]
            if ri.off_paths:
                choices.append(OffsetofE(ri.kind, ri.tag, self.rng.choice(ri.off_paths)))
        for n in self.names:
            if self.visible(n):
                choices.append(Name(n) if self.mode[n] == 'enum' else SizeofName(n))
        if not choices:
            return
        nm = self.fresh('snap')
        self.items.append(EnumDecl(EnumSpec(None, [Enumerator(nm, self.rng.choice(choices))])))
        self.declare_name(nm, 'enum')
        self.snaps.append(nm)

    # ------------------------------------------------------------ blocks
    def open_block(self, kind, cond=None):
        blk = Block(kind, [])
        if kind == 'for1':
            blk.var = self.fresh('it')
        self.items.append(blk)
        self.push([])                    # statement scope (6.8.4p3 / 6.8.5p5)
        if blk.var:
            self.declare_name(blk.var, 'obj')
        if cond is not None:
            blk.cond = cond
            self.register_enum(cond)
        self.push(blk.items)             # substatement scope
        return blk

    def close_block(self):
        self.pop()
        self.pop()


# ============================================================================
# Field makers
# ============================================================================
def fields_plain(g, ri, modes=('enum',)):
    vis = g.vis_names(modes)
    if not vis:
        return None
    rng = g.rng
    k = rng.randint(1, 4)
    anchor = rng.randrange(k)
    fields = []
    for i in range(k):
        m = MEMBER_NAMES[i]
        r = rng.random()
        if i == anchor or r < 0.25:
            f = Field(('prim', rng.choice(ELEMS)), m, bound=g.bound_expr(rng.choice(vis)))
            ri.arr_paths.append([m])
        elif r < 0.6:
            f = Field(('prim', rng.choice(ELEMS)), m)
        elif r < 0.8:
            f = Field(('prim', rng.choice(ELEMS)), m, bound=Lit(rng.randint(1, 5)))
            ri.arr_paths.append([m])
        else:
            f = Field(('ptr', 'char'), m)
        ri.off_paths.append([m])
        fields.append(f)
    return fields


def fields_obj(g, ri):
    return fields_plain(g, ri, modes=('obj', 'mixed'))


def fields_bf(g, ri):
    vis = g.vis_names()
    if not vis:
        return None
    rng = g.rng
    fields = []
    if rng.random() < 0.4:
        fields.append(Field(('prim', 'char'), 'a', bound=g.bound_expr(rng.choice(vis))))
        ri.arr_paths.append(['a'])
    for j in range(rng.randint(1, 3)):
        m = 'b%d' % j
        fields.append(Field(('prim', 'unsigned int'), m, width=g.width_expr(rng.choice(vis))))
        ri.bf_members.append(m)
    if rng.random() < 0.3:
        fields.append(Field(('prim', 'char'), 'z'))
    ri.bf = True
    return fields


def fields_align(g, ri):
    vis = g.vis_names()
    if not vis:
        return None
    rng = g.rng
    k = rng.randint(2, 4)
    anchor = rng.randrange(k)
    fields = []
    for i in range(k):
        m = MEMBER_NAMES[i]
        r = rng.random()
        if i == anchor or r < 0.3:
            nm = rng.choice(vis)
            lo = g.lo[nm]
            base = rng.choice([p for p in ELEMS if PSABI[p][1] <= lo])
            al = [g.align_expr(nm)]
            if rng.random() < 0.25:
                other = [n for n in vis if PSABI[base][1] <= g.lo[n]]
                if len(other) > 1 and rng.random() < 0.5:
                    al.append(Name(rng.choice([n for n in other if n != nm])))
                else:
                    al.append(Lit(rng.choice([a for a in FUNDAMENTAL_ALIGNS
                                              if a >= PSABI[base][1]])))
                rng.shuffle(al)
            r2 = rng.random()
            if r2 < 0.25:
                bound = Lit(rng.randint(1, 3))
                ri.arr_paths.append([m])
            elif r2 < 0.4:
                bound = g.bound_expr(rng.choice(vis))
                ri.arr_paths.append([m])
            else:
                bound = None
            f = Field(('prim', base), m, bound=bound, alignas=al)
        elif r < 0.7:
            f = Field(('prim', rng.choice(ELEMS)), m)
        else:
            f = Field(('prim', rng.choice(['char', 'short'])), m,
                      bound=g.bound_expr(rng.choice(vis)))
            ri.arr_paths.append([m])
        ri.off_paths.append([m])
        fields.append(f)
    return fields


def fields_selfref(g, ri):
    """D1-style fields, sometimes with `enum { N = N + k } *p;` in the member list."""
    vis = g.vis_names()
    if not vis:
        return None
    rng = g.rng
    cand = [n for n in vis if not g.here(n)]
    if not cand or rng.random() >= 0.3:
        return fields_plain(g, ri)
    nm = rng.choice(cand)
    k = rng.randint(2, 4)
    pos = rng.randrange(k)
    fields = []
    for i in range(k):
        m = MEMBER_NAMES[i]
        if i == pos:
            spec, lo, hi = g.selfref_spec(nm)
            g.register_enum(spec)
            g.note(nm, lo, hi)
            fields.append(Field(('enumptr', spec), m))
        else:
            n2 = rng.choice(g.vis_names())
            fields.append(Field(('prim', rng.choice(ELEMS)), m, bound=g.bound_expr(n2)))
            ri.arr_paths.append([m])
        ri.off_paths.append([m])
    return fields


def fields_h6(g, ri, top=True):
    vis = g.vis_names()
    if not vis:
        return None
    rng = g.rng
    k = rng.randint(2, 4) if top else rng.randint(1, 3)
    anchor = rng.randrange(k)
    used_enum = False
    fields = []
    for i in range(k):
        m = MEMBER_NAMES[i]
        r = rng.random()
        free = [n for n in g.names if not g.here(n)]
        recs = [x for x in g.vis_recs() if not x.bf and x.tag != ri.tag]
        if top and not used_enum and free and r < 0.2:
            nm = rng.choice(free)
            v = g.pick_value(nm, range(1, 17))
            spec = EnumSpec(None, [Enumerator(nm, Lit(v))]) if rng.random() < 0.6 \
                else g.enum_literal_spec(nm, v)
            g.register_enum(spec)
            g.note(nm, v, v)
            used_enum = True
            fields.append(Field(('enumptr', spec), 'p' + m))
            ri.off_paths.append(['p' + m])
            continue
        if top and r < 0.45:
            nk = rng.choice(['struct', 'union'])
            ntag = g.fresh('I')
            nri = RecInfo(nk, ntag)
            nf = fields_h6(g, nri, top=False)
            g.scope.tags[ntag] = nri      # nested tag: enclosing scope (6.2.1p4)
            fields.append(Field(('recdef', RecSpec(nk, ntag, nf)), m))
            ri.arr_paths += [[m] + p for p in nri.arr_paths]
            ri.off_paths += [[m]] + [[m] + p for p in nri.off_paths]
            continue
        if top and recs and r < 0.6:
            rr = rng.choice(recs)
            if rng.random() < 0.3:
                fields.append(Field(('recref', rr.kind, rr.tag), m,
                                    bound=g.bound_expr(rng.choice(g.vis_names()))))
                ri.arr_paths.append([m])
            else:
                fields.append(Field(('recref', rr.kind, rr.tag), m))
                ri.arr_paths += [[m] + p for p in rr.arr_paths]
                ri.off_paths += [[m] + p for p in rr.off_paths]
            ri.off_paths.append([m])
            continue
        if i == anchor or r < 0.8:
            fields.append(Field(('prim', rng.choice(ELEMS)), m,
                                bound=g.bound_expr(rng.choice(g.vis_names()))))
            ri.arr_paths.append([m])
        else:
            fields.append(Field(('prim', rng.choice(ELEMS)), m))
        ri.off_paths.append([m])
    if not ri.arr_paths:
        fields.append(Field(('prim', 'char'), 'z', bound=g.bound_expr(rng.choice(g.vis_names()))))
        ri.arr_paths.append(['z'])
        ri.off_paths.append(['z'])
    return fields


# ============================================================================
# Skeleton shared by D1, H1, H2, H3, H5, H6
# ============================================================================
class Cfg:
    def __init__(self, declare, record, block_kinds, max_depth=2, p_file=0.85,
                 p_decl=0.8, p_helper=0.7, cond=None):
        self.declare, self.record = declare, record
        self.block_kinds, self.max_depth = block_kinds, max_depth
        self.p_file, self.p_decl, self.p_helper = p_file, p_decl, p_helper
        self.cond = cond


BASIC_KINDS = ['plain'] * 5 + ['if1', 'do0', 'for1', 'switch1']


def build_scope(g, cfg, depth):
    rng = g.rng
    acts = []
    for name in g.names:
        if not g.here(name) and (not g.visible(name) or rng.random() < cfg.p_decl):
            acts.append(('decl', name))
    acts += [('rec',)] * rng.randint(1, 2)
    acts += [('obs',)] * rng.randint(1, 2)
    if rng.random() < 0.3:
        acts.append(('snap',))
    rng.shuffle(acts)
    deferred = 0
    for a in acts:
        if a[0] == 'decl':
            if not g.here(a[1]):
                cfg.declare(g, a[1], depth)
            while deferred and cfg.record(g):
                deferred -= 1
        elif a[0] == 'rec':
            if not cfg.record(g):
                deferred += 1
        elif a[0] == 'obs':
            g.observe()
        else:
            g.snapshot()
    g.observe()
    # a nested block is always the last item of its scope (scope END is H4's job)
    if depth < cfg.max_depth and rng.random() < (0.9 if depth == 0 else 0.6):
        kind = rng.choice(cfg.block_kinds)
        cond = cfg.cond(g) if kind in ('ifdecl', 'switchdecl') else None
        if kind in ('ifdecl', 'switchdecl') and cond is None:
            kind = 'plain'
        g.open_block(kind, cond)
        build_scope(g, cfg, depth + 1)
        g.close_block()


def skeleton(g, cfg):
    rng = g.rng
    g.fn = 'file'
    for name in g.names:
        if rng.random() < cfg.p_file:
            cfg.declare(g, name, -1)
    for _ in range(rng.randint(0, 2)):
        cfg.record(g)
    if rng.random() < 0.5:
        g.snapshot()
    use_helper = rng.random() < cfg.p_helper
    if use_helper:
        g.begin_func('f1')
        build_scope(g, cfg, 0)
        g.end_func()
    g.begin_func('main')
    if use_helper:
        acts = ['call', 'obs']
        if rng.random() < 0.4:
            acts.append('decl')
        if rng.random() < 0.4:
            acts.append('rec')
        rng.shuffle(acts)
        for a in acts:
            if a == 'call':
                g.items.append(Call('f1'))
            elif a == 'obs':
                g.observe()
            elif a == 'decl':
                nm = g.names[0]
                if not g.here(nm):
                    cfg.declare(g, nm, 0)
            else:
                cfg.record(g)
        g.observe()
    else:
        build_scope(g, cfg, 0)
    g.end_func()


def lit_declarer(pool):
    def declare(g, name, depth):
        g.declare_literal(name, pool)
    return declare


def rec_maker(kind, prefix, fieldfn):
    def record(g):
        k = kind if kind != 'any' else g.rng.choice(['struct', 'union'])
        return g.make_record(k, prefix if kind != 'any' else ('S' if k == 'struct' else 'U'),
                             fieldfn)
    return record


def pick_names(g, n, pool=NAME_POOL, mode='enum'):
    for nm in g.rng.sample(pool, n):
        g.names.append(nm)
        g.mode[nm] = mode


# ============================================================================
# Families
# ============================================================================
def gen_D1(g):
    pick_names(g, 2 if g.rng.random() < 0.3 else 1)
    skeleton(g, Cfg(lit_declarer(range(1, 17)), rec_maker('struct', 'S', fields_plain),
                    BASIC_KINDS))


def gen_D2(g):
    rng = g.rng
    pick_names(g, 1)
    N = g.names[0]
    nsib = rng.randint(2, 4)
    r = rng.random()
    file_pos = None if r < 0.25 else (0 if r < 0.7 else rng.randint(1, nsib - 1))
    shared = rng.random() < 0.6
    fstruct = rng.random() < 0.45
    record = rec_maker('struct', 'S', fields_plain)
    sibs = []
    for i in range(nsib):
        if file_pos == i:
            g.fn = 'file'
            g.declare_literal(N, range(1, 17))
            if fstruct:
                record(g)
            if rng.random() < 0.3:
                g.snapshot()
        fname = 'f%d' % i
        sibs.append(fname)
        g.begin_func(fname)
        tag = 'T' if shared else None
        modes = ['local-first'] * 3
        if g.visible(N):
            modes += ['struct-first', 'struct-first', 'no-local']
        mode = rng.choice(modes)
        if mode == 'local-first':
            if rng.random() < 0.3:
                g.observe()
            g.declare_literal(N, range(1, 17))
            g.make_record('struct', 'S', fields_plain, tag=tag)
            g.observe()
            if rng.random() < 0.3:
                record(g)
                g.observe()
        elif mode == 'struct-first':
            g.make_record('struct', 'S', fields_plain, tag=tag)
            g.observe()
            g.declare_literal(N, range(1, 17))
            record(g)
            g.observe()
        else:
            g.make_record('struct', 'S', fields_plain, tag=tag)
            g.observe()
        if rng.random() < 0.35:
            g.open_block(rng.choice(BASIC_KINDS))
            record(g)
            g.observe()
            g.close_block()
        g.end_func()
    if file_pos is None and rng.random() < 0.5:
        g.fn = 'file'
        g.declare_literal(N, range(1, 17))
        if fstruct:
            record(g)
    g.begin_func('main')
    if g.visible(N) and rng.random() < 0.4:
        g.observe()
    if rng.random() < 0.3:
        g.declare_literal(N, range(1, 17))
        record(g)
        g.observe()
    order = sibs[:]
    rng.shuffle(order)
    for fname in order:
        g.items.append(Call(fname))
        if rng.random() < 0.25:
            g.observe()
    g.observe()
    g.end_func()


def gen_H1(g):
    pick_names(g, 2 if g.rng.random() < 0.35 else 1)
    skeleton(g, Cfg(lit_declarer(range(1, 15)), rec_maker('struct', 'B', fields_bf),
                    BASIC_KINDS))


def gen_H2(g):
    pick_names(g, 2 if g.rng.random() < 0.35 else 1)
    skeleton(g, Cfg(lit_declarer(FUNDAMENTAL_ALIGNS), rec_maker('struct', 'A', fields_align),
                    BASIC_KINDS))


def gen_H3(g):
    rng = g.rng
    obj = rng.choice(OBJ_POOL)
    g.names.append(obj)
    g.mode[obj] = 'obj'
    if rng.random() < 0.75:
        m = rng.choice(NAME_POOL)
        g.names.append(m)
        g.mode[m] = 'mixed'

    def declare(g, name, depth):
        rng = g.rng
        static = depth < 0 or rng.random() < 0.2
        if g.mode[name] == 'mixed' and (depth < 0 or rng.random() < 0.35):
            g.declare_literal(name, range(1, 17))
            g.note(name, PSABI['int'][0], PSABI['int'][0])
            return
        forms = ['scalar', 'scalar', 'ptr', 'array', 'array']
        if g.visible(name) and g.hi.get(name, 99) <= 32:
            forms += ['self', 'self', 'self']
        form = rng.choice(forms)
        bound = None
        if form == 'scalar':
            p = rng.choice(['char', 'short', 'int', 'long', 'float', 'double'])
            base = ('prim', p)
            lo = hi = PSABI[p][0]
        elif form == 'ptr':
            base = ('ptr', 'char')
            lo = hi = PTR_SIZE_ALIGN[0]
        elif form == 'array':
            p = rng.choice(['char', 'short', 'int', 'double'])
            n = rng.randint(1, 32 // PSABI[p][0])
            base, bound = ('prim', p), Lit(n)
            lo = hi = PSABI[p][0] * n
        else:
            # self-sized: the bound's `name` is the OUTER one (6.2.1p7)
            k = rng.randint(1, 5)
            base = ('prim', 'char')
            bound = Bin('+', SizeofName(name, rng.random() < 0.4), Lit(k)) \
                if rng.random() < 0.7 else SizeofName(name, rng.random() < 0.4)
            add = k if isinstance(bound, Bin) else 0
            lo, hi = g.lo[name] + add, g.hi[name] + add
        g.items.append(ObjDecl(static, base, name, bound))
        g.declare_name(name, 'obj')
        g.note(name, lo, hi)

    skeleton(g, Cfg(declare, rec_maker('struct', 'S', fields_obj), BASIC_KINDS))


def gen_H5(g):
    pick_names(g, 2 if g.rng.random() < 0.25 else 1)

    def declare(g, name, depth):
        if g.visible(name) and g.rng.random() < 0.95:
            spec, lo, hi = g.selfref_spec(name)
            g.emit_enum(spec)
            g.note(name, lo, hi)
        else:
            g.declare_literal(name, range(1, 6))

    def cond(g):
        vis = g.vis_names()
        if not vis:
            return None
        nm = g.rng.choice(vis)
        spec, lo, hi = g.selfref_spec(nm)
        g.note(nm, lo, hi)
        return spec

    skeleton(g, Cfg(declare, rec_maker('struct', 'S', fields_selfref),
                    BASIC_KINDS + ['ifdecl', 'switchdecl'], p_file=1.0, cond=cond))


def gen_H6(g):
    pick_names(g, 2 if g.rng.random() < 0.3 else 1)
    skeleton(g, Cfg(lit_declarer(range(1, 17)), rec_maker('any', 'S', fields_h6), BASIC_KINDS))


def gen_H4(g):
    rng = g.rng
    pick_names(g, 2 if rng.random() < 0.3 else 1)
    N = g.names[0]
    pool = range(1, 17)
    record = rec_maker('struct', 'S', fields_plain)
    g.fn = 'file'
    for nm in g.names:
        if rng.random() < 0.8:
            g.declare_literal(nm, pool)
    if rng.random() < 0.6:
        record(g)
    kinds = ['plain', 'plain', 'if1', 'do0', 'for1', 'switch1', 'ifdecl', 'ifdecl',
             'ifdecl1', 'switchdecl']

    def cond_spec():
        nm = rng.choice(g.names)
        v = g.pick_value(nm, pool)
        g.note(nm, v, v)
        spec = EnumSpec(None, [Enumerator(nm, Lit(v))]) if rng.random() < 0.6 \
            else g.enum_literal_spec(nm, v)
        return spec

    def block(depth):
        kind = rng.choice(kinds)
        cond = cond_spec() if kind in ('ifdecl', 'ifdecl1', 'switchdecl') else None
        g.open_block(kind, cond)
        if kind == 'ifdecl1':
            nm = cond.items[-1].name if cond.items[-1].name in g.names else \
                [e.name for e in cond.items if e.name in g.names][0]
            pre = '%s.%s' % (g.fn, g.fresh('s'))
            g.pr('%s.val.%s' % (pre, nm), 'd', ('name', nm))
            g.close_block()
            return
        for nm in g.names:
            p = 0.8 if cond is None else 0.3
            if nm == N or rng.random() < 0.5:
                if rng.random() < p and not g.here(nm):
                    g.declare_literal(nm, pool)
        if rng.random() < 0.8:
            record(g)
        g.observe()
        if depth < 2 and rng.random() < 0.45:
            block(depth + 1)
            g.observe()                      # scope end at depth 2
            if rng.random() < 0.4:
                record(g)
                g.observe()
        g.close_block()

    use_helper = rng.random() < 0.7
    g.begin_func('f1' if use_helper else 'main')
    if not g.visible(N) or rng.random() < 0.5:
        g.declare_literal(N, pool)
    if rng.random() < 0.6:
        record(g)
    for _ in range(rng.randint(2, 4)):
        if rng.random() < 0.6:
            g.observe()
        block(1)
        g.observe()                          # the key observation: after scope end
        if rng.random() < 0.5:
            record(g)
            g.observe()
    g.end_func()
    if use_helper:
        g.begin_func('main')
        if rng.random() < 0.4:
            g.observe()
        g.items.append(Call('f1'))
        g.observe()
        g.end_func()


FAMILIES = collections.OrderedDict([
    ('D1', (gen_D1, 'design', 'enum constant as member array bound, shadowed at '
            'file/function/block scope, struct defined at each scope')),
    ('D2', (gen_D2, 'design', 'sibling functions with same-named local enum constant '
            'and local struct (often same tag) using it')),
    ('H1', (gen_H1, 'heldout', 'bit-field widths from shadowed constants (all-ones read-back)')),
    ('H2', (gen_H2, 'heldout', '_Alignas(shadowed constant) on members; _Alignof/offsetof/sizeof')),
    ('H3', (gen_H3, 'heldout', 'sizeof obj in member bounds; obj shadowed with different '
            'types, self-sized objects, enum/object mixed names')),
    ('H4', (gen_H4, 'heldout', 'scope end: uses after a shadowing block (incl. if/switch '
            'controlling-expression declarations) bind the outer declaration')),
    ('H5', (gen_H5, 'heldout', 'self-referential enumerators enum { N = N + k } in inner '
            'scopes, in members and in controlling expressions')),
    ('H6', (gen_H6, 'heldout', 'unions, nested struct/union members, record-typed members, '
            'enum constants declared inside member declarations')),
])


def expr_names(e):
    """Identifiers referenced by an abstract expression / type name."""
    if e is None or isinstance(e, Lit):
        return set()
    if isinstance(e, Name):
        return {e.n}
    if isinstance(e, SizeofName):
        return {e.n}
    if isinstance(e, (SizeofType, AlignofType)):
        return tn_names(e.tn)
    if isinstance(e, Bin):
        return expr_names(e.a) | expr_names(e.b)
    return set()


def tn_names(tn):
    t = tn.t
    if t[0] == 'arr':
        return expr_names(t[2])
    if t[0] == 'td':
        return {t[1]}
    return set()


FEATURES = [
    ('shadowed', 'a name declared in >= 2 nested scopes'),
    ('decl-after-use', 'a record/bound in a scope uses a name that the SAME scope declares later'),
    ('use-after-block', 'a name declared inside a block is used again after the block closed'),
    ('sibling-same-tag', 'two functions define a record with the same tag'),
    ('implicit-enumerator', 'an enumerator without `=` (value = previous + 1, 6.7.2.2p3)'),
    ('selfref-enumerator', 'enum { N = ...N... } (right side sees the outer N)'),
    ('cond-declaration', 'enumeration constant declared in an if/switch controlling expression'),
    ('enum-in-member', 'enumeration constant declared inside a struct member declaration'),
    ('nested-record', 'record defined inside a member declaration'),
    ('record-member', 'member of a previously defined record type'),
    ('union', 'union definition'),
    ('bitfield', 'bit-field with a named-constant width'),
    ('alignas', '_Alignas on a member'),
    ('sizeof-object', 'sizeof applied to an object in a constant context'),
    ('self-sized-object', 'object whose array bound uses sizeof of its own (outer) name'),
    ('mixed-kind', 'one name is an enumeration constant in one scope, an object in another'),
]


def features(tu):
    """Constructs present in an abstract program (for coverage reporting only)."""
    feats = set()
    decl_depths = collections.defaultdict(set)
    kinds = collections.defaultdict(set)
    fn_tags = collections.defaultdict(set)

    def spec_names(spec, depth):
        out = set()
        for i, en in enumerate(spec.items):
            out.add(en.name)
            decl_depths[en.name].add(depth)
            kinds[en.name].add('enum')
            if en.expr is not None and en.name in expr_names(en.expr):
                feats.add('selfref-enumerator')
            if en.expr is None and i > 0:
                feats.add('implicit-enumerator')
        return out

    def field_info(fields, depth, uses):
        """names used by bounds/widths/alignas; names declared inside members."""
        declared = set()
        for f in fields:
            for e in [f.bound, f.width] + list(f.alignas):
                n = expr_names(e)
                uses |= n
                if any(isinstance(x, SizeofName) for x in [e] + ([e.a, e.b] if isinstance(e, Bin) else [])):
                    feats.add('sizeof-object')
            if f.alignas:
                feats.add('alignas')
            if f.width is not None:
                feats.add('bitfield')
            b = f.base
            if b[0] == 'enumptr':
                feats.add('enum-in-member')
                declared |= spec_names(b[1], depth)
            elif b[0] == 'recdef':
                feats.add('nested-record')
                if b[1].kind == 'union':
                    feats.add('union')
                declared |= field_info(b[1].fields, depth, uses)
            elif b[0] == 'recref':
                feats.add('record-member')
        return declared

    def walk(items, depth, fname):
        used = set()
        closed = set()
        for it in items:
            if isinstance(it, EnumDecl):
                d = spec_names(it.spec, depth)
                if d & used:
                    feats.add('decl-after-use')
                for en in it.spec.items:
                    n = expr_names(en.expr)
                    if n & closed:
                        feats.add('use-after-block')
                    used |= n
            elif isinstance(it, RecDecl):
                uses = set()
                if it.spec.kind == 'union':
                    feats.add('union')
                fn_tags[fname].add(it.spec.tag)
                d = field_info(it.spec.fields, depth, uses)
                if uses & closed:
                    feats.add('use-after-block')
                if d & used:
                    feats.add('decl-after-use')
                used |= uses
            elif isinstance(it, ObjDecl):
                decl_depths[it.name].add(depth)
                kinds[it.name].add('obj')
                if it.name in used:
                    feats.add('decl-after-use')
                if it.bound is not None and it.name in expr_names(it.bound):
                    feats.add('self-sized-object')
                used |= expr_names(it.bound)
            elif isinstance(it, TypedefDecl):
                n = expr_names(it.bound)
                if n & closed:
                    feats.add('use-after-block')
                used |= n
            elif isinstance(it, Print):
                w = it.what
                n = {w[1]} if w[0] in ('name', 'sizeofname') else \
                    tn_names(w[1]) if w[0] in ('sizeoftype', 'alignof') else set()
                if n & closed:
                    feats.add('use-after-block')
                used |= n
            elif isinstance(it, Block):
                inner = set()
                if it.cond is not None:
                    feats.add('cond-declaration')
                    inner |= spec_names(it.cond, depth + 1)
                for x in it.items:
                    if isinstance(x, EnumDecl):
                        inner |= {en.name for en in x.spec.items}
                    elif isinstance(x, ObjDecl):
                        inner.add(x.name)
                    elif isinstance(x, RecDecl):
                        for f in x.spec.fields:
                            if f.base[0] == 'enumptr':
                                inner |= {en.name for en in f.base[1].items}
                walk(it.items, depth + 1, fname)
                closed |= inner

    for it in tu.items:
        if isinstance(it, FuncDef):
            walk(it.items, 1, it.name)
        else:
            walk([it], 0, 'file')
    for n, ds in decl_depths.items():
        if len(ds) >= 2:
            feats.add('shadowed')
    if any(len(k) > 1 for k in kinds.values()):
        feats.add('mixed-kind')
    fns = [t for f, t in fn_tags.items() if f != 'file']
    for i in range(len(fns)):
        for j in range(i + 1, len(fns)):
            if fns[i] & fns[j]:
                feats.add('sibling-same-tag')
    return feats


def generate(family, seed):
    g = Gen(family, seed)
    FAMILIES[family][0](g)
    return g.tu


def expected(tu, mutant=None):
    return Model(mutant).run(tu)


# ============================================================================
# CLI
# ============================================================================
def parse_families(s):
    out = []
    for part in s.split(','):
        part = part.strip()
        if not part:
            continue
        if part == 'all':
            out += list(FAMILIES)
        elif part in ('design', 'heldout'):
            out += [f for f, v in FAMILIES.items() if v[1] == part]
        elif part in FAMILIES:
            out.append(part)
        else:
            raise SystemExit('unknown family %r (see --list-families)' % part)
    seen = []
    for f in out:
        if f not in seen:
            seen.append(f)
    return seen


def parse_seeds(s):
    out = []
    for part in s.split(','):
        if '-' in part:
            a, b = part.split('-')
            out += list(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


def write_pair(tu, cpath, epath):
    with open(cpath, 'w') as f:
        f.write(render(tu))
    with open(epath, 'w') as f:
        f.write(''.join(ln + '\n' for ln in expected(tu)))


def cmd_render(a):
    tu = generate(a.family, a.seed)
    src = render(tu)
    exp = expected(tu, a.mutant)
    if a.out:
        with open(a.out, 'w') as f:
            f.write(src)
    else:
        sys.stdout.write(src)
    if a.expect:
        with open(a.expect, 'w') as f:
            f.write(''.join(ln + '\n' for ln in exp))
    elif a.out:
        sys.stdout.write(''.join(ln + '\n' for ln in exp))


def cmd_corpus(a):
    for fam in parse_families(a.families):
        d = os.path.join(a.out, fam)
        os.makedirs(d, exist_ok=True)
        for s in parse_seeds(a.seeds):
            tu = generate(fam, s)
            write_pair(tu, os.path.join(d, '%s_%04d.c' % (fam, s)),
                       os.path.join(d, '%s_%04d.expect' % (fam, s)))
    print('wrote corpus to %s' % a.out)


def resolve_cc(name):
    p = shutil.which(name) or (name if os.path.exists(name) else None)
    if p is None:
        raise SystemExit('compiler %s not found' % name)
    return p


def cc_version(path):
    try:
        return subprocess.run([path, '--version'], capture_output=True, text=True,
                              timeout=30).stdout.splitlines()[0].strip()
    except Exception as e:  # pragma: no cover
        return 'unknown (%s)' % e


def audit_one(job):
    fam, seed, ccs, opts, work, keep, san = job
    res = {'fam': fam, 'seed': seed, 'problems': [], 'warn': 0, 'lines': 0,
           'builds': 0, 'runs': 0, 'agree': 0, 'variants': 0, 'cc_equal': True,
           'killed': {}, 'feats': set(), 'san': None}
    try:
        tu = generate(fam, seed)
        src = render(tu)
        exp = expected(tu)
    except ModelError as e:
        res['problems'].append('GENERATOR/MODEL ERROR: %s' % e)
        return res
    res['lines'] = len(exp)
    res['feats'] = features(tu)
    for m in MUTANTS:
        try:
            res['killed'][m] = expected(tu, m) != exp
        except ModelError:
            res['killed'][m] = True
    d = os.path.join(work, fam)
    os.makedirs(d, exist_ok=True)
    base = os.path.join(d, '%s_%04d' % (fam, seed))
    with open(base + '.c', 'w') as f:
        f.write(src)
    with open(base + '.expect', 'w') as f:
        f.write(''.join(ln + '\n' for ln in exp))
    outs = {}
    for cc in ccs:
        for opt in opts:
            key = '%s-%s' % (os.path.basename(cc), opt)
            res['variants'] += 1
            ok, got, diag, err = build_and_run(cc, ['-' + opt], base, key, keep)
            res['warn'] += 1 if diag else 0
            if not ok:
                res['problems'].append('%s: %s' % (key, err))
                continue
            res['builds'] += 1
            res['runs'] += 1
            outs[key] = got
            if got == exp:
                res['agree'] += 1
            else:
                n = max(len(exp), len(got))
                e2 = exp + ['<eof>'] * (n - len(exp))
                g2 = got + ['<eof>'] * (n - len(got))
                diff = [(i + 1, x, y) for i, (x, y) in enumerate(zip(e2, g2)) if x != y][:6]
                res['problems'].append('%s: output differs from model: %s' % (
                    key, '; '.join('line %d expected %r got %r' % x for x in diff)))
    vals = list(outs.values())
    res['cc_equal'] = len(vals) == res['variants'] and all(v == vals[0] for v in vals)
    if san:
        key = '%s-O1-asan-ubsan' % os.path.basename(san)
        ok, got, diag, err = build_and_run(
            san, ['-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'],
            base, key, keep)
        if not ok:
            res['problems'].append('%s: %s' % (key, err))
        elif got != exp:
            res['problems'].append('%s: output differs from model' % key)
        res['san'] = ok and got == exp
    return res


def build_and_run(cc, flags, base, key, keep):
    """-> (ok, stdout_lines, had_compiler_diagnostic, error_text)"""
    exe = '%s.%s' % (base, key)
    p = subprocess.run([cc, '-std=c17', '-pedantic-errors'] + flags + [base + '.c', '-o', exe],
                       capture_output=True, text=True, timeout=300)
    diag = bool(p.stderr.strip())
    if p.returncode != 0:
        return False, None, diag, 'compile failed rc=%d\n%s' % (p.returncode,
                                                                 p.stderr.strip()[:2000])
    try:
        r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return False, None, diag, 'run timed out'
    finally:
        if not keep and os.path.exists(exe):
            os.remove(exe)
    if r.returncode != 0 or r.stderr.strip():
        return False, None, diag, 'exit status %d, stderr: %s' % (r.returncode,
                                                                 r.stderr.strip()[:2000])
    return True, r.stdout.splitlines(), diag, None


def cmd_audit(a):
    fams = parse_families(a.families)
    seeds = parse_seeds(a.seeds)
    ccs = [resolve_cc(c) for c in (a.cc or ['clang', 'gcc'])]
    opts = [o.strip().lstrip('-') for o in a.opts.split(',') if o.strip()]
    os.makedirs(a.work, exist_ok=True)
    san = resolve_cc(a.sanitize_cc) if a.sanitize_cc else None
    jobs = [(f, s, ccs, opts, a.work, a.keep, san) for f in fams for s in seeds]
    t0 = time.time()
    results = collections.defaultdict(list)
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        for res in ex.map(audit_one, jobs):
            results[res['fam']].append(res)
    dt = time.time() - t0
    w = sys.stdout.write
    w('# scope_oracle.py audit\n')
    w('# date: %s   wall time: %.1fs\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), dt))
    with open(os.path.abspath(__file__), 'rb') as f:
        w('# oracle: scope_oracle.py sha256=%s  python %s\n' % (
            hashlib.sha256(f.read()).hexdigest(), sys.version.split()[0]))
    for cc in ccs:
        w('# compiler: %s  (%s)\n' % (cc, cc_version(cc)))
    w('# flags: -std=c17 -pedantic-errors {%s}\n' % ','.join('-' + o for o in opts))
    if san:
        w('# sanitizer pass: %s -std=c17 -pedantic-errors -O1 -fsanitize=address,undefined '
          '-fno-sanitize-recover=all (output must also equal the model)\n' % san)
    w('# families: %s   seeds: %s (%d per family)\n' % (','.join(fams), a.seeds, len(seeds)))
    w('#\n# per family: programs, expected label lines, builds ok, runs ok (exit 0),\n'
      '#   model==compiler (per build variant), clang==gcc==all opt levels (per program),\n'
      '#   programs with any disagreement, programs with any compiler diagnostic\n')
    tot = collections.Counter()
    all_problems = []
    for fam in fams:
        rs = results[fam]
        c = collections.Counter()
        for r in rs:
            c['programs'] += 1
            c['lines'] += r['lines']
            c['builds'] += r['builds']
            c['runs'] += r['runs']
            c['agree'] += r['agree']
            c['variants'] += r['variants']
            c['cc_equal'] += 1 if r['cc_equal'] else 0
            c['bad'] += 1 if r['problems'] or not r['cc_equal'] else 0
            c['warn'] += 1 if r['warn'] else 0
            c['san_ok'] += 1 if r['san'] else 0
            for p in r['problems']:
                all_problems.append('%s seed %d: %s' % (fam, r['seed'], p))
        tot.update(c)
        w('%-3s programs=%d lines=%d builds=%d/%d runs=%d/%d model==cc=%d/%d '
          'clang==gcc=%d/%d disagreements=%d diag_programs=%d\n' % (
              fam, c['programs'], c['lines'], c['builds'], c['variants'], c['runs'],
              c['variants'], c['agree'], c['variants'], c['cc_equal'], c['programs'],
              c['bad'], c['warn']) + ('    sanitizer_clean=%d/%d\n' % (
                  c['san_ok'], c['programs']) if san else ''))
    w('ALL programs=%d lines=%d builds=%d/%d runs=%d/%d model==cc=%d/%d clang==gcc=%d/%d '
      'disagreements=%d diag_programs=%d\n' % (
          tot['programs'], tot['lines'], tot['builds'], tot['variants'], tot['runs'],
          tot['variants'], tot['agree'], tot['variants'], tot['cc_equal'], tot['programs'],
          tot['bad'], tot['warn']) + ('    sanitizer_clean=%d/%d\n' % (
              tot['san_ok'], tot['programs']) if san else ''))
    w('#\n# construct coverage (programs per family containing the construct; computed\n'
      '# from the abstract tree)\n')
    for f, doc in FEATURES:
        w('#   %-20s %s\n' % (f, doc))
    w('%-20s' % 'construct' + ''.join('%8s' % f for f in fams) + '\n')
    for f, doc in FEATURES:
        w('%-20s' % f + ''.join('%8d' % sum(f in r['feats'] for r in results[fam])
                                  for fam in fams) + '\n')
    w('#\n# mutant sensitivity: programs whose expected output changes (or becomes\n'
      '# ill-formed) under a deliberately wrong binding rule (model-only, no compiler)\n')
    for m in MUTANTS:
        w('#   %-16s %s\n' % (m, MUTANT_DOC[m]))
    w('%-4s' % 'fam' + ''.join('%17s' % m for m in MUTANTS) + '\n')
    for fam in fams:
        rs = results[fam]
        w('%-4s' % fam + ''.join('%17s' % ('%d/%d' % (sum(r['killed'].get(m, False)
                                                           for r in rs), len(rs)))
                                 for m in MUTANTS) + '\n')
    w('#\n')
    if all_problems:
        w('DISAGREEMENTS/PROBLEMS (%d):\n' % len(all_problems))
        for p in all_problems:
            w('  ' + p.replace('\n', '\n    ') + '\n')
    else:
        w('DISAGREEMENTS/PROBLEMS: none\n')
    return 1 if all_problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list-families', action='store_true', help='list families and exit')
    sub = ap.add_subparsers(dest='cmd')
    r = sub.add_parser('render', help='render one program and its expected output')
    r.add_argument('--family', required=True, choices=list(FAMILIES))
    r.add_argument('--seed', required=True, type=int)
    r.add_argument('--out', help='C file to write (default: stdout)')
    r.add_argument('--expect', help='expected-output file to write')
    r.add_argument('--mutant', choices=MUTANTS, help=argparse.SUPPRESS)
    c = sub.add_parser('corpus', help='render many programs + expectations')
    c.add_argument('--families', default='all')
    c.add_argument('--seeds', default='0-199')
    c.add_argument('--out', required=True)
    au = sub.add_parser('audit', help='check the model against reference compilers')
    au.add_argument('--families', default='all')
    au.add_argument('--seeds', default='0-199')
    au.add_argument('--cc', action='append', help='reference compiler (repeatable)')
    au.add_argument('--opts', default='O0,O2')
    au.add_argument('--work', required=True)
    au.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    au.add_argument('--keep', action='store_true', help='keep executables')
    au.add_argument('--sanitize-cc', help='also build each program with this compiler and '
                    '-fsanitize=address,undefined; its output must equal the model')
    a = ap.parse_args(argv)
    if a.list_families:
        for f, (fn, grp, doc) in FAMILIES.items():
            print('%s  %-7s  %s' % (f, grp, doc))
        return 0
    if a.cmd == 'render':
        cmd_render(a)
        return 0
    if a.cmd == 'corpus':
        cmd_corpus(a)
        return 0
    if a.cmd == 'audit':
        return cmd_audit(a)
    ap.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main())
