// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "config.h"

#define PGM_GRAPH_NAME_LEN		80
#define PGM_EDGE_NAME_LEN		80
#define PGM_NODE_NAME_LEN		80
#define PGM_MAX_NODES			(1024*4)  /* per graph */
#define PGM_MAX_EDGES			(1024*4)  /* per graph */
#define PGM_MAX_GRAPHS			128       /* per process */

#define PGM_MAX_IN_DEGREE		32
#define PGM_MAX_OUT_DEGREE		PGM_MAX_IN_DEGREE

#define __PGM_SIGNALED         0x80000000
#define __PGM_DATA_PASSING     0x40000000

#define __PGM_EDGE_CV          0x00000001
#define __PGM_EDGE_FIFO        0x00000002
#define __PGM_EDGE_MQ          0x00000004
#define __PGM_EDGE_RING        0x00000008
#define __PGM_EDGE_SOCK_STREAM 0x00000010

typedef enum
{
	/* Signaled edges allow the consumer to sleep just once, even
	   if there are multiple inbound signaled edges. */

	/* condition variable-based IPC */
	pgm_cv_edge		= (__PGM_EDGE_CV   | __PGM_SIGNALED),
	/* Very low overhead IPC for passing POD data between nodes.
	   Currently limited to nodes within the same process. */
	pgm_ring_edge   = (__PGM_EDGE_RING | __PGM_SIGNALED | __PGM_DATA_PASSING),
	/* named FIFO IPC */
	pgm_fast_fifo_edge	= (__PGM_EDGE_FIFO | __PGM_SIGNALED | __PGM_DATA_PASSING),
	/* POSIX message queue IPC */
	pgm_fast_mq_edge	= (__PGM_EDGE_MQ   | __PGM_SIGNALED | __PGM_DATA_PASSING),


	/* Consumer may block/sleep (repeatedly) on select() with unsignaled
	   edges. May be mixed with signaled edges, but consumers can still
	   block on select(). */

	/* named FIFO IPC */
	pgm_fifo_edge	= (__PGM_EDGE_FIFO | __PGM_DATA_PASSING),
	/* POSIX message queue IPC */
	pgm_mq_edge		= (__PGM_EDGE_MQ   | __PGM_DATA_PASSING),
	/* SOCK_STREAM-based IPC */
	pgm_sock_stream_edge = (__PGM_EDGE_SOCK_STREAM | __PGM_DATA_PASSING),
} pgm_edge_type_t;

typedef unsigned char pgm_command_t;
const pgm_command_t PGM_TERMINATE = 0x80;

/* Graph handle (opaque type) */
typedef int graph_t;

/* Node handle (opaque type) */
typedef struct pgm_node_handle
{
	graph_t graph;
	int		node;
} node_t;

/* Edge handle (opaque type) */
typedef struct pgm_edge_handle
{
	graph_t graph;
	int		edge;
} edge_t;

/*
   Edge attributes. Describes producer/consumer constraints and
   edge-type-specific parameters.
 */
typedef struct pgm_edge_attr
{
	/* Token parameters. For data-producing edges, values denote
	   the number of bytes produced/consumed. */

	size_t nr_produce;    /* #tokens produced per invocation */
	size_t nr_consume;    /* #tokens consumed per invocation */
	size_t nr_threshold;  /* #tokens required before consume allowed */

	pgm_edge_type_t type; /* type indicating underlying IPC of the edge */

	/* edge-type specific params */
	union
	{
		struct /* CV edge params */
		{
			/* Initial token count for consumers. This typically
			   should be zero, unless you're doing something tricky. */
			size_t nr_init;
		};
		struct /* Ring buffer params */
		{
			/* nr_produce/nr_consume interpreted as size of members */

			/* Number of elements in ring buffer */
			size_t nmemb;
		};
		struct /* POSIX message queue params */
		{
			/* Values > 10 may require root privilege. */
			int mq_maxmsg;
		};
		struct /* sock_stream_params */
		{
			/* port of producer */
			short port;
			/* host name or IP address of the producer (used by the consumer) */
			const char* node;
			/* producer's socket descriptor */
			int fd_prod_socket;
		};
	};
} edge_attr_t;

/* default to simple signal-based, non-data-passing IPC */
static const edge_attr_t default_edge = {
	.nr_produce   = 1,
	.nr_consume   = 1,
	.nr_threshold = 1,
	.type         = pgm_cv_edge,
};

/*
   Initialize the PGM runtime in the application for cases where
   graph nodes are always (1) process-local (threads within the same
   process) and (2) FIFO-based edges are never used.

   Use pgm_init() if nodes are cross-process, or FIFO edges
   are needed. If just FIFO edges are needed and nodes are not
   cross-process, pass "use_shared_mem=0" to pgm_init().

   Return: 0 on success. -1 on error. +1 on already initialized.
*/
int pgm_init_process_local(void);

/*
   Initialize the PGM runtime in the application.
     [in] dir: Directory where PGM data is stored.
     [in] create:
           0 = Only read existing graph files. (Files should have been
               created by another process.)
           1 = Create graph files. (Pass '1' if graph nodes will be
               executed within the same process.)
     [in] use_shared_mem:
           0 = Don't create shared memory segments.
           1 = Create shared memory segments needed to support
               graph sharing between processes.
    Return: 0 on success. -1 on error.
 */
int pgm_init(const char* dir, int create = 0, int use_shared_mem = 0);

/*
   Destroy any PGM runtime data. Call before process termination.
   Return: 0 on success. -1 on error.
 */
int pgm_destroy();

/*
   Create a new graph.
     [out] graph: Pointer to graph
     [in]  graph_name: Name of graph
   Return: 0 on success. -1 on error.
 */
int pgm_init_graph(graph_t* graph, const char* graph_name);

/*
   Prints the graph in dot format to the provided file descriptor.
     [in] graph: Graph
     [in] file: File descriptor
   Return: 0 on success. -1 on error.
 */
int pgm_print_graph(graph_t graph, FILE* out);

/*
   Destroy a graph.
     [in] graph: Graph
   Return: 0 on success. -1 on error.
 */
int pgm_destroy_graph(graph_t graph);

/*
   Add a node to a graph.
     [out] node: Pointer to node
     [in]  graph: Graph
     [in]  name: Name of node
   Return: 0 on success. -1 on error.
 */
int pgm_init_node(node_t* node, graph_t graph, const char* name);

/*
   Add an edge between two nodes in the same graph.
     [out] edge: Pointer to edge
     [in]  producer: The producer node
     [in]  consumer: The consumer node
     [in]  name: Name of the edge
     [in]  attr: Edge parameters
   Return: 0 on success. -1 on error.
 */
int pgm_init_edge(edge_t* edge, node_t producer, node_t consumer,
	const char* name, const edge_attr_t* attr = &default_edge);

/*
   Add an back-edge between two nodes in the same graph. It is assumed
   that the producer will be a child of the consumer. The 'nr_skip' parameter
   denotes how many times the consumer may execute before it starts reading
   from the edge. For data-passing edges, contents of data buffer is undefined.
     [out] edge: Pointer to edge
     [in]  nr_skips: Number of times consumer initially skips reading the edge
     [in]  producer: The producer node
     [in]  consumer: The consumer node
     [in]  name: Name of the edge
     [in]  attr: Edge parameters
   Return: 0 on success. -1 on error.
 */
int pgm_init_backedge(edge_t* edge, size_t nr_skips,
	node_t producer, node_t consumer,
	const char* name, const edge_attr_t* attr = &default_edge);

/*
   Find a graph by its name. (Graph is found within the namespace
   defined by the 'dir' parameter of pgm_init()).
     [out] graph: Pointer to graph
     [in]  name: Name of graph to find
   Return: 0 on success. -1 on error.
 */
int pgm_find_graph(graph_t* graph, const char* name);

/*
   Find a node by name from a given graph.
     [out] node: Pointer to node
     [in]  graph: Graph
     [in]  name: Name of node to find
   Return: 0 on success. -1 on error.
 */
int pgm_find_node(node_t* node, graph_t graph, const char* name);

/*
   Find an edge between two nodes, by name, from a given graph.
     [out] edge: Pointer to edge
     [in]  producer: The edge's producer
     [in]  consumer: The edge's consumer
     [in]  name: Name of node to find
     [out] attrs: Pointer to where attributes of found edge may be stored.
                  May be NULL.
   Return: 0 on success. -1 on error.
 */
int pgm_find_edge(edge_t* edge, node_t producer, node_t consumer,
	const char* name, edge_attr_t* attrs = NULL);

/*
   Find the first edge between a producer and consumer.
     [out] edge: Pointer to edge
     [in]  producer: The edge's producer
     [in]  consumer: The edge's consumer
     [out] attrs: Pointer to where attributes of found edge may be stored.
                  May be NULL.
   Return: 0 on success. -1 on error.
 */
int pgm_find_first_edge(edge_t* edge, node_t producer, node_t consumer,
	edge_attr_t* attrs = NULL);

/*
   Attach user data to a given node.
     [in] node: Node to which data is to be attached
     [in] udata: Pointer to attach to the node
   Return: 0 on success. -1 on error.
 */
int pgm_set_user_data(node_t node, void* udata);

/*
   Get user data attached to a given node.
     [in] node: Node from which to fetch user data
   Return: Attached user data. NULL if no data attached.
 */
void* pgm_get_user_data(node_t node);

/*
   Get node descriptors to all successors of a node.
     [in]     node: Node descriptor
     [in/out] successors: Buffer for returning successors
     [in]     len: Number of node descriptors that 'sucessors' can hold.
                   Must be >= number of successors of node.
   Return: 0 on success. -1 on error.
 */
int pgm_get_successors(node_t n, node_t* successors, int len, int ignore_backedges = 1);

/*
   Get edge descriptors of all outbound edges of a node.
     [in]     node: Node descriptor
     [in/out] edges: Buffer for returning edges
     [in]     len: Number of edge descriptors that 'edges' can hold.
                   Must be >= number of outbound-edges of node.
   Return: 0 on success. -1 on error.
 */
int pgm_get_edges_out(node_t n, edge_t* edges, int len, int ignore_backedges = 1);

/*
   Get node descriptors to all predecessors of a node.
     [in]     node: Node descriptor
     [in/out] predecessors: Buffer for returning predecessors
     [in]     len: Number of node descriptors that 'predecessors' can hold.
                   Must be >= number of predecessors of node.
   Return: 0 on success. -1 on error.
 */
int pgm_get_predecessors(node_t, node_t* predecessors, int len, int ignore_backedges = 1);

/*
   Get edge descriptors of all inbound edges of a node.
     [in]     node: Node descriptor
     [in/out] edges: Buffer for returning edges
     [in]     len: Number of edge descriptors that 'edges' can hold.
                   Must be >= number of outbound-edges of node.
   Return: 0 on success. -1 on error.
 */
int pgm_get_edges_in(node_t n, edge_t* edges, int len, int ignore_backedges = 1);

/*
   Get the total number of edges connected to a node.
     [in] node: Node descriptor
   Return: Degree of node. -1 on error.
 */
int pgm_get_degree(node_t node, int ignore_backedges = 1);

/*
   Get the number of inbound edges of a node.
     [in] node: Node descriptor
   Return: Inbound degree of edge. -1 on error.
 */
int pgm_get_degree_in(node_t node, int ignore_backedges = 1);

/*
   Get the number of outbound edges of a node.
     [in] node: Node descriptor
   Return: Outbound degree of node. -1 on error.
 */
int pgm_get_degree_out(node_t node, int ignore_backedges = 1);

/*
   Get the name of a node.
     [in] node: Node descriptor
   Return: Name of the node. NULL on error.
 */
const char* pgm_get_name(node_t node);

/*
   Get the producer of an edge.
     [in] edge: Edge descriptor
   Return: Node descriptor of edge's producer
 */
node_t pgm_get_producer(edge_t edge);

/*
   Get the consumer of an edge.
     [in] edge: Edge descriptor
   Return: Node descriptor of edge's consumer
 */
node_t pgm_get_consumer(edge_t edge);

/*
   Get all the attributes of an edge.
     [in]   edge: Edge descriptor
     [out] attrs: Pointer to where attributes are to be stored
   Return: 0 on success. -1 on error.
 */
int pgm_get_edge_attrs(edge_t edge, edge_attr_t* attrs);

/*
   Query to determine if edge was added with pgm_init_backedge().
     [in] backedge: Edge descriptor
   Returns: 1 if edge was added with pgm_init_backedge(). 0 otherwise.
 */
int pgm_is_backedge(edge_t backedge);

/*
   Get the number of skips remaining on a backedge.
     [in] edge: Edge descriptor
   Returns: Number of remaining skips. Beware: Returns 0 if edge is invalid.
 */
size_t pgn_get_nr_skips_remaining(edge_t backedge);

/*
   Get the number of tokens generated by the producer per invocation.
     [in] edge: Edge descriptor
   Return: Number of tokens produced on edge. -1 on error.
 */
int pgm_get_nr_produce(edge_t edge);

/*
   Get the number of tokens consumed by the consumer per invocation.
     [in] edge: Edge descriptor
   Return: Number of tokens consumed on edge. -1 on error.
 */
int pgm_get_nr_consume(edge_t edge);

/*
   Get the token threshold that must be met before tokens can be consumed.
     [in] edge: Edge descriptor
   Return: Token threshold on edge. -1 on error.
 */
int pgm_get_nr_threshold(edge_t edge);

/*
   Query to see if 'query' is a child of 'node'.
     [in] node:  possible parent of 'query'
     [in] query: possible child of 'node'
   Return:
     1 if 'query' is a child of 'node'.
     0 if 'query' is NOT a child of 'node'.
     -1 if 'query' or 'node' is an invalid node, or
       if 'query' and 'node' are from different graphs

   Note: Explicit backedges are ignored.

   Note: Beware that pgm_is_child() returns -1 on error,
     so the code "if(pgm_is_child(..))" may introduce bugs
     since the logic test does not distinguish between the
     positive case and the error case.
 */
int pgm_is_child(node_t node, node_t query);

/*
   Query to see if 'query' is a parent of 'node'.
     [in] node:  possible child of 'query'
     [in] query: possible parent of 'node'
   Return:
     1 if 'query' is a parent of 'node'.
     0 if 'query' is NOT a parent of 'node'.
    -1 if 'query' or 'node' is an invalid node, or
       if 'query' and 'node' are from different graphs

   Note: Explicit backedges are ignored.

   Note: Beware that pgm_is_parent() returns -1 on error,
     so the code "if(pgm_is_parent(..))" may introduce bugs
     since the logic test does not distinguish between the
     positive case and the error case.
 */
int pgm_is_parent(node_t node, node_t query);

/*
   Query to see if 'query' is an ancestor of 'node'.
     [in] node:  possible decendent of 'query'
     [in] query: possible ancestor of 'node'
   Return:
     1 if 'query' is an ancestor of 'node'.
     0 if 'query' is NOT an ancestor of 'node'.
    -1 if 'query' or 'node' is an invalid node, or
       if 'query' and 'node' are from different graphs

   Note: Explicit backedges are ignored.

   Note: Beware that pgm_is_ancestor() returns -1 on error,
     so the code "if(pgm_is_ancestor(..))" may introduce bugs
     since the logic test does not distinguish between the
     positive case and the error case.
 */
int pgm_is_ancestor(node_t node, node_t query);

/*
   Query to see if 'query' is a decendant of 'node'.
     [in] node:  possible ancestor of 'query'
     [in] query: possible decendent of 'node'
   Return:
     1 if 'query' is an decendent of 'node'.
     0 if 'query' is NOT an decendent of 'node'.
    -1 if 'query' or 'node' is an invalid node, or
       if 'query' and 'node' are from different graphs

   Note: Explicit backedges are ignored.

   Note: Beware that pgm_is_descendant() returns -1 on error,
     so the code "if(pgm_is_descendant(..))" may introduce bugs
     since the logic test does not distinguish between the
     positive case and the error case.
 */
int pgm_is_descendant(node_t node, node_t query);

/*
   Check if a graph is a acyclic.
     [in] graph: Graph descriptor
     [in] ignore_explicit_backedges: Ignore edges that were added
          with pgm_init_backedge().
   Return: 1 if graph is acyclic, 0 if it is not.
 */
int pgm_is_dag(graph_t graph, int ignore_explicit_backedges = 1);

/* Signature of a function that weights a given edge */
typedef double (*pgm_weight_func_t)(edge_t e, void* user);

/*
   Get the length of the shortest path from any graph source to a node,
   optionally weighted by user function. Edges added with pgm_init_backedge()
   are ignored.
     [in] node: Node descriptor
     [in]    w: Edge weight function. If NULL, then default edge
                weight of 1.0 is used.
     [in] user: User data passed to 'w' as input. May be NULL.
   Return: Length of shortest path.
 */
double pgm_get_min_depth(node_t node, pgm_weight_func_t w = NULL, void* user = NULL);

/*
   Get the length of the longest path from any graph source to a node,
   optionally weighted by a user function. Edges added with pgm_init_backedge()
   are ignored.
     [in] node: Node descriptor
     [in]    w: Edge weight function. If NULL, then default edge
                weight of 1.0 is used.
     [in] user: User data passed to 'w' as input. May be NULL.
   Return: Length of the longest path.
 */
double pgm_get_max_depth(node_t node, pgm_weight_func_t w = NULL, void* user = NULL);

/*
   Establish exclusive ownership of a node by a thread of execution.
     [in] node: Node descriptor
     [in]  tid: Thread descriptor. (optional)
   Return: 0 on success. -1 on error.
 */
int pgm_claim_node(node_t node, pid_t tid = 0);

/*
   Establish exclusive ownership of any free node by a thread of execution.
     [in]  graph: Graph descriptor
     [out]  node: Pointer to where claimed node descriptor is stored
     [in]    tid: Thread descriptor (optional)
   Return: 0 on success. -1 on error.
 */
int pgm_claim_any_node(graph_t graph, node_t* node, pid_t tid = 0);

/*
   Free a node previously claimed by a thread of execution.
     [in] node: Node descriptor
     [in]  tid: Thread descriptor of the owner. Must match the 'tid' that was
                used to establish the ownership.
   Return: 0 on success. -1 on error.
 */
int pgm_release_node(node_t node, pid_t tid = 0);

/*
   Wait for, and consume, inbound tokens. Caller will block until
   all requisite tokens are available.
     [in] node: Node descriptor
   Return: 0 on success. PGM_TERMINATE if node was signaled to exit.
           -1 on error.
 */
int pgm_wait(node_t node);

/*
   Generate tokens on all outbound edges.
     [in] node: Node descriptor
   Return: 0 on success. -1 on error.
 */
int pgm_complete(node_t node);

/*
   Tell a node that execution is stopping. Termination message is automatically
   passed on to successor nodes. Node cannot produce or consume tokens after
   termination.
     [in] node: Node descriptor
   Return: 0 on success. -1 on error.
 */
int pgm_terminate(node_t node);


/*
   Functions for allocating memory buffers for use with edges.
   All memory used with edges must be allocated using these functions.
   All allocations are 16-byte aligned.
 */

/*
   Allocate an additional buffer for a producer.
     [in] edge: Edge descriptor. Must be of a data-passing edge.
   Return: Pointer to allocated memory. 16-byte aligned.
 */
void* pgm_malloc_edge_buf_p(edge_t edge);

/*
   Allocate an additional buffer for a consumer.
     [in] edge: Edge descriptor. Must be of a data-passing edge.
   Return: Pointer to allocated memory. 16-byte aligned.
 */
void* pgm_malloc_edge_buf_c(edge_t edge);

/*
   Free an allocated edge buffer.
     [in] buf: Pointer to allocated memory
 */
void pgm_free(void* buf);

/*
   Get the currently assigned producer buffer
     [in] edge: Edge descriptor
   Return: Pointer to memory of current producer buffer.
 */
void* pgm_get_edge_buf_p(edge_t edge);

/*
   Get the currently assigned consumer buffer
     [in] edge: Edge descriptor
   Return: Pointer to memory of current consumer buffer.
 */
void* pgm_get_edge_buf_c(edge_t edge);

/*
   Get the edge from an assigned buffer's pointer.
     [in] buf: Pointer to memory buffer
   Return: Edge descriptor of assigned edge. BAD_EDGE
           if edge is not currently assigned.
 */
edge_t pgm_get_edge_from_buf(void* buf);

/*
   Call to determine if buffer is currently in use by an edge.
     [in] buf: Pointer to memory buffer
   Return: 1 if in use. 0 if not. -1 on error.
 */
int  pgm_is_buf_in_use(void* buf);

/*
   Swap the currently assigned producer buffer with the buffer 'buf'.
     [in] edge: Edge descriptor
     [in]  buf: Pointer to memory buffer
   Return: Pointer to the previously assigned memory buffer. User
           is responsible for calling pgm_free() on returned memory
           buffer if it is not (re-)assigned to another edge.
 */
void* pgm_swap_edge_buf_p(edge_t edge, void* buf);

/*
   Swap the currently assigned consumer buffer with the buffer 'buf'.
     [in] edge: Edge descriptor
     [in]  buf: Pointer to memory buffer
   Return: Pointer to the previously assigned memory buffer. User
           is responsible for calling pgm_free() on returned memory
           buffer if it is not (re-)assigned to another edge.
 */
void* pgm_swap_edge_buf_c(edge_t edge, void* buf);

/*
   Swap the edge assignments of two memory buffers. Can be used
   to exchange a consumer buffer with a producer buffer to avoid
   extra memory copies. Buffers need not be from the same graph,
   but they must have the same size.
     [in] a: Pointer to an assigned memory buffer
     [in] b: Pointer to an assigned memory buffer
   Return: 0 on success. -1 on error.
 */
int pgm_swap_edge_bufs(void* a, void* b);

/*
 Convenience functions to allow number-based names instead of
 string-based names.
 */

int pgm_init_graph(graph_t* graph, unsigned int numerical_name);

int pgm_find_graph(graph_t* graph, unsigned int numerical_name);

int pgm_init_node(node_t* node, graph_t graph, unsigned int numerical_name);

int pgm_find_node(node_t* node, graph_t graph, unsigned int numerical_name);

int pgm_init_edge(edge_t* edge, node_t producer, node_t consumer,
	unsigned int numerical_name,
	const edge_attr_t* attrs = &default_edge);

int pgm_init_backedge(edge_t* edge, size_t nskips,
	node_t producer, node_t consumer,
	unsigned int numerical_name,
	const edge_attr_t* attrs = &default_edge);

int pgm_find_edge(edge_t* edge, node_t producer, node_t consumer,
	unsigned int numerical_name,
	edge_attr_t* attrs = NULL);
