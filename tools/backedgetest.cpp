// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

/* A program for testing the basic condition-variable-based edge. */

#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "pgm.h"

int errors = 0;
pthread_barrier_t init_barrier;

__thread char __errstr[80] = {0};

#define CheckError(e) \
do { int __ret = (e); \
if(__ret < 0) { \
	errors++; \
	char* errstr = strerror_r(errno, __errstr, sizeof(errstr)); \
	fprintf(stderr, "%lu: Error %d (%s (%d)) @ %s:%s:%d\n",  \
		pthread_self(), __ret, errstr, errno, __FILE__, __FUNCTION__, __LINE__); \
}}while(0)

int TOTAL_ITERATIONS = 10;

void* thread(void* _graph_t)
{
	char tabbuf[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	int iterations = 0;
	int ret = 0;

	const graph_t& graph = *((graph_t*)_graph_t);
	node_t node;

	CheckError(pgm_claim_any_node(graph, &node));
	tabbuf[node.node] = '\0';

	int in_degree = pgm_get_degree_in(node);
	bool is_src = (in_degree == 0);

	edge_t backedge;
	int nr_skips = 0;
	int skipped = 0;
	uint32_t* buf;

	if(is_src)
	{
		ret = pgm_get_edges_in(node, &backedge, 1, 0);
		nr_skips = pgn_get_nr_skips_remaining(backedge);
		assert(nr_skips > 0);

		buf = (uint32_t*)pgm_get_edge_buf_c(backedge);
	}
	else
	{
		ret = pgm_get_edges_out(node, &backedge, 1, 0);

		buf = (uint32_t*)pgm_get_edge_buf_p(backedge);
	}
	assert(ret == 1);


	pthread_barrier_wait(&init_barrier);

	if(!errors)
	{
		do {
			ret = pgm_wait(node);
			if(is_src)
			{
				++skipped;
				if(iterations > TOTAL_ITERATIONS)
					ret = PGM_TERMINATE;
			}

			if(ret != PGM_TERMINATE)
			{
				CheckError(ret);

				fprintf(stdout, "%s%d fires\n", tabbuf, node.node);

				if(is_src)
				{
					if(skipped > nr_skips)
					{
						fprintf(stdout, "%s%d read %u from backedge.\n", tabbuf, node.node, *buf);
					}
					usleep(500*1000);
				}
				else
				{
					*buf = (uint32_t)iterations;
				}

				CheckError(pgm_complete(node));
				iterations++;
			}
			else
			{
				fprintf(stdout, "%s%d terminates\n", tabbuf, node.node);

				if(is_src)
					CheckError(pgm_terminate(node));
			}

		} while(ret != PGM_TERMINATE);
	}

	pthread_barrier_wait(&init_barrier);

	CheckError(pgm_release_node(node));

	pthread_exit(0);
}

int main(void)
{
	graph_t g;
	node_t  n0, n1;
	edge_t  e0_1;
	edge_t  be1_0;

	pthread_t t0, t1;

	edge_attr_t backedge_attr;
	memset(&backedge_attr, 0, sizeof(backedge_attr));
	backedge_attr.nr_produce = sizeof(uint32_t);
	backedge_attr.nr_consume = sizeof(uint32_t);
	backedge_attr.nr_threshold = sizeof(uint32_t);
	backedge_attr.type = pgm_fifo_edge;

	CheckError(pgm_init("/tmp/graphs", 1));
	CheckError(pgm_init_graph(&g, "demo"));

	CheckError(pgm_init_node(&n0, g, "n0"));
	CheckError(pgm_init_node(&n1, g, "n1"));

	CheckError(pgm_init_edge(&e0_1, n0, n1, "e0_1")); // default to cv edge
	CheckError(pgm_init_backedge(&be1_0, TOTAL_ITERATIONS/2, n1, n0, "be1_0", &backedge_attr));

	pthread_barrier_init(&init_barrier, 0, 1);
	pthread_create(&t0, 0, thread, &g);
	pthread_create(&t1, 0, thread, &g);

	pthread_join(t0, 0);
	pthread_join(t1, 0);

	CheckError(pgm_destroy_graph(g));

	CheckError(pgm_destroy());

	return 0;
}
