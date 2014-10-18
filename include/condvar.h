// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

#pragma once

#include <sys/syscall.h>
#include <linux/futex.h>

#include "spinlock.h"

typedef int seq_t;

typedef enum
{
	CV_PRIVATE,
	CV_PUBLIC
} wait_mode_t;

typedef struct
{
	seq_t   seq;
	int32_t waiters;
	wait_mode_t  mode;
} cv_t;

static const cv_t CV_INITIALIZER = {0, 0, CV_PRIVATE};
static const cv_t CV_INITIALIZER_PUBLIC = {0, 0, CV_PUBLIC};

static inline void cv_init(cv_t* cv)
{
	*cv = CV_INITIALIZER;
}

static inline void cv_init_shared(cv_t* cv)
{
	*cv = CV_INITIALIZER_PUBLIC;
}

static inline int cv_wait(cv_t* cv, spinlock_t* l)
{
	int ret;
	int wait_mode = (cv->mode == CV_PRIVATE) ? FUTEX_WAIT_PRIVATE : FUTEX_WAIT;
	seq_t seq = cv->seq;

	++cv->waiters;
	spin_unlock(l); /* memory barrier */
	ret = syscall(SYS_futex, &cv->seq, wait_mode, seq, NULL, NULL, 0);
	spin_lock(l);
	--cv->waiters;

	return ret;
}

#ifndef PGM_PREEMPTIVE
static inline int cv_wait_np(cv_t* cv, spinlock_t* l, unsigned long *flags)
{
	int ret;
	int wait_mode = (cv->mode == CV_PRIVATE) ? FUTEX_WAIT_PRIVATE : FUTEX_WAIT;
	seq_t seq = cv->seq;

	++cv->waiters;
	spin_unlock_np(l, *flags); /* memory barrier */
	ret = syscall(SYS_futex, &cv->seq, wait_mode, seq, NULL, NULL, 0);
	spin_lock_np(l, flags);
	--cv->waiters;

	return ret;
}
#endif

static inline int cv_signal(cv_t* cv)
{
	int wake_mode = (cv->mode == CV_PRIVATE) ? FUTEX_WAKE_PRIVATE : FUTEX_WAKE;
	int ret = 0;
	if(cv->waiters > 0) {
		__sync_fetch_and_add(&cv->seq, 1);
		/* wake one waiter */
		ret = syscall(SYS_futex, &cv->seq, wake_mode, 1, NULL, NULL, 0);
	}
	return ret;
}
