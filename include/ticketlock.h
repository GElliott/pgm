#pragma once

#include <stdint.h>
#include <sys/io.h>

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

static inline void tl_init_w_irqcap(ticketlock_t *t)
{
	tl_init(t);
	iopl(3);
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

static inline void tl_lock_irqdisable(ticketlock_t* t)
{
	/* spinwait with interrupts disabled */
	asm volatile("cli": : :"memory");
	__tl_lock(t);
}

static inline void tl_unlock_irqenable(ticketlock_t* t)
{
	__tl_unlock(t);
	/* re-enable interrupts before we exit */
	asm volatile("sti": : :"memory");
}
