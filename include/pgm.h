#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 CONDITIONAL COMPILATION:    USE_OPTIMIZED_MSG_PASSING

 Use pthread_mutex and condition variables to signal between nodes when a node
 should have sufficient tokens to run. Token counters and synchronization
 variables are stored in shared memory.

 Note: This option is not required if USE_FIFOS is asserted. However, this may
 cause tasks to have to incrementally read tokens from FIFOs as they become
 available.
 */
#define USE_OPTIMIZED_MSG_PASSING

/*
 CONDITIONAL COMPILATION:    USE_FIFOS

 Transmit tokens via Unix FIFOs. Each byte represents one token.

 Note: This option is relatively useless while termination relies upon the
 TERMINATE token. Once the TERMINATE token is no longer needed, then the user
 should be able to transmit arbitrary bytes.

 TODO: Develop out-of-band method to signal termination that can work via
 both FIFOs and sockets. (The goal is to allow sockets for network-based PGM.
 FIFO support is a small step in this direction.) Of course, some sort of
 network discovery protocol will also be necessary to replace shared memory
 segments found in this current implementation.
 */
/*#define USE_FIFOS*/

/*
 USE_OPTIMIZED_MSG_PASSING and USE_FIFOS can be combined, but at least one
 must be asserted.
 */
#if !defined(USE_FIFOS) && !defined(USE_OPTIMIZED_MSG_PASSING)
#error "Cannot disable FIFOs without optimized message passing!"
#endif


#define PGM_GRAPH_NAME_LEN		80
#define PGM_EDGE_NAME_LEN		80
#define PGM_NODE_NAME_LEN		80
#define PGM_MAX_NODES			(1024*4)	/* per graph */
#define PGM_MAX_EDGES			(1024*4)	/* per graph */
#define PGM_MAX_GRAPHS			128			/* per process */

#define PGM_MAX_IN_DEGREE		32
#define PGM_MAX_OUT_DEGREE		PGM_MAX_IN_DEGREE

/* Special PGM Messages -- 1 byte */
const uint8_t TERMINATE = 0xff;
const uint8_t TOKEN		= 0x01;

typedef int graph_t;

typedef struct pgm_node_handle
{
	graph_t graph;
	int		node;
} node_t;

typedef struct pgm_edge_handle
{
	graph_t graph;
	int		edge;
} edge_t;


int pgm_init(const char* dir, int create = 0);

/* dir = directory in which to store fifos */
int pgm_init_graph(graph_t* graph, const char* graph_name);
int pgm_destroy_graph(graph_t graph);
int pgm_init_node(node_t* node, graph_t graph, const char* name);
int pgm_init_edge(edge_t* edge, node_t producer, node_t consumer,
				  const char* name,
				  int produce = 1, int consume = 1, int threshold = 1);

int pgm_find_graph(graph_t* graph, const char* graph_name);
int pgm_find_node(node_t* node, graph_t graph, const char* name);
int pgm_find_edge(edge_t* edge, node_t producer, node_t consumer,
				  const char* name);
/* returns the first edge between producer and consumer. */
int pgm_find_first_edge(edge_t* edge, node_t producer, node_t consumer);

/* returns buffer list of nodes. caller must free() buffer */
int pgm_find_successors(node_t n, node_t** successors, int* num);
int pgm_find_out_edges(node_t n, edge_t** edges, int* num);
int pgm_find_predecessors(node_t, node_t** predecessors, int* num);
int pgm_find_in_edges(node_t n, edge_t** edges, int* num);
/**/

int pgm_degree(node_t node);
int pgm_degree_in(node_t node);
int pgm_degree_out(node_t node);
const char* pgm_name(node_t node);
int pgm_nr_produce(edge_t edge);
int pgm_nr_consume(edge_t edge);
int pgm_nr_threshold(edge_t edge);
int pgm_is_dag(graph_t graph);

int pgm_claim_node(node_t node, pid_t tid = 0);
int pgm_release_node(node_t node, pid_t tid = 0);

int pgm_wait(node_t node);
int pgm_complete(node_t node);
int pgm_terminate(node_t node);

/*
 Convenience functions to allow number-based names instead of
 string-based names.
 */

static int pgm_init_graph(graph_t* graph, unsigned int numerical_name)
{
	char name[PGM_GRAPH_NAME_LEN];
	snprintf(name, PGM_GRAPH_NAME_LEN, "%x", numerical_name);
	return pgm_init_graph(graph, name);
}

static int pgm_find_graph(graph_t* graph, unsigned int numerical_name)
{
	char name[PGM_GRAPH_NAME_LEN];
	snprintf(name, PGM_GRAPH_NAME_LEN, "%x", numerical_name);
	return pgm_find_graph(graph, name);
}

static int pgm_init_node(node_t* node, graph_t graph,
						 unsigned int numerical_name)
{
	char name[PGM_NODE_NAME_LEN];
	snprintf(name, PGM_NODE_NAME_LEN, "%x", numerical_name);
	return pgm_init_node(node, graph, name);
}

static int pgm_find_node(node_t* node, graph_t graph,
						 unsigned int numerical_name)
{
	char name[PGM_NODE_NAME_LEN];
	snprintf(name, PGM_NODE_NAME_LEN, "%x", numerical_name);
	return pgm_find_node(node, graph, name);
}

static int pgm_init_edge(edge_t* edge, node_t producer, node_t consumer,
				unsigned int numerical_name,
				int produce = 1, int consume = 1, int threshold = 1)
{
	char name[PGM_EDGE_NAME_LEN];
	snprintf(name, PGM_EDGE_NAME_LEN, "%x", numerical_name);
	return pgm_init_edge(edge, producer, consumer, name, produce,
						 consume, threshold);
}

static int pgm_find_edge(edge_t* edge, node_t producer, node_t consumer,
						 unsigned int numerical_name)
{
	char name[PGM_EDGE_NAME_LEN];
	snprintf(name, PGM_EDGE_NAME_LEN, "%x", numerical_name);
	return pgm_find_edge(edge, producer, consumer, name);
}