// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

/* Program for testing all edge types (except for sock_stream). */

#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "pgm.h"

int errors = 0;
__thread char __errstr[80] = {0};

#define CheckError(e) \
do { int __ret = (e); \
if(__ret < 0) { \
	errors++; \
	char* errstr = strerror_r(errno, __errstr, sizeof(errstr)); \
	fprintf(stderr, "%lu: Error %d (%s (%d)) @ %s:%s:%d\n",  \
		pthread_self(), __ret, errstr, errno, __FILE__, __FUNCTION__, __LINE__); \
}}while(0)

#define CheckReturn(statement, expected) \
do { \
int __ret = (statement); \
int __expected = (expected); \
if(__ret != __expected) { \
	errors++; \
	fprintf(stderr, "%lu: %s returned %d, expected %d @ %s:%s:%d\n",  \
		pthread_self(), #statement, __ret, __expected, __FILE__, __FUNCTION__, __LINE__); \
}}while(0)

int main(void)
{
	graph_t g;
	node_t  n0, n1, n2, n3, n4, n5;
	edge_t  e0_1, e0_3, e1_2, e3_4, e2_5, e4_5;
	edge_t  be5_0;

	pgm_init_process_local();
	pgm_init_graph(&g, "predtest");
	CheckError(pgm_init_node(&n0, g, 0u));
	CheckError(pgm_init_node(&n1, g, 1u));
	CheckError(pgm_init_node(&n2, g, 2u));
	CheckError(pgm_init_node(&n3, g, 3u));
	CheckError(pgm_init_node(&n4, g, 4u));
	CheckError(pgm_init_node(&n5, g, 5u));

	CheckError(pgm_init_edge(&e0_1, n0, n1, "e0_1"));
	CheckError(pgm_init_edge(&e0_3, n0, n3, "e0_3"));
	CheckError(pgm_init_edge(&e1_2, n1, n2, "e1_2"));
	CheckError(pgm_init_edge(&e3_4, n3, n4, "e3_4"));
	CheckError(pgm_init_edge(&e2_5, n2, n5, "e2_5"));
	CheckError(pgm_init_edge(&e4_5, n4, n5, "e4_5"));
	CheckError(pgm_init_backedge(&be5_0, 1, n5, n0, "be5_0"));

	// check self case
	CheckReturn(pgm_is_ancestor(n0, n0), 0);

	// all ancestors of the root?
	CheckReturn(pgm_is_ancestor(n1, n0), 1);
	CheckReturn(pgm_is_ancestor(n2, n0), 1);
	CheckReturn(pgm_is_ancestor(n3, n0), 1);
	CheckReturn(pgm_is_ancestor(n4, n0), 1);
	CheckReturn(pgm_is_ancestor(n5, n0), 1);

	// sink decended from all other nodes?
	CheckReturn(pgm_is_descendant(n0, n5), 1);
	CheckReturn(pgm_is_descendant(n1, n5), 1);
	CheckReturn(pgm_is_descendant(n2, n5), 1);
	CheckReturn(pgm_is_descendant(n3, n5), 1);
	CheckReturn(pgm_is_descendant(n4, n5), 1);


	// n2 and n3 are on different branches of the diamond.
	// These should all evaluate to false.
	CheckReturn(pgm_is_ancestor(n2, n3), 0);
	CheckReturn(pgm_is_ancestor(n3, n2), 0);
	CheckReturn(pgm_is_descendant(n2, n3), 0);
	CheckReturn(pgm_is_descendant(n3, n2), 0);

	// n1 and n4 are on different branches of the diamond.
	// These should all evaluate to false.
	CheckReturn(pgm_is_ancestor(n1, n4), 0);
	CheckReturn(pgm_is_ancestor(n4, n1), 0);
	CheckReturn(pgm_is_descendant(n1, n4), 0);
	CheckReturn(pgm_is_descendant(n4, n1), 0);

	return 0;
}


