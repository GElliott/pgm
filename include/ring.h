// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

#pragma once

#include <stdlib.h>
#include <string.h>

#ifndef __cplusplus

#define nleading_unset_bits(x) \
__builtin_choose_expr((sizeof(x) == 8), \
	__builtin_clzl(x), \
	__builtin_clz(x))

#else

/*
  g++ doesn't have __builtin_choose_expr().
  Switch between types using meta programming...
*/
namespace __ring
{
	template<bool c, typename T, typename F>
	struct conditional { typedef T type; };
	template<typename T, typename F>
	struct conditional<false, T, F> { typedef F type; };

	struct clz32
	{
		template <class T>
		static inline int clz(T x)
		{
			return __builtin_clz(x);
		}
	};

	struct clz64
	{
		template <class T>
		static inline int clz(T x)
		{
			return __builtin_clzl(x);
		}
	};
}
#define nleading_unset_bits(x) \
	__ring::conditional<(sizeof(x) == 8), __ring::clz64, __ring::clz32>::type::clz(x)

#endif

typedef enum
{
	SLOT_FREE = 0,  /* must be zero */
	SLOT_READY
} slot_state_t;

struct ring
{
	size_t nmemb;
	size_t memb_sz;
	volatile size_t nfree;

	size_t widx;
	size_t ridx;

	char* slots;
	char* buf;

	char user_managed_buffers;
};

/*
   Initialize a ring buffer struct.
     [in] r: Pointer to ring buffer instance.
     [in] min_count: Minimum number of elements in ring buffer.
	      (value is rounded UP to nearest power of two)
     [in] size: size of ring buffer element.

   Return: 0 on success. -1 on error.
 */
static inline int init_ring(struct ring* r, size_t min_count, size_t size)
{
	size_t count;

	if (!r || min_count == 0 || size == 0)
		return -1;

	/* round min_count up to the nearest power of two */
	count = (min_count > 2) ?
		(~((~(size_t)0)>>1)) >> (nleading_unset_bits(min_count - 1) - 1) :
		min_count;

	/* overflow! too big! */
	if (count == 0)
		return -1;

	r->nmemb = count;
	r->memb_sz = size;
	r->nfree = count;
	r->widx = 0;
	r->ridx = 0;
	/* calloc() initializes slots to SLOT_FREE */
	r->slots = (char*)calloc(count, sizeof(char));
	r->buf = (char*)malloc(count*size);

	r->user_managed_buffers = 0;

	return 0;
}

/*
  Varient to allow user to specify their own (possibly static) buffers.
  Assumptions:
    1) slot_buf and data_buf have been allocated and are of sufficient size.
    2) 'count' is a power of two.
*/
static inline void __init_ring(struct ring* r, size_t count, size_t size,
	char* slot_buf, void* data_buf)
{
	r->nmemb = count;
	r->memb_sz = size;
	r->nfree = count;
	r->widx = 0;
	r->ridx = 0;
	r->slots = slot_buf;
	r->buf = (char*)data_buf;

	/* init slots to SLOT_FREE */
	memset(r->slots, 0, count);

	r->user_managed_buffers = 1;
}

/*
   Free ring buffer resources.
 */
static inline void free_ring(struct ring* r)
{
	if (!r)
		return;
	if (r->user_managed_buffers)
		return;

	free(r->slots);
	free(r->buf);
}

static inline int is_ring_empty(struct ring* r)
{
	return (r->nfree == r->nmemb);
}

static inline int is_ring_full(struct ring* r)
{
	return (r->nfree == 0);
}


/* multi-writer varients for adding elements to ring buf */

static inline void* __begin_mwrite_ring(struct ring* r)
{
	ssize_t nfree;
	size_t idx;
	void* dst;

	if(r->nfree <= 0)
		return NULL;

	nfree = __sync_fetch_and_sub(&r->nfree, 1);
	if (nfree <= 0)
	{
		__sync_fetch_and_add(&r->nfree, 1);
		return NULL;
	}

	/* roll over idx */
	idx = __sync_fetch_and_add(&r->widx, 1) % r->nmemb;
	dst = r->buf + idx*r->memb_sz;

	return (void*)dst;
}

static inline void __end_mwrite_ring(struct ring* r, void* addr)
{
	size_t idx = ((char*)addr - r->buf) / r->memb_sz;
	r->slots[idx] = SLOT_READY;
	__sync_synchronize(); /* memory barrier */
}


/* single-writer varients (optimized for single writer) */

static inline void* __begin_write_ring(struct ring* r)
{
	size_t idx;
	void* dst;

	if(r->nfree <= 0)
		return NULL;

	/* roll over idx */
	idx = r->widx++ % r->nmemb;
	dst = r->buf + idx*r->memb_sz;

	return (void*)dst;
}

static inline void __end_write_ring(struct ring* r, void* addr)
{
	size_t idx = ((char*)addr - r->buf) / r->memb_sz;
	r->slots[idx] = SLOT_READY;
	__sync_fetch_and_sub(&r->nfree, 1); /* memory barrier */
}


/* single-reader varients */

static inline void* __begin_read_ring(struct ring* r)
{
	size_t idx;

	if (r->nfree == r->nmemb)
		return NULL;

	idx = r->ridx % r->nmemb;
	if (*(volatile char*)&(r->slots[idx]) == SLOT_READY)
		return r->buf + idx * r->memb_sz;
	else
		return NULL;
}

static inline void __end_read_ring(struct ring* r, void* addr)
{
	size_t idx = ((char*)addr - r->buf) / r->memb_sz;
	r->slots[idx] = SLOT_FREE;
	r->ridx++;
	__sync_fetch_and_add(&r->nfree, 1); /* memory barrier */
}


#ifdef NDEBUG
#define check_size(r, ptr) \
	do { assert(sizeof(*ptr) == r->memb_sz); } while(0)
#define check_size_vec(r, sz) \
	do { assert(sz == r->memb_sz); } while(0)
#else
#define check_size(r, size)
#define check_size_vec(r, sz)
#endif

/*
   The following macros are used for enqueuing and dequeuing data from
   the provided ring buffer

   BEWARE!!!! Reads and writes are done by simple assignment (by the '='
   operator) for the sake of performance (vs using memcpy). Types are
   deduced from the second parameter to each macro. You must be consistent
   with your types. For example, mixing int32_t with int64_t will result
   in corrupted ring buffers.

   ----> Be very careful when passing literals!!! <----

   TODO: Make C++-template versions for safer typing.
*/

/*
   Macro for enqueuing an element to the ring buffer,
   with multi-writer support.
   [in] r: Pointer to struct ring
   [in] src: Value to write (not pointer to value!)
 */
#define mwrite_ring(r, src) \
do{ \
	struct ring* __r = (r); \
	typeof((src))* __dst; \
	check_size(__r, __dst); \
	do { __dst = (typeof(__dst)) __begin_mwrite_ring(__r); } \
		while (__dst == NULL); \
	*__dst = (src); \
	__end_mwrite_ring(__r, __dst); \
}while(0)

/*
   Macro for enqueuing an element to the ring buffer.
   [in] r: Pointer to struct ring
   [in] src: Value to write (not pointer to value!)
 */
#define write_ring(r, src) \
do{ \
	struct ring* __r = (r); \
	typeof((src))* __dst; \
	check_size(__r, __dst); \
	do { __dst = (typeof(__dst)) __begin_write_ring(__r); } \
		while (__dst == NULL); \
	*__dst = (src); \
	__end_write_ring(__r, __dst); \
}while(0)

/*
   Macro for dequeuing an element from the ring buffer.
   [in] r: Pointer to struct ring
   [in/out] dst_ptr: Pointer to where read element is to be written.
 */
#define read_ring(r, dst_ptr) \
do{ \
	struct ring* __r = (r); \
	typeof((dst_ptr)) __src; \
	check_size(__r, __src); \
	do { __src = (typeof(__src)) __begin_read_ring(__r); } \
		while (__src == NULL); \
	*(dst_ptr) = *__src; \
	__end_read_ring(__r, __src); \
}while(0)


/* Write/Read routines for plain vector/array types. These use memcpy. */

#define mwrite_vec_ring(r, src_vec, sz) \
do{ \
	struct ring* __r = (r); \
	void* __dst; \
	check_size_vec(__r, sz); \
	do { __dst = __begin_mwrite_ring(__r); } while (__dst == NULL); \
	memcpy(__dst, src_vec, sz); \
	__end_mwrite_ring(__r, __dst); \
}while(0)

#define write_vec_ring(r, src_vec, sz) \
do{ \
	struct ring* __r = (r); \
	void* __dst; \
	check_size_vec(__r, sz); \
	do { __dst = __begin_write_ring(__r); } while (__dst == NULL); \
	memcpy(__dst, src_vec, sz); \
	__end_write_ring(__r, __dst); \
}while(0)

#define read_vec_ring(r, dst_vec, sz) \
do{ \
	struct ring* __r = (r); \
	void* __src; \
	check_size_vec(__r, sz); \
	do { __src = __begin_read_ring(__r); } while (__src == NULL); \
	memcpy(dst_vec, __src, sz); \
	__end_read_ring(__r, __src); \
}while(0)
