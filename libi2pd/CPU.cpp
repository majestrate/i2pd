#include "CPU.h"
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#include "Log.h"

#ifndef bit_AES
#define bit_AES (1 << 25)
#endif
#ifndef bit_AVX
#define bit_AVX (1 << 28)
#endif
#ifndef bit_AVX2
#define bit_AVX2 (1 << 5)
#endif


namespace i2p
{
namespace cpu
{
	bool aesni = false;
	bool avx = false;
	bool avx2 = false;

	static void run_cpuid(uint32_t eax, uint32_t ecx, uint32_t * abcd)
	{
#if defined(_MSC_VER)
    __cpuidex(abcd, eax, ecx);
#else
    uint32_t ebx = 0;
	uint32_t edx = 0;
#if defined( __i386__ ) && defined ( __PIC__ )
     /* in case of PIC under 32-bit EBX cannot be clobbered */
    __asm__ ( "movl %%ebx, %%edi \n\t cpuid \n\t xchgl %%ebx, %%edi" : "=D" (ebx),
#else
    __asm__ ( "cpuid" : "+b" (ebx),
#endif
              "+a" (eax), "+c" (ecx), "=d" (edx) );
    abcd[0] = eax; abcd[1] = ebx; abcd[2] = ecx; abcd[3] = edx;
#endif
	}

	void Detect()
	{
#if defined(__x86_64__) || defined(__i386__)
		int info[4];
		uint32_t abcd[4];
		__cpuid(0, info[0], info[1], info[2], info[3]);
		if (info[0] >= 0x00000001) {
			__cpuid(0x00000001, info[0], info[1], info[2], info[3]);
			aesni = info[2] & bit_AES;  // AESNI
			avx = info[2] & bit_AVX;  // AVX
			run_cpuid(7, 0, abcd);
			avx2 = abcd[1] & bit_AVX2;
		}
#endif
		if(aesni)
		{
			LogPrint(eLogInfo, "AESNI enabled");
		}
		if(avx)
		{
			LogPrint(eLogInfo, "AVX enabled");
		}
		if(avx2)
		{
			LogPrint(eLogInfo, "AVX2 enabled");
		}
	}
}
}
