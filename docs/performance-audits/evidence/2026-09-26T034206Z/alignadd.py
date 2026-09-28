# Exhaustive check of the align-then-add block summary used in the audit's
# rejected-at-census table: every step x -> align_up(x + s_before, a) + s_after
# (x86-64 places the slot before aligning, AArch64 after) is a member of
# F(p, A, q)(x) = align_up(x + p, A) + q, A a power of two, and the family is
# closed under composition with the rule below.
def up(x, a): return (x + a - 1) & ~(a - 1)
def apply(t, x):
    p, A, q = t
    return up(x + p, A) + q
def compose(t1, t2):  # first t1, then t2
    p1, A1, q1 = t1; p2, A2, q2 = t2
    c = q1 + p2
    if A2 <= A1:
        return (p1, A1, up(c, A2) + q2)
    return (p1 + up(c, A1), A2, q2)
aligns = [1, 2, 4, 8, 16]
elems = [(p, A, q) for A in aligns for p in range(0, 20) for q in range(0, 20)]
checks = 0
for t1 in elems:
    for t2 in elems:
        t12 = compose(t1, t2)
        for x in range(0, 64):
            assert apply(t12, x) == apply(t2, apply(t1, x)), (t1, t2, x)
            checks += 1
# associativity on a sample of triples
import random
random.seed(1)
for _ in range(200000):
    a, b, c = random.choice(elems), random.choice(elems), random.choice(elems)
    left, right = compose(compose(a, b), c), compose(a, compose(b, c))
    for x in range(0, 64, 7):
        assert apply(left, x) == apply(right, x)
        checks += 1
print("align-add composition checks", checks, "ok")
