from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
codegen_path = root / "src/buster/lib/compiler/codegen/codegen.c"
test_path = root / "tests/basic_c_int128.c"

codegen = codegen_path.read_text(encoding="utf-8")
old = "if (value_type && value_type->kind == IR_TYPE_INTEGER && value_type->layout.resolved && value_type->layout.size == 16)"
new = "if (value_type && value_type->layout.resolved && value_type->layout.size == 16)"
if codegen.count(old) != 1:
    raise RuntimeError(f"compare-exchange gate: expected one match, found {codegen.count(old)}")
codegen = codegen.replace(old, new, 1)
codegen_path.write_text(codegen, encoding="utf-8")

test = test_path.read_text(encoding="utf-8")
anchor = "static _Atomic(WideUnsigned) atomic_wide;\n"
addition = """
typedef struct WidePair
{
    unsigned long long low;
    unsigned long long high;
} WidePair;

static _Atomic(WidePair) atomic_pair;

static int wide_pair_equal(WidePair left, WidePair right)
{
    return left.low == right.low && left.high == right.high;
}

static int test_atomic_wide_pair(void)
{
    WidePair initial = {0x0123456789abcdefULL, 0xfedcba9876543210ULL};
    WidePair desired = {0x8877665544332211ULL, 0x1020304050607080ULL};
    __c11_atomic_store(&atomic_pair, initial, __ATOMIC_SEQ_CST);
    WidePair loaded = __c11_atomic_load(&atomic_pair, __ATOMIC_ACQUIRE);
    if (!wide_pair_equal(loaded, initial))
        return 0;
    WidePair expected = {0, 0};
    if (__c11_atomic_compare_exchange_strong(&atomic_pair, &expected, desired, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
    if (!wide_pair_equal(expected, initial))
        return 0;
    if (!__c11_atomic_compare_exchange_strong(&atomic_pair, &expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        return 0;
    loaded = __c11_atomic_load(&atomic_pair, __ATOMIC_RELAXED);
    return wide_pair_equal(loaded, desired) && (((unsigned long long)(void*)&atomic_pair & 15ULL) == 0);
}
"""
if test.count(anchor) != 1:
    raise RuntimeError(f"test insertion anchor: expected one match, found {test.count(anchor)}")
test = test.replace(anchor, anchor + addition, 1)
old_tail = """    if (!test_atomic_wide(high))
        return 11;
    return 0;
}
"""
new_tail = """    if (!test_atomic_wide(high))
        return 11;
    if (!test_atomic_wide_pair())
        return 12;
    return 0;
}
"""
if test.count(old_tail) != 1:
    raise RuntimeError(f"test call anchor: expected one match, found {test.count(old_tail)}")
test_path.write_text(test.replace(old_tail, new_tail, 1), encoding="utf-8")

print("patched issue 195 and added aggregate atomic regression")
