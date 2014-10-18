// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

#pragma once

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

