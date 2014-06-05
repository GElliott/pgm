// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

#pragma once

static inline void __sync_pause()
{
#if (ARCH == x86_64) || (ARCH == i386) || (ARCH == i686)
	asm volatile("pause": : :"memory");
#else
	__sync_synchronize();
#endif
}
