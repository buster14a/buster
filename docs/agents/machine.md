  image. Passing lists between Buster and another compiler still requires the
  public AAPCS64 `va_list` representation; that work is
  tracked in [#360](https://github.com/buster14a/buster/issues/360).
- Empty inline assembly with no operands or targets and exactly one `memory`
  clobber selects a zero-byte compiler-barrier row on x86-64 and AArch64. The
  row is a scheduler and memory barrier even though it emits no instruction.
  Templates, operands, register/flags clobbers and asm-goto remain outside
  this deliberately narrow #70 slice.
- x86 CPUID/XGETBV literal assembly with complete 32-bit pure outputs and
  separate fixed inputs selects constrained machine rows. Numeric/named ties
  retain the input's fixed register. CPUID consumes RAX/RCX together and
