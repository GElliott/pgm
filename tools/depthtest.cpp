// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

/* A program for testing the shortest and longest path algorithms of PGM. */

#include <iostream>
#include <map>
#include <errno.h>
#include <string.h>
#include <pthread.h>

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

struct edge_compare
{
	bool operator()(const edge_t& a, const edge_t& b) const
	{
		return a.edge < b.edge;
	}
};
std::map<edge_t, double, edge_compare> weights;


double weight(edge_t e, void* user)
{
	return weights[e];
}

int main(void)
{
	graph_t g;
	node_t  n0, n1, n2, n3, n4;
	edge_t  e0_1, e0_2, e1_4, e2_3, e3_4;
	edge_t  be4_0, be3_1;

	CheckError(pgm_init_process_local());
	CheckError(pgm_init_graph(&g, "demo"));

	CheckError(pgm_init_node(&n0, g, "n0"));
	CheckError(pgm_init_node(&n1, g, "n1"));
	CheckError(pgm_init_node(&n2, g, "n2"));
	CheckError(pgm_init_node(&n3, g, "n3"));
	CheckError(pgm_init_node(&n4, g, "n4"));

	CheckError(pgm_init_edge(&e0_1, n0, n1, "e0_1"));
	CheckError(pgm_init_edge(&e0_2, n0, n2, "e0_2"));
	CheckError(pgm_init_edge(&e1_4, n1, n4, "e1_4"));
	CheckError(pgm_init_edge(&e2_3, n2, n3, "e2_3"));
	CheckError(pgm_init_edge(&e3_4, n3, n4, "e3_4"));

	CheckError(pgm_init_backedge(&be4_0, 1, n4, n0, "be4_0"));
	CheckError(pgm_init_backedge(&be3_1, 1, n3, n1, "be3_1"));

	printf("min depths:\n");
	printf("depth(n0): %d\n", (int)pgm_get_min_depth(n0));
	printf("depth(n1): %d\n", (int)pgm_get_min_depth(n1));
	printf("depth(n2): %d\n", (int)pgm_get_min_depth(n2));
	printf("depth(n3): %d\n", (int)pgm_get_min_depth(n3));
	printf("depth(n4): %d\n", (int)pgm_get_min_depth(n4));
	printf("max depths:\n");
	printf("depth(n0): %d\n", (int)pgm_get_max_depth(n0));
	printf("depth(n1): %d\n", (int)pgm_get_max_depth(n1));
	printf("depth(n2): %d\n", (int)pgm_get_max_depth(n2));
	printf("depth(n3): %d\n", (int)pgm_get_max_depth(n3));
	printf("depth(n4): %d\n", (int)pgm_get_max_depth(n4));

	weights[e0_1] = 3;
	weights[e0_2] = 5;
	weights[e1_4] = 1000;
	weights[e2_3] = 7;
	weights[e3_4] = 11;
	printf("min distances from source w/ edge weights:\n");
	printf("dist(n0): %d\n", (int)pgm_get_min_depth(n0, weight));
	printf("dist(n1): %d\n", (int)pgm_get_min_depth(n1, weight));
	printf("dist(n2): %d\n", (int)pgm_get_min_depth(n2, weight));
	printf("dist(n3): %d\n", (int)pgm_get_min_depth(n3, weight));
	printf("dist(n4): %d\n", (int)pgm_get_min_depth(n4, weight));
	printf("max distances from source w/ edge weights:\n");
	printf("dist(n0): %d\n", (int)pgm_get_max_depth(n0, weight));
	printf("dist(n1): %d\n", (int)pgm_get_max_depth(n1, weight));
	printf("dist(n2): %d\n", (int)pgm_get_max_depth(n2, weight));
	printf("dist(n3): %d\n", (int)pgm_get_max_depth(n3, weight));
	printf("dist(n4): %d\n", (int)pgm_get_max_depth(n4, weight));


	printf("inserting n00 as second source to n4\n");
	node_t n00;
	edge_t e00_4;
	CheckError(pgm_init_node(&n00, g, "n00"));
	CheckError(pgm_init_edge(&e00_4, n00, n4, "e00_4"));
	weights[e00_4] = 4;

	printf("depth(n0): %d\n", (int)pgm_get_min_depth(n0));
	printf("depth(n00): %d\n", (int)pgm_get_min_depth(n00));
	printf("depth(n1): %d\n", (int)pgm_get_min_depth(n1));
	printf("depth(n2): %d\n", (int)pgm_get_min_depth(n2));
	printf("depth(n3): %d\n", (int)pgm_get_min_depth(n3));
	printf("depth(n4): %d\n", (int)pgm_get_min_depth(n4));

	printf("min distances from source w/ edge weights:\n");
	printf("dist(n0): %d\n", (int)pgm_get_min_depth(n0, weight));
	printf("dist(n00): %d\n", (int)pgm_get_min_depth(n00, weight));
	printf("dist(n1): %d\n", (int)pgm_get_min_depth(n1, weight));
	printf("dist(n2): %d\n", (int)pgm_get_min_depth(n2, weight));
	printf("dist(n3): %d\n", (int)pgm_get_min_depth(n3, weight));
	printf("dist(n4): %d\n", (int)pgm_get_min_depth(n4, weight));
	printf("max distances from source w/ edge weights:\n");
	printf("dist(n0): %d\n", (int)pgm_get_max_depth(n0, weight));
	printf("dist(n00): %d\n", (int)pgm_get_max_depth(n00, weight));
	printf("dist(n1): %d\n", (int)pgm_get_max_depth(n1, weight));
	printf("dist(n2): %d\n", (int)pgm_get_max_depth(n2, weight));
	printf("dist(n3): %d\n", (int)pgm_get_max_depth(n3, weight));
	printf("dist(n4): %d\n", (int)pgm_get_max_depth(n4, weight));

	CheckError(pgm_destroy_graph(g));

	return 0;
}
