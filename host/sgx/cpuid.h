// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#ifndef _OE_CPUIDCOUNT_H
#define _OE_CPUIDCOUNT_H

#if defined(__GNUC__)
#include <cpuid.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#include <limits.h>
#else
#error "oe_get_cpuid(): no cpuid intrinsic mapping for this compiler"
#endif

#define CPUID_EXTENDED_FEATURE_FLAGS_LEAF 0x07
#define CPUID_EXTENDED_FEATURE_FLAGS_SGX_FLC_MASK 0x40000000

#define CPUID_SGX_LEAF 0x12
#define CPUID_SGX_KSS_MASK 0x80
#define CPUID_SGX_MISC_EXINFO_MASK 0x01

/* Same as __get_cpuid, but sub-leaf can be specified.
   Need this function as cpuid level 4 needs the sub-leaf to be specified in ECX
*/
static inline void oe_get_cpuid(
    unsigned int __leaf,
    unsigned int __subleaf,
    unsigned int* __eax,
    unsigned int* __ebx,
    unsigned int* __ecx,
    unsigned int* __edx)
{
#if defined(__GNUC__)
    __cpuid_count(__leaf, __subleaf, *__eax, *__ebx, *__ecx, *__edx);
#elif defined(_MSC_VER)
    int registers[4] = {0};

    __cpuidex(registers, (int)__leaf, (int)__subleaf);

    *__eax = (unsigned int)registers[0];
    *__ebx = (unsigned int)registers[1];
    *__ecx = (unsigned int)registers[2];
    *__edx = (unsigned int)registers[3];
#endif
}
#endif /* _OE_CPUIDCOUNT_H */
