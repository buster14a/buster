
#pragma once

#include <buster/aarch64.h>
#include <buster/system_headers.h>
#include <buster/os.h>

ENUM_T(A64Implementer, u8,
    A64_IMPLEMENTER_ARM = 0x41,
    A64_IMPLEMENTER_BROADCOM = 0x42,
    A64_IMPLEMENTER_CAVIUM_MARVELL = 0x43,
    A64_IMPLEMENTER_FUJITSU = 0x46,
    A64_IMPLEMENTER_HI_SILICON = 0x48,
    A64_IMPLEMENTER_NVIDIA = 0x4e,
    A64_IMPLEMENTER_QUALCOMM = 0x51,
    A64_IMPLEMENTER_SAMSUNG = 0x53,
    A64_IMPLEMENTER_APPLE = 0x61,
    A64_IMPLEMENTER_ARM_CHINA = 0x63,
    A64_IMPLEMENTER_MICROSOFT = 0x6d,
    A64_IMPLEMENTER_AMPERE = 0xc0,
    A64_IMPLEMENTER_UNKNOWN = 0xff,
);

ENUM_T(A64PartNumber, u16,
    A64_PART_CORTEX_A53                 = 0xD03,
    A64_PART_CORTEX_A55                 = 0xD05,
    A64_PART_CORTEX_A57                 = 0xD07,
    A64_PART_CORTEX_A72                 = 0xD08,
    A64_PART_CORTEX_A73                 = 0xD09,
    A64_PART_CORTEX_A75                 = 0xD0A,
    A64_PART_CORTEX_A76                 = 0xD0B,
    A64_PART_CORTEX_A77                 = 0xD0D,
    A64_PART_CORTEX_A78                 = 0xD41,
    A64_PART_CORTEX_X1                  = 0xD44,
    A64_PART_NEOVERSE_N1                = 0xD49,
    A64_PART_NEOVERSE_N2                = 0xD4F,
    A64_PART_NEOVERSE_V1                = 0xD80,
);

STRUCT(IdentificationRegister)
{
    u16 revision:4;
    u16 part_number:12;
    u8 architecture:4;
    u8 variant:4;
    A64Implementer implementer;
};

static_assert(sizeof(IdentificationRegister) == sizeof(u32));

BUSTER_IMPL CpuModel cpu_detect_model_aarch64()
{
    CpuModel result = CPU_MODEL_ERROR;

#if defined(__linux__)
#define BUSTER_AARCH64_BUFFER_LENGTH (sizeof(u64) * 2 + 2)
    char8 buffer[BUSTER_AARCH64_BUFFER_LENGTH + 4096];
    let fd = os_file_open(SOs("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1"), (OpenFlags){ .read = 1 }, (OpenPermissions){});
    let midr_el1_string = BUSTER_ARRAY_TO_SLICE(String8, buffer);
    midr_el1_string.length = BUSTER_AARCH64_BUFFER_LENGTH;
    let file_size = os_file_read(fd, BUSTER_SLICE_TO_BYTE_SLICE(midr_el1_string), BUSTER_AARCH64_BUFFER_LENGTH);
    buffer[file_size] = 0;
    os_file_close(fd);

    if (file_size == BUSTER_AARCH64_BUFFER_LENGTH)
    {
        if (buffer[0] == '0' && buffer[1] == 'x')
        {
            let value = string8_parse_u64_hexadecimal((char*)buffer + 2).value;

            if (value <= INT32_MAX)
            {
                let value_u32 = (u32)value;
                let id_register = *(IdentificationRegister*)&value_u32;

                result = CPU_MODEL_A64_GENERIC;

                switch (id_register.implementer)
                {
                    break; case A64_IMPLEMENTER_ARM:
                    {
                        switch (id_register.part_number)
                        {
                            break; case 0x926: result = CPU_MODEL_A64_ARM_ARM926EJ_S;
                            break; case 0xb02: result = CPU_MODEL_A64_ARM_MPCORE;
                            break; case 0xb36: result = CPU_MODEL_A64_ARM_ARM1136J_S;
                            break; case 0xb56: result = CPU_MODEL_A64_ARM_ARM1156T2_S;
                            break; case 0xb76: result = CPU_MODEL_A64_ARM_ARM1176JZ_S;
                            break; case 0xc05: result = CPU_MODEL_A64_ARM_CORTEX_A5;
                            break; case 0xc07: result = CPU_MODEL_A64_ARM_CORTEX_A7;
                            break; case 0xc08: result = CPU_MODEL_A64_ARM_CORTEX_A8;
                            break; case 0xc09: result = CPU_MODEL_A64_ARM_CORTEX_A9;
                            break; case 0xc0f: result = CPU_MODEL_A64_ARM_CORTEX_A15;
                            break; case 0xc0e: result = CPU_MODEL_A64_ARM_CORTEX_A17;
                            break; case 0xc20: result = CPU_MODEL_A64_ARM_CORTEX_M0;
                            break; case 0xc23: result = CPU_MODEL_A64_ARM_CORTEX_M3;
                            break; case 0xc24: result = CPU_MODEL_A64_ARM_CORTEX_M4;
                            break; case 0xc27: result = CPU_MODEL_A64_ARM_CORTEX_M7;
                            break; case 0xd20: result = CPU_MODEL_A64_ARM_CORTEX_M23;
                            break; case 0xd21: result = CPU_MODEL_A64_ARM_CORTEX_M33;
                            break; case 0xd24: result = CPU_MODEL_A64_ARM_CORTEX_M52;
                            break; case 0xd22: result = CPU_MODEL_A64_ARM_CORTEX_M55;
                            break; case 0xd23: result = CPU_MODEL_A64_ARM_CORTEX_M85;
                            break; case 0xc18: result = CPU_MODEL_A64_ARM_CORTEX_R8;
                            break; case 0xd13: result = CPU_MODEL_A64_ARM_CORTEX_R52;
                            break; case 0xd16: result = CPU_MODEL_A64_ARM_CORTEX_R52PLUS;
                            break; case 0xd15: result = CPU_MODEL_A64_ARM_CORTEX_R82;
                            break; case 0xd14: result = CPU_MODEL_A64_ARM_CORTEX_R82AE;
                            break; case 0xd02: result = CPU_MODEL_A64_ARM_CORTEX_A34;
                            break; case 0xd04: result = CPU_MODEL_A64_ARM_CORTEX_A35;
                            break; case 0xd8f: result = CPU_MODEL_A64_ARM_CORTEX_A320;
                            break; case 0xd03: result = CPU_MODEL_A64_ARM_CORTEX_A53;
                            break; case 0xd05: result = CPU_MODEL_A64_ARM_CORTEX_A55;
                            break; case 0xd46: result = CPU_MODEL_A64_ARM_CORTEX_A510;
                            break; case 0xd80: result = CPU_MODEL_A64_ARM_CORTEX_A520;
                            break; case 0xd88: result = CPU_MODEL_A64_ARM_CORTEX_A520AE;
                            break; case 0xd07: result = CPU_MODEL_A64_ARM_CORTEX_A57;
                            break; case 0xd06: result = CPU_MODEL_A64_ARM_CORTEX_A65;
                            break; case 0xd43: result = CPU_MODEL_A64_ARM_CORTEX_A65AE;
                            break; case 0xd08: result = CPU_MODEL_A64_ARM_CORTEX_A72;
                            break; case 0xd09: result = CPU_MODEL_A64_ARM_CORTEX_A73;
                            break; case 0xd0a: result = CPU_MODEL_A64_ARM_CORTEX_A75;
                            break; case 0xd0b: result = CPU_MODEL_A64_ARM_CORTEX_A76;
                            break; case 0xd0e: result = CPU_MODEL_A64_ARM_CORTEX_A76AE;
                            break; case 0xd0d: result = CPU_MODEL_A64_ARM_CORTEX_A77;
                            break; case 0xd41: result = CPU_MODEL_A64_ARM_CORTEX_A78;
                            break; case 0xd42: result = CPU_MODEL_A64_ARM_CORTEX_A78AE;
                            break; case 0xd4b: result = CPU_MODEL_A64_ARM_CORTEX_A78C;
                            break; case 0xd47: result = CPU_MODEL_A64_ARM_CORTEX_A710;
                            break; case 0xd4d: result = CPU_MODEL_A64_ARM_CORTEX_A715;
                            break; case 0xd81: result = CPU_MODEL_A64_ARM_CORTEX_A720;
                            break; case 0xd89: result = CPU_MODEL_A64_ARM_CORTEX_A720AE;
                            break; case 0xd87: result = CPU_MODEL_A64_ARM_CORTEX_A725;
                            break; case 0xd44: result = CPU_MODEL_A64_ARM_CORTEX_X1;
                            break; case 0xd4c: result = CPU_MODEL_A64_ARM_CORTEX_X1C;
                            break; case 0xd48: result = CPU_MODEL_A64_ARM_CORTEX_X2;
                            break; case 0xd4e: result = CPU_MODEL_A64_ARM_CORTEX_X3;
                            break; case 0xd82: result = CPU_MODEL_A64_ARM_CORTEX_X4;
                            break; case 0xd85: result = CPU_MODEL_A64_ARM_CORTEX_X925;
                            break; case 0xd4a: result = CPU_MODEL_A64_ARM_NEOVERSE_E1;
                            break; case 0xd0c: result = CPU_MODEL_A64_ARM_NEOVERSE_N1;
                            break; case 0xd49: result = CPU_MODEL_A64_ARM_NEOVERSE_N2;
                            break; case 0xd8e: result = CPU_MODEL_A64_ARM_NEOVERSE_N3;
                            break; case 0xd40: result = CPU_MODEL_A64_ARM_NEOVERSE_V1;
                            break; case 0xd4f: result = CPU_MODEL_A64_ARM_NEOVERSE_V2;
                            break; case 0xd84: result = CPU_MODEL_A64_ARM_NEOVERSE_V3;
                            break; case 0xd83: result = CPU_MODEL_A64_ARM_NEOVERSE_V3AE;
                        }
                    }
                    break; case A64_IMPLEMENTER_BROADCOM:
                    {
                    }
                    break; case A64_IMPLEMENTER_CAVIUM_MARVELL:
                    {
                    }
                    break; case A64_IMPLEMENTER_FUJITSU:
                    {
                    }
                    break; case A64_IMPLEMENTER_HI_SILICON:
                    {
                    }
                    break; case A64_IMPLEMENTER_NVIDIA:
                    {
                    }
                    break; case A64_IMPLEMENTER_QUALCOMM:
                    {
                    }
                    break; case A64_IMPLEMENTER_SAMSUNG:
                    {
                    }
                    break; case A64_IMPLEMENTER_APPLE:
                    {
                    }
                    break; case A64_IMPLEMENTER_ARM_CHINA:
                    {
                    }
                    break; case A64_IMPLEMENTER_MICROSOFT:
                    {
                    }
                    break; case A64_IMPLEMENTER_AMPERE:
                    {
                    }
                    break; case A64_IMPLEMENTER_UNKNOWN:
                    {
                    }
                    break; default: {}
                }
            }
        }
    }
    else
    {
        string8_print(S8("Error reading CPU model\n"));
    }
#elif defined(__APPLE__)
      u32 family;
      size_t family_length = sizeof(family);
      sysctlbyname("hw.cpufamily", &family, &family_length, 0, 0);

      switch (family)
      {
          break; case CPUFAMILY_UNKNOWN: result = CPU_MODEL_A64_GENERIC;
          break; case CPUFAMILY_ARM_9: result = CPU_MODEL_A64_ARM920T;
          break; case CPUFAMILY_ARM_11: result = CPU_MODEL_A64_ARM_ARM1136J_S;
          break; case CPUFAMILY_ARM_XSCALE: result = CPU_MODEL_A64_ARM_XSCALE;
          break; case CPUFAMILY_ARM_12: result = CPU_MODEL_A64_GENERIC;
          break; case CPUFAMILY_ARM_13: result = CPU_MODEL_A64_ARM_CORTEX_A8;
          break; case CPUFAMILY_ARM_14: result = CPU_MODEL_A64_ARM_CORTEX_A9;
          break; case CPUFAMILY_ARM_15: result = CPU_MODEL_A64_ARM_CORTEX_A7;
          break; case CPUFAMILY_ARM_SWIFT: result = CPU_MODEL_A64_ARM_SWIFT;
          break; case CPUFAMILY_ARM_CYCLONE: result = CPU_MODEL_A64_APPLE_A7;
          break; case CPUFAMILY_ARM_TYPHOON: result = CPU_MODEL_A64_APPLE_A8;
          break; case CPUFAMILY_ARM_TWISTER: result = CPU_MODEL_A64_APPLE_A9;
          break; case CPUFAMILY_ARM_HURRICANE: result = CPU_MODEL_A64_APPLE_A10;
          break; case CPUFAMILY_ARM_MONSOON_MISTRAL: result = CPU_MODEL_A64_APPLE_A11;
          break; case CPUFAMILY_ARM_VORTEX_TEMPEST: result = CPU_MODEL_A64_APPLE_A12;
          break; case CPUFAMILY_ARM_LIGHTNING_THUNDER: result = CPU_MODEL_A64_APPLE_A13;
          break; case CPUFAMILY_ARM_FIRESTORM_ICESTORM: result = CPU_MODEL_A64_APPLE_M1;
          break; case CPUFAMILY_ARM_BLIZZARD_AVALANCHE: result = CPU_MODEL_A64_APPLE_M2;
          break; case CPUFAMILY_ARM_EVEREST_SAWTOOTH: case CPUFAMILY_ARM_IBIZA: case CPUFAMILY_ARM_PALMA: case CPUFAMILY_ARM_LOBOS: result = CPU_MODEL_A64_APPLE_M3;
          break; case CPUFAMILY_ARM_COLL: result = CPU_MODEL_A64_APPLE_A17;
          break; case CPUFAMILY_ARM_DONAN: case CPUFAMILY_ARM_BRAVA: case CPUFAMILY_ARM_TAHITI: case CPUFAMILY_ARM_TUPAI: result = CPU_MODEL_A64_APPLE_M4;
          break; default: result = CPU_MODEL_A64_APPLE_M4;
      }

      if (family == 0 && result == CPU_MODEL_A64_GENERIC)
      {
          char8 buffer[4096];
          size_t buffer_length = BUSTER_ARRAY_LENGTH(buffer) - 1;
          if (sysctlbyname("machdep.cpu.brand_string", buffer, &buffer_length, 0, 0) == 0)
          {
              if (sysctlbyname("machdep.cpu.brand_string", 0, &buffer_length, 0, 0) == 0)
              {
                  let brand_string = S8(buffer);
                  if (string8_starts_with_sequence(brand_string, S8("Apple M1")))
                  {
                      result = CPU_MODEL_A64_APPLE_M1;
                  }
                  else if (string8_starts_with_sequence(brand_string, S8("Apple M2")))
                  {
                      result = CPU_MODEL_A64_APPLE_M2;
                  }
                  else if (string8_starts_with_sequence(brand_string, S8("Apple M3")))
                  {
                      result = CPU_MODEL_A64_APPLE_M3;
                  }
                  else if (string8_starts_with_sequence(brand_string, S8("Apple M4")))
                  {
                      result = CPU_MODEL_A64_APPLE_M4;
                  }
              }
          }
      }

#elif defined(_WIN32)
      result = CPU_MODEL_A64_GENERIC;
#endif

    return result;
}
