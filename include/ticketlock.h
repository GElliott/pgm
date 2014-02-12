#pragma once

#include <stdint.h>

#if defined(PGM_NP_INTERRUPTS)
#include <sys/io.h>
#endif

#if defined(PGM_NP_LITMUS)
#include "litmus.h"
#endif

#if ULONG_MAX == 0xffffffffffffffffUL
typedef unsigned long long ticketdata_t;
typedef uint32_t ticketid_t;
static const ticketdata_t TL_INC = 0x100000000;
#else
typedef unsigned long ticketdata_t;
typedef uint16_t ticketid_t;
static const ticketdata_t TL_INC = 0x10000;
#endif

typedef union
{
	ticketdata_t data;
	struct
	{
		ticketid_t owner;
		ticketid_t next;
	} __attribute__((packed));
} ticketlock_t;

static const ticketlock_t TICKETLOCK_INITIALIZER = {0};

static inline void tl_init(ticketlock_t* t)
{
	*t = TICKETLOCK_INITIALIZER;
}

static inline void __tl_lock(ticketlock_t* t)
{
	ticketlock_t updated = { __sync_fetch_and_add(&t->data, TL_INC) };
	if(updated.next != updated.owner)
		while(updated.next != t->owner)
			asm volatile("pause": : :"memory");
	asm volatile("mfence": : :"memory");
}

static inline void tl_lock(ticketlock_t* t)
{
	__tl_lock(t);
}

static inline void __tl_unlock(ticketlock_t* t)
{
	asm volatile("mfence": : :"memory");
	__sync_fetch_and_add(&t->owner, (ticketid_t)1);
}

static inline void tl_unlock(ticketlock_t* t)
{
	__tl_unlock(t);
}

#ifndef PGM_PREEMPTIVE
static inline void tl_init_np(ticketlock_t *t)
{
	tl_init(t);

#if defined(PGM_NP_INTERRUPTS)
	iopl(3);
#endif
}

static inline unsigned long save_flags(void)
{
	unsigned long flags;
	asm volatile("pushf ; pop %0" : "=rm" (flags) : :"memory");
	return flags;
}

static inline void restore_flags(unsigned long flags)
{
	asm volatile("push %0 ; popf" : : "g" (flags) : "memory", "cc");
}

static inline void tl_lock_np(ticketlock_t* t, unsigned long* flags)
{
#if defined(PGM_NP_INTERRUPTS)
	/* save flags and disable interrupts */
	*flags = save_flags();
	asm volatile("cli": : :"memory");
#elif defined(PGM_NP_LITMUS)
	/* start non-preemption */
	asm volatile("mfence": : :"memory");
	enter_np();
	asm volatile("mfence": : :"memory");
#endif

	__tl_lock(t);
}

static inline void tl_unlock_np(ticketlock_t* t, unsigned long flags)
{
	__tl_unlock(t);

#if defined(PGM_NP_INTERRUPTS)
	/* re-enable flags before we exit */
	restore_flags(flags);
#elif defined(PGM_NP_LITMUS)
	/* end non-preemption */
	asm volatile("mfence": : :"memory");
	exit_np();
	asm volatile("mfence": : :"memory");
#endif
}
#endif
