#include <buster/lib/target.h>
#include <stdio.h>

int main(void)
{
    int result = 0;
    for (u32 arch = 0; arch <= CPU_ARCH_COUNT; arch += 1)
    {
        for (u32 os = 0; os <= OPERATING_SYSTEM_COUNT; os += 1)
        {
            for (u32 policy = 0; policy < 3; policy += 1)
            {
                for (u32 stack = 0; stack < 7; stack += 1)
                {
                    Target target = {.cpu_arch = (CpuArch)arch, .os = (OperatingSystem)os,
                        .plain_char_policy = (TargetPlainCharPolicy)policy, .x86_preferred_stack_boundary = (u8)stack};
                    TargetDataLayout layout = target_data_layout(target);
                    if (fwrite(&layout, sizeof(layout), 1, stdout) != 1) result = 1;
                }
            }
        }
    }
    return result;
}
