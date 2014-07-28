// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

/* Program for testing all edge types (except for sock_stream). */

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

void* thread(void* _node)
{
	// All within the same process, so no need to call pgm_find_graph().

	char tabbuf[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

	node_t& node = *((node_t*)_node);
	int ret = 0;
	int iterations = 0;
	tabbuf[node.node] = '\0';

	CheckError(pgm_claim_node(node));

	bool is_src = (pgm_get_degree_in(node) == 0);

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
				if(iterations > 3)
					ret = PGM_TERMINATE;
			}

			if(ret != PGM_TERMINATE)
			{
				CheckError(ret);
				fprintf(stdout, "%s%d fires\n", tabbuf, node.node);

				if(is_src)
					usleep(1000*1000);

				CheckError(pgm_complete(node));
				iterations++;
			}
			else
			{
				fprintf(stdout, "%s- %d terminates\n", tabbuf, node.node);

				if(is_src)
				{
					CheckError(pgm_terminate(node));
				}
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
	node_t  n0, n1, n2, n3, n4, n5, n6;
	edge_t  e0_1, e0_2, e0_3, e0_4, e0_5;
	edge_t  e1_6, e2_6, e3_6, e4_6, e5_6;

	pthread_t t0, t1, t2, t3, t4, t5, t6;

	edge_attr_t cv_attr;
	edge_attr_t fast_fifo_attr, fast_mq_attr;
	edge_attr_t fifo_attr, mq_attr;

	memset(&cv_attr, 0, sizeof(cv_attr));

	cv_attr.nr_produce = 1;
	cv_attr.nr_consume = 1;
	cv_attr.nr_threshold = 1;

	fast_fifo_attr = fast_mq_attr = fifo_attr = mq_attr = cv_attr;

	cv_attr.type = pgm_cv_edge;
	fast_fifo_attr.type = pgm_fast_fifo_edge;
	fast_mq_attr.type = pgm_fast_mq_edge;
	fifo_attr.type = pgm_fifo_edge;
	mq_attr.type = pgm_mq_edge;


	fast_mq_attr.mq_maxmsg = 10; /* root required for higher values */
	mq_attr.mq_maxmsg = 10;

	CheckError(pgm_init("/tmp/graphs", 1));
	CheckError(pgm_init_graph(&g, "demo"));

	CheckError(pgm_init_node(&n0, g, "n0"));
	CheckError(pgm_init_node(&n1, g, "n1"));
	CheckError(pgm_init_node(&n2, g, "n2"));
	CheckError(pgm_init_node(&n3, g, "n3"));
	CheckError(pgm_init_node(&n4, g, "n4"));
	CheckError(pgm_init_node(&n5, g, "n5"));
	CheckError(pgm_init_node(&n6, g, "n6"));

	CheckError(pgm_init_edge(&e0_1, n0, n1, "e0_1", &cv_attr));
	CheckError(pgm_init_edge(&e0_2, n0, n2, "e0_2", &fifo_attr));
	CheckError(pgm_init_edge(&e0_3, n0, n3, "e0_3", &fast_mq_attr));
	CheckError(pgm_init_edge(&e0_4, n0, n4, "e0_4", &mq_attr));
	CheckError(pgm_init_edge(&e0_5, n0, n5, "e0_5", &fast_fifo_attr));

	CheckError(pgm_init_edge(&e1_6, n1, n6, "e1_6", &fast_mq_attr));
	CheckError(pgm_init_edge(&e2_6, n2, n6, "e2_6", &fifo_attr));
	CheckError(pgm_init_edge(&e3_6, n3, n6, "e3_6", &cv_attr));
	CheckError(pgm_init_edge(&e4_6, n4, n6, "e4_6", &mq_attr));
	CheckError(pgm_init_edge(&e5_6, n5, n6, "e5_6", &fast_fifo_attr));


	pthread_barrier_init(&init_barrier, 0, 7);
	pthread_create(&t0, 0, thread, &n0);
	pthread_create(&t1, 0, thread, &n1);
	pthread_create(&t2, 0, thread, &n2);
	pthread_create(&t3, 0, thread, &n3);
	pthread_create(&t4, 0, thread, &n4);
	pthread_create(&t5, 0, thread, &n5);
	pthread_create(&t6, 0, thread, &n6);

	pthread_join(t0, 0);
	pthread_join(t1, 0);
	pthread_join(t2, 0);
	pthread_join(t3, 0);
	pthread_join(t4, 0);
	pthread_join(t5, 0);
	pthread_join(t6, 0);

	CheckError(pgm_destroy_graph(g));

	CheckError(pgm_destroy());

	return 0;
}
