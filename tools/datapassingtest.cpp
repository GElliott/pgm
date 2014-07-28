// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

/* A program for testing data-passing edges and buffer allocation routines. */

#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>

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

int TOTAL_ITERATIONS = 4*1000;

#define TEST_INCREMENTAL_PRODUCTION

typedef struct msg
{
	union
	{
		uint32_t val;
		struct
		{
			uint16_t top;
			uint16_t bottom;
		};
	};
} msg_t;

void* thread(void* _graph_t)
{
	// All within the same process, so no need to call pgm_find_graph().

	char tabbuf[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	int iterations = 0;
	int ret = 0;

	const graph_t& graph = *((graph_t*)_graph_t);
	node_t node;

	CheckError(pgm_claim_any_node(graph, &node));
	tabbuf[node.node] = '\0';

	int out_degree = pgm_get_degree_out(node);
	int in_degree = pgm_get_degree_in(node);
	edge_t* out_edges = (edge_t*)calloc(out_degree, sizeof(edge_t));
	edge_t* in_edges = (edge_t*)calloc(in_degree, sizeof(edge_t));
	msg_t** out_bufs = 0;
	msg_t** in_bufs = 0;

	CheckError(pgm_get_edges_out(node, out_edges, out_degree));
	if(out_degree)
	{
		out_bufs = (msg_t**)calloc(out_degree, sizeof(msg_t*));
		for(int i = 0; i < out_degree; ++i)
		{
			out_bufs[i] = (msg_t*)pgm_get_edge_buf_p(out_edges[i]);
		}
	}
	CheckError(pgm_get_edges_in(node, in_edges, in_degree));
	if(in_degree)
	{
		in_bufs = (msg_t**)calloc(in_degree, sizeof(msg_t*));
		for(int i = 0; i < in_degree; ++i)
		{
			in_bufs[i] = (msg_t*)pgm_get_edge_buf_c(in_edges[i]);
		}
	}

	bool is_src = (in_degree == 0);

	// an extra buffer for the sake of testing
	msg_t* extra_buf = 0;
	if(is_src)
	{
		extra_buf = (msg_t*)pgm_malloc_edge_buf_p(out_edges[0]);
		CheckError((extra_buf) ? 0 : -1);
	}

	pthread_barrier_wait(&init_barrier);

	if(!errors)
	{
		do {
			if(!is_src)
			{
				ret = pgm_wait(node);
			}
			else
			{
				if(iterations > TOTAL_ITERATIONS)
					ret = PGM_TERMINATE;
			}

			if(ret != PGM_TERMINATE)
			{
				CheckError(ret);

				if(is_src)
				{
					// (for testing) swap buffers
					extra_buf = (msg_t*)pgm_swap_edge_buf_p(out_edges[0], extra_buf);

#ifdef TEST_INCREMENTAL_PRODUCTION
					uint16_t halfval = (iterations % 2 != 0) ? 0xdead : 0xbeef;
					for(int i = 0; i < out_degree; ++i)
						if(out_bufs[i])
							*((uint16_t*)out_bufs[i]) = halfval;
#else
					uint32_t val = 0xdeadbeef;
					for(int i = 0; i < out_degree; ++i)
						if(out_bufs[i])
							out_bufs[i]->val = val;
#endif
					usleep(1*1000);
				}
				else
				{
					uint32_t val = 0;
					for(int i = 0; i < in_degree; ++i)
						if(in_bufs[i])
							val += in_bufs[i]->val;

					if(in_degree && out_degree)
					{
						// (for testing) swap the consumer and producer buffers
						CheckError(pgm_swap_edge_bufs(in_bufs[0], out_bufs[0]));
						msg_t* tmp = in_bufs[0];
						in_bufs[0] = out_bufs[0];
						out_bufs[0] = tmp;
					}

					for(int i = 0; i < out_degree; ++i)
						if(out_bufs[i])
							out_bufs[i]->val = val;
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

	if(extra_buf)
		pgm_free(extra_buf);

	CheckError(pgm_release_node(node));

	free(in_edges);
	free(out_edges);
	free(out_bufs);
	free(in_bufs);

	pthread_exit(0);
}

int main(void)
{
	graph_t g;
	node_t  n0, n1, n2;
	edge_t  e0_1, e1_2;

	pthread_t t0, t1, t2;

	edge_attr_t mq_attr, fifo_attr;
	memset(&mq_attr, 0, sizeof(mq_attr));

	mq_attr.nr_produce = sizeof(msg_t);
	mq_attr.nr_consume = sizeof(msg_t);
	mq_attr.nr_threshold = sizeof(msg_t);
	mq_attr.mq_maxmsg = 10;
	mq_attr.type = pgm_mq_edge;

	fifo_attr = mq_attr;
#ifdef TEST_INCREMENTAL_PRODUCTION
	fifo_attr.nr_produce /= 2;
#endif
	fifo_attr.type = pgm_fifo_edge;

	CheckError(pgm_init("/tmp/graphs", 1));
	CheckError(pgm_init_graph(&g, "demo"));

	CheckError(pgm_init_node(&n0, g, "n0"));
	CheckError(pgm_init_node(&n1, g, "n1"));
	CheckError(pgm_init_node(&n2, g, "n2"));

	CheckError(pgm_init_edge(&e0_1, n0, n1, "e0_1", &fifo_attr));
	CheckError(pgm_init_edge(&e1_2, n1, n2, "e1_2", &mq_attr));

	pthread_barrier_init(&init_barrier, 0, 3);
	pthread_create(&t0, 0, thread, &g);
	pthread_create(&t1, 0, thread, &g);
	pthread_create(&t2, 0, thread, &g);

	pthread_join(t0, 0);
	pthread_join(t1, 0);
	pthread_join(t2, 0);

	CheckError(pgm_destroy_graph(g));

	CheckError(pgm_destroy());

	return 0;
}
