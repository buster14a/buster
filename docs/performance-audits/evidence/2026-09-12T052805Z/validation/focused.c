#include <stdio.h>
#include <buster/lib/compiler/codegen/register_allocator_quality_internal.h>
typedef struct { u64 test_count, succeeded_test_count; } UnitTestResult;
typedef struct { u64 unused; } UnitTestArguments;
#define BUSTER_TEST(arguments, check) do { (void)(arguments); result.test_count += 1; if (check) result.succeeded_test_count += 1; else fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #check); } while (0)
#define BUSTER_TEST_RAW(arguments, check, message) BUSTER_TEST(arguments, check)
BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_quality_traffic(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 starts[] = {0, 2, UINT32_MAX - 4095u, UINT32_MAX, UINT64_C(4294967296), UINT64_C(17592186036224)};
    u32 weights[] = {0, 1, 4096, 1, 4096, 4096};
    u64 expected[] = {0, 3, UINT64_C(4294967296), UINT64_C(4294967296), UINT64_C(4294971392), UINT64_C(17592186040320)};
    for (u32 probe = 0; probe < BUSTER_ARRAY_LENGTH(starts); probe += 1)
    {
        MachineQualityTraffic traffic = (MachineQualityTraffic)starts[probe];
        machine_quality_traffic_add(&traffic, weights[probe]);
        BUSTER_TEST_RAW(arguments, traffic == expected[probe], S8("QUALITY weighted sum"));
        BUSTER_TEST(arguments, (traffic >= 3) == (expected[probe] >= 3));
    }

    u64 heap_costs[] = {3, UINT64_C(4294967297), UINT32_MAX, UINT64_C(4294967296)};
    u32 heap_expected[] = {1, 3, 2, 0};
    for (u32 repeat = 0; repeat < 3; repeat += 1)
    {
        MachineQualityInterval heap[BUSTER_ARRAY_LENGTH(heap_costs)];
        u32 count = BUSTER_ARRAY_LENGTH(heap);
        for (u32 index = 0; index < count; index += 1)
        {
            heap[index] = (MachineQualityInterval){.virtual_register = index, .weight = (MachineQualityTraffic)heap_costs[index]};
        }
        for (u32 root = count / 2; root > 0; root -= 1)
        {
            machine_quality_heap_sift(heap, count, root - 1);
        }
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(heap_expected); index += 1)
        {
            BUSTER_TEST_RAW(arguments, heap[0].virtual_register == heap_expected[index], S8("QUALITY descending weighted heap"));
            count -= 1;
            heap[0] = heap[count];
            machine_quality_heap_sift(heap, count, 0);
        }
        // The existing strict-greater heap tie policy is deterministic but is
        // not a stable sort: replacing the root with the last item gives 0,2,1.
        MachineQualityInterval ties[] = {{.virtual_register = 0, .weight = 7},
                                         {.virtual_register = 1, .weight = 7},
                                         {.virtual_register = 2, .weight = 7}};
        u32 tie_expected[] = {0, 2, 1};
        count = BUSTER_ARRAY_LENGTH(ties);
        machine_quality_heap_sift(ties, count, 0);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(tie_expected); index += 1)
        {
            BUSTER_TEST(arguments, ties[0].virtual_register == tie_expected[index]);
            count -= 1;
            ties[0] = ties[count];
            machine_quality_heap_sift(ties, count, 0);
        }
    }
    machine_quality_heap_sift(0, 0, 0);

    u64 region_costs[] = {0, UINT32_MAX, UINT64_C(4294967296), UINT64_C(4294967296), 3, UINT64_C(17592186040320)};
    u32 region_expected[] = {5, 2, 3, 1, 4, UINT32_MAX};
    MachineQualityTraffic regions[BUSTER_ARRAY_LENGTH(region_costs)];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(regions); index += 1)
    {
        regions[index] = (MachineQualityTraffic)region_costs[index];
    }
    for (u32 repeat = 0; repeat < 3; repeat += 1)
    {
        u32 previous = UINT32_MAX;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(region_expected); index += 1)
        {
            previous = machine_quality_region_next(regions, BUSTER_ARRAY_LENGTH(regions), previous);
            BUSTER_TEST_RAW(arguments, previous == region_expected[index], S8("QUALITY descending region order"));
        }
    }
    MachineQualityTraffic zeros[] = {0, 0, 0};
    BUSTER_TEST(arguments, machine_quality_region_next(0, 0, UINT32_MAX) == UINT32_MAX);
    BUSTER_TEST(arguments, machine_quality_region_next(zeros, BUSTER_ARRAY_LENGTH(zeros), UINT32_MAX) == UINT32_MAX);
    return result;
}


int main(void)
{
    UnitTestArguments arguments = {0};
    UnitTestResult result = machine_test_quality_traffic(&arguments);
    printf("QUALITY shared-production boundaries: %llu/%llu passed\n", (unsigned long long)result.succeeded_test_count, (unsigned long long)result.test_count);
    return result.test_count != result.succeeded_test_count;
}
