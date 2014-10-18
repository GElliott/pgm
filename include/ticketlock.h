// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

#pragma once

#include <stdint.h>

#if defined(PGM_NP_INTERRUPTS)
#include <sys/io.h>
#endif

#if defined(PGM_NP_LITMUS)
#include "litmus.h"
#endif

#include "atomic.h"
#include "interrupts.h"

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
			__sync_pause();
	__sync_synchronize();
}

static inline void tl_lock(ticketlock_t* t)
{
	__tl_lock(t);
}

static inline void __tl_unlock(ticketlock_t* t)
{
	__sync_synchronize();
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

static inline void tl_lock_np(ticketlock_t* t, unsigned long* flags)
{
#if defined(PGM_NP_INTERRUPTS)
	/* save flags and disable interrupts */
	*flags = save_flags();
	asm volatile("cli": : :"memory");
#elif defined(PGM_NP_LITMUS)
	/* start non-preemption */
	__sync_synchronize();
	enter_np();
	__sync_synchronize();
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
	__sync_synchronize();
	exit_np();
	__sync_synchronize();
#endif
}
#endif
