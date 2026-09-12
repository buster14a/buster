; Pinned LLVM 18 opaque-pointer input; the NVVM annotation declares a kernel.
target triple = "nvptx64-nvidia-cuda"
define void @buster_gpu_smoke(ptr addrspace(1) %output, i32 %input) {
entry:
  %product = mul i32 %input, 3
  %value = add i32 %product, 7
  store i32 %value, ptr addrspace(1) %output, align 4
  ret void
}
!nvvm.annotations = !{!0}
!0 = !{ptr @buster_gpu_smoke, !"kernel", i32 1}
