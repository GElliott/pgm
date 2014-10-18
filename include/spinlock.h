// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

#pragma once

#include "config.h"

#if PGM_SYNC_METHOD == 1

#if PGM_SPINLOCK_TYPE == 0 /* ticket lock */
#include "ticketlock.h"
typedef ticketlock_t spinlock_t;
static const spinlock_t PGM_SPINLOCK_INITIALIZER = TICKETLOCK_INITIALIZER;

static inline void spin_init(spinlock_t* l)
{
	tl_init(l);
}

static inline void spin_lock(spinlock_t* l)
{
	tl_lock(l);
}

static inline void spin_unlock(spinlock_t* l)
{
	tl_unlock(l);
}

#ifndef PGM_PREEMPTIVE
static inline void spin_init_np(spinlock_t* l)
{
	tl_init_np(l);
}

static inline void spin_lock_np(spinlock_t* l, long unsigned int* flags)
{
	tl_lock_np(l, flags);
}

static inline void spin_unlock_np(spinlock_t* l, long unsigned int flags)
{
	tl_unlock_np(l, flags);
}
#endif /* end !PGM_PREEMPTIVE */

#elif PGM_SPINLOCK_TYPE == 1 /* queue lock */

#include "queuelock.h"
typedef queuelock_t spinlock_t;
static const spinlock_t PGM_SPINLOCK_INITIALIZER = QUEUELOCK_INITIALIZER;

static inline void spin_init(spinlock_t* l)
{
	ql_init(l);
}

static inline void spin_lock(spinlock_t* l)
{
	ql_lock_no_nest(l);
}

static inline void spin_unlock(spinlock_t* l)
{
	ql_unlock_no_nest(l);
}

#ifndef PGM_PREEMPTIVE
static inline void spin_init_np(spinlock_t* l)
{
	ql_init_np(l);
}

static inline void spin_lock_np(spinlock_t* l, long unsigned int* flags)
{
	ql_lock_no_nest_np(l, flags);
}

static inline void spin_unlock_np(spinlock_t* l, long unsigned int flags)
{
	ql_unlock_no_nest_np(l, flags);
}
#endif /* end !PGM_PREEMPTIVE */

#else
#error "Unknown spinlock configuration!"
#endif

#endif /* end PGM_SYNC_METHOD == 1 */
