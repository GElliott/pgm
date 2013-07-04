#include <iostream>

#include <pthread.h>

#include "pgm.h"

#define CheckError(e) \
if(e != 0) { \
	fprintf(stderr, "Error %d @ %s:%s:%d\n",  \
		e, __FILE__, __FUNCTION__, __LINE__); \
}

void* thread(void* _node)
{
	// All within the same process, so no need to call pgm_find_graph().
	
	node_t& node = *((node_t*)_node);
	int ret = 0;
	
	CheckError(pgm_claim_node(node));
	
	do {
		ret = pgm_wait(node);

		if(ret != TERMINATE)
		{
			CheckError(ret);
//			fprintf(stdout, "%d fires\n", node.node);
			CheckError(pgm_complete(node));
		}
		else
		{
			fprintf(stdout, "- %d terminates\n", node.node);
		}
	} while(ret != TERMINATE);
	
	CheckError(pgm_release_node(node));
	
	pthread_exit(0);
}

int main(void)
{
	graph_t g;
	node_t  n0, n1, n2, n3;
	edge_t  e0_1, e0_2, e1_3, e2_3;
	
	pthread_t t1, t2, t3;
	
	CheckError(pgm_init("/tmp/graphs", 1));
	CheckError(pgm_init_graph(&g, "demo"));
	
	CheckError(pgm_init_node(&n0, g, "n0"));
	CheckError(pgm_init_node(&n1, g, "n1"));
	CheckError(pgm_init_node(&n2, g, "n2"));
	CheckError(pgm_init_node(&n3, g, "n3"));
	
	CheckError(pgm_init_edge(&e0_1, n0, n1, "e0_1"));
	CheckError(pgm_init_edge(&e0_2, n0, n2, "e0_2"));
	CheckError(pgm_init_edge(&e1_3, n1, n3, "e1_3"));
	CheckError(pgm_init_edge(&e2_3, n2, n3, "e2_3"));
	
	pthread_create(&t1, 0, thread, &n1);
	pthread_create(&t2, 0, thread, &n2);
	pthread_create(&t3, 0, thread, &n3);
	
	CheckError(pgm_claim_node(n0));

	for(int i = 0; i < 100; ++i)
	{
//		fprintf(stdout, "*** %d fires\n", n0.node);
		CheckError(pgm_complete(n0));
		
//		usleep(100*1000); // 10th of a second
	}
	
	
	CheckError(pgm_terminate(n0));
	
	pthread_join(t1, 0);
	pthread_join(t2, 0);
	pthread_join(t3, 0);
	
	CheckError(pgm_release_node(n0));

	CheckError(pgm_destroy_graph(g));
	
	return 0;
}
