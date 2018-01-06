#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__

#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

#if __LINUX_ARM_ARCH__ >= 7 ||		\
	(__LINUX_ARM_ARCH__ == 6 && defined(CONFIG_CPU_32v6K))
#define sev()	__asm__ __volatile__ ("sev" : : : "memory")
#define wfe()	__asm__ __volatile__ ("wfe" : : : "memory")
#define wfi()	__asm__ __volatile__ ("wfi" : : : "memory")
#endif

#if __LINUX_ARM_ARCH__ >= 7
#define isb(option) __asm__ __volatile__ ("isb " #option : : : "memory")
#define dsb(option) __asm__ __volatile__ ("dsb " #option : : : "memory")
#define dmb(option) __asm__ __volatile__ ("dmb " #option : : : "memory")
#elif defined(CONFIG_CPU_XSC3) || __LINUX_ARM_ARCH__ == 6
#define isb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" \
				    : : "r" (0) : "memory")
#elif defined(CONFIG_CPU_FA526)
#define isb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb(x) __asm__ __volatile__ ("" : : : "memory")
#else
#define isb(x) __asm__ __volatile__ ("" : : : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb(x) __asm__ __volatile__ ("" : : : "memory")
#endif

#ifdef CONFIG_THUMB2_KERNEL
#define __load_no_speculate_n(ptr, lo, hi, failval, cmpptr, sz)	\
({								\
	typeof(*ptr) __nln_val;					\
	typeof(*ptr) __failval =				\
		(typeof(*ptr))(unsigned long)(failval);		\
								\
	asm volatile (						\
	"	cmp	%[c], %[l]\n"				\
	"	it	hs\n"					\
	"	cmphs	%[h], %[c]\n"				\
	"	blo	1f\n"					\
	"	ld" #sz " %[v], %[p]\n"				\
	"1:	it	lo\n"					\
	"	movlo	%[v], %[f]\n"				\
	"	.inst 0xf3af8014 @ CSDB\n"			\
	: [v] "=&r" (__nln_val)					\
	: [p] "m" (*(ptr)), [l] "r" (lo), [h] "r" (hi),		\
	  [f] "r" (__failval), [c] "r" (cmpptr)			\
	: "cc");						\
								\
	__nln_val;						\
})
#else
#define __load_no_speculate_n(ptr, lo, hi, failval, cmpptr, sz)	\
({								\
	typeof(*ptr) __nln_val;					\
	typeof(*ptr) __failval =				\
		(typeof(*ptr))(unsigned long)(failval);		\
								\
	asm volatile (						\
	"	cmp	%[c], %[l]\n"				\
	"	cmphs	%[h], %[c]\n"				\
	"	ldr" #sz "hi %[v], %[p]\n"			\
	"	movls	%[v], %[f]\n"				\
	"	.inst 0xe320f014 @ CSDB\n"			\
	: [v] "=&r" (__nln_val)					\
	: [p] "m" (*(ptr)), [l] "r" (lo), [h] "r" (hi),		\
	  [f] "r" (__failval), [c] "r" (cmpptr)			\
	: "cc");						\
								\
	__nln_val;						\
})
#endif

#define __load_no_speculate(ptr, lo, hi, failval, cmpptr)		\
({									\
	typeof(*(ptr)) __nl_val;					\
									\
	switch (sizeof(__nl_val)) {					\
	case 1:								\
		__nl_val = __load_no_speculate_n(ptr, lo, hi, failval,	\
						 cmpptr, b);		\
		break;							\
	case 2:								\
		__nl_val = __load_no_speculate_n(ptr, lo, hi, failval,	\
						 cmpptr, h);		\
		break;							\
	case 4:								\
		__nl_val = __load_no_speculate_n(ptr, lo, hi, failval,	\
						 cmpptr, );		\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
									\
	__nl_val;							\
})

#define nospec_ptr(ptr, lo, hi)						\
({									\
	typeof(ptr) __np_ptr = (ptr);					\
	__load_no_speculate(&__np_ptr, lo, hi, 0, __np_ptr);		\
})

#ifdef CONFIG_ARM_HEAVY_MB
extern void (*soc_mb)(void);
extern void arm_heavy_mb(void);
#define __arm_heavy_mb(x...) do { dsb(x); arm_heavy_mb(); } while (0)
#else
#define __arm_heavy_mb(x...) dsb(x)
#endif

#if defined(CONFIG_ARM_DMA_MEM_BUFFERABLE) || defined(CONFIG_SMP)
#define mb()		__arm_heavy_mb()
#define rmb()		dsb()
#define wmb()		__arm_heavy_mb(st)
#define dma_rmb()	dmb(osh)
#define dma_wmb()	dmb(oshst)
#else
#define mb()		barrier()
#define rmb()		barrier()
#define wmb()		barrier()
#define dma_rmb()	barrier()
#define dma_wmb()	barrier()
#endif

#define __smp_mb()	dmb(ish)
#define __smp_rmb()	__smp_mb()
#define __smp_wmb()	dmb(ishst)

#include <asm-generic/barrier.h>

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_BARRIER_H */
