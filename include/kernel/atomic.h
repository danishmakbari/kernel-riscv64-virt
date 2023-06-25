#ifndef KERNEL_ATOMIC_H
#define KERNEL_ATOMIC_H

#include <stdint.h>

static inline uint64_t atomic_test_and_set(volatile uint64_t *var, uint64_t new)
{
	uint64_t old;
	asm volatile("amoswap.d %0, %1, (%2)"
			: "=r" (old)
			: "r" (new), "r" (var));
	return old;
}

static inline void atomic_set(volatile uint64_t *var, uint64_t new)
{
	asm volatile("amoswap.d x0, %0, (%1)"
			:
			: "r" (new), "r" (var));
}

static inline uint64_t atomic_test(volatile uint64_t *var)
{
	uint64_t val;
	asm volatile("amoadd.d %0, x0, (%1)"
			: "=r" (val)
			: "r" (var));
	return val;
}

static inline void atomic_acquire_membar(void)
{
	asm volatile("fence ir, iorw");
}

static inline void atomic_release_membar(void)
{
	asm volatile("fence iorw, ow");
}

static inline void atomic_membar(void)
{
	asm volatile("fence iorw, iorw");
}

#endif

