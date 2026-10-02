"""Independent semantic checksums for the hosted runtime.c probe.

This is a correctness oracle, not a performance workload.  Kernel 0--4 use
the modular sum of one repetition instead of the C program's nested loops.
The state-dependent kernels 5--6 retain the required ordering.  All integer
operations have explicit C unsigned 64-bit (or index unsigned 32-bit) width.
Floating arithmetic retains binary64 operation order; the tested counts
1, 3 and 19 are small enough that all these binary fractions sum exactly.

The output contract is Linux x86-64: unsigned long long is 64 bits and the
double/unsigned union observes the little-endian IEEE binary64 bit pattern.
No compiler-produced result is used to establish an expected value.
"""

import struct


U64_MASK = (1 << 64) - 1
U32_MASK = (1 << 32) - 1
COUNT = 1024


def _add(left: int, right: int) -> int:
    return (left + right) & U64_MASK


def _mul(left: int, right: int) -> int:
    return (left * right) & U64_MASK


def _xor(left: int, right: int) -> int:
    return (left ^ right) & U64_MASK


def _shl(value: int, count: int) -> int:
    return (value << count) & U64_MASK


def parse_u64_decimal(text: str) -> int:
    """Match runtime.c's digit-prefix parser, including unsigned overflow."""
    value = 0
    for character in text:
        if not "0" <= character <= "9":
            break
        value = _add(_mul(value, 10), ord(character) - ord("0"))
    return value


def _initial_values(seed: int) -> tuple[list[int], list[int], list[float]]:
    words = []
    shifted_words = []
    fractions = []
    state = seed & U64_MASK
    for _ in range(COUNT):
        state = _add(_mul(state, 6364136223846793005), 1442695040888963407)
        words.append(state)
        shifted_words.append(state >> 7)
        fractions.append(float(state & 1023) * 0.25)
    return words, shifted_words, fractions


def _helper(word: int, seed: int) -> int:
    combined = _add(_mul(_xor(word, seed), 33), word >> 7)
    return _xor(combined, _shl(seed, 3))


def _invariant_total(kernel_index: int, seed: int, words: list[int], shifted_words: list[int]) -> int:
    """Sum one outer repetition in the ring of integers modulo 2**64."""
    total = 0
    divisor = (seed & 31) + 1
    x = _add(seed, 19)
    y = _add(_mul(seed, 3), 7)
    product = _mul(x, y)
    coefficient = _add(x, y)
    for word, shifted in zip(words, shifted_words):
        if kernel_index == 0:
            term = word
        elif kernel_index == 1:
            term = _add(word // divisor, shifted // 37)
        elif kernel_index == 2:
            term = _add(_mul(_add(word, product), coefficient), word >> (seed & 15))
        elif kernel_index == 3:
            repeated = _add(_mul(word, shifted), seed)
            term = _add(_add(_xor(repeated, shifted), repeated), _mul(repeated, 3))
        else:
            term = _add(_helper(word, seed), _helper(shifted, _add(seed, 1)))
        total = _add(total, term)
    return total


def expected_checksum(kernel_index: int, reps: int, seed: int) -> str:
    """Return exactly sixteen lowercase hexadecimal digits, without newline."""
    if not 0 <= kernel_index <= 7:
        raise ValueError("kernel_index must be between 0 and 7")
    if reps < 0 or seed < 0:
        raise ValueError("runtime arguments must be nonnegative")
    reps &= U64_MASK
    seed &= U64_MASK
    words, shifted_words, fractions = _initial_values(seed)
    result = seed
    if kernel_index <= 4:
        total = _invariant_total(kernel_index, seed, words, shifted_words)
        result = _add(seed, _mul(reps, total))
    elif kernel_index == 5:
        for _ in range(reps):
            for word in words:
                value = _xor(word, result)
                if value & 128:
                    result = _add(result, _mul(value, 7))
                else:
                    result = _xor(result, value >> 3)
    elif kernel_index == 6:
        for _ in range(reps):
            for index in range(COUNT):
                # C casts seed to unsigned before the index addition.
                source_index = ((index * 17 + (seed & U32_MASK)) & U32_MASK) & (COUNT - 1)
                shifted_words[index] = _add(words[source_index], result)
                result = _add(result, shifted_words[index] >> 11)
        result = _xor(result, shifted_words[(seed & U32_MASK) & (COUNT - 1)])
    else:
        accumulator = float(seed & 255) * 0.25
        for _ in range(reps):
            for fraction in fractions:
                accumulator += fraction * 0.5 + 0.125
        result = struct.unpack("<Q", struct.pack("<d", accumulator))[0]
    return f"{result:016x}"


def expected(kernel_index: int, reps: int, seed: int) -> str:
    """Compatibility wrapper matching the C printf trailing newline."""
    return expected_checksum(kernel_index, reps, seed) + "\n"


def self_check() -> None:
    """Analytic controls independent of the compiler-generated probe."""
    assert parse_u64_decimal("") == 0
    assert parse_u64_decimal("123tail") == 123
    assert parse_u64_decimal("-1") == 0
    assert parse_u64_decimal("18446744073709551616") == 0
    assert expected_checksum(0, 0, 0) == "0000000000000000"
    assert expected_checksum(0, 0, 19) == "0000000000000013"
    assert expected_checksum(0, 0, U64_MASK) == "ffffffffffffffff"
    assert expected_checksum(7, 0, 0) == "0000000000000000"
    assert expected_checksum(7, 0, 1) == "3fd0000000000000"  # 0.25
    assert expected_checksum(7, 0, 255) == "404fe00000000000"  # 63.75
    # The LCG modulo 1024 is full-period: multiplier == 1 mod 4 and
    # increment odd.  Its first 1024 states therefore cover 0..1023.
    # Sum((state & 1023)/8 + 1/8) = (523776 + 1024)/8 = 65600.
    assert expected_checksum(7, 1, 0) == "40f0040000000000"  # 65600.0
