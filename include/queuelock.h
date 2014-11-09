// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

// An implementation of an MCS (Mellor-Crummey and Scott) spinlock

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
typedef uint64_t ptr_t;
#else
typedef uint32_t ptr_t;
#endif

typedef struct __queuenode
{
	volatile struct __queuenode* next;
	volatile int                 blocked;
} queuenode_t;

typedef struct
{
	ptr_t tail;
} queuelock_t;

static const queuelock_t QUEUELOCK_INITIALIZER = {.tail = 0};

static inline void ql_init(queuelock_t* q)
{
	*q = QUEUELOCK_INITIALIZER;
}

static inline void __ql_lock(queuelock_t* q, queuenode_t* self)
{
	queuenode_t* prev;

	/* prepare for enqueue */
	self->next = NULL;
	self->blocked = 1;

	/* enqueue at the end of the line */
	do
	{
		prev = (struct __queuenode*)((volatile ptr_t) q->tail);
	} while(!__sync_bool_compare_and_swap(&q->tail, (ptr_t)prev, (ptr_t)self));

	/* was there someone ahead of us? */
	if (prev)
	{
		/* tell the one ahead of us about ourselves */
		prev->next = self;

		/* wait until unlock */
		while(self->blocked)
		{
			__sync_pause();
		}
	}

	/* make sure nothing leaks out (upward) of the critical section */
	__sync_synchronize();
}

static inline void ql_lock(queuelock_t* q, queuenode_t* self)
{
	__ql_lock(q, self);
}

static inline void __ql_unlock(queuelock_t* q, queuenode_t* self)
{
	/* make sure nothing leaks out (downward) of the critical section */
	__sync_synchronize();

	/* try to dequeue from the end of the line */
	if (!self->next && !__sync_bool_compare_and_swap(&q->tail, (ptr_t)self, (ptr_t)0))
	{
		/* we really weren't at the end of the line! */

		/* wait until the one behind us tells us where they are */
		while(!self->next)
		{
			__sync_pause();
		}
	}

	/* tell the next one behind us that they may go */
	if(self->next)
	{
		/* unlock */
		self->next->blocked = 0;
	}
}

static inline void ql_unlock(queuelock_t* q, queuenode_t* self)
{
	__ql_unlock(q, self);
}

#ifndef PGM_PREEMPTIVE

static inline void ql_init_np(queuelock_t* q)
{
	ql_init(q);

#if defined(PGM_NP_INTERRUPTS)
	iopl(3);
#endif
}

static inline void ql_lock_np(queuelock_t* q, queuenode_t* self, unsigned long* flags)
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

	__ql_lock(q, self);
}

static inline void ql_unlock_np(queuelock_t* q, queuenode_t* self, unsigned long flags)
{
	__ql_unlock(q, self);

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

/*
   Carrying around the queuenode_t is a pain for simple non-nested
   critical sections. These varients of lock/unlock just puts the
   wait node in thread-local storage. At most one non-nest queue
   lock calll may be nested within other critical sections.
*/
static inline void ql_lock_no_nest(queuelock_t* q)
{
	extern __thread queuenode_t thread_qnode;
	ql_lock(q, &thread_qnode);
}

static inline void ql_unlock_no_nest(queuelock_t* q)
{
	extern __thread queuenode_t thread_qnode;
	ql_unlock(q, &thread_qnode);
}

#ifndef PGM_PREEMPTIVE
static inline void ql_lock_no_nest_np(queuelock_t* q, unsigned long* flags)
{
	extern __thread queuenode_t thread_qnode;
	ql_lock_np(q, &thread_qnode, flags);
}

static inline void ql_unlock_no_nest_np(queuelock_t* q, unsigned long flags)
{
	extern __thread queuenode_t thread_qnode;
	ql_unlock_np(q, &thread_qnode, flags);
}
#endif
