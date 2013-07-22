#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
int pgm_find_graph(graph_t* graph, const char* graph_name);

int pgm_destroy_graph(graph_t graph);

int pgm_init_node(node_t* node, graph_t graph, const char* name);
int pgm_find_node(node_t* node, graph_t graph, const char* name);
int pgm_init_edge(edge_t* edge, node_t producer, node_t consumer, const char* name);
int pgm_find_edge(edge_t* edge, node_t producer, node_t consumer, const char* name);

int pgm_claim_node(node_t node, pid_t tid = 0);
int pgm_release_node(node_t node, pid_t tid = 0);

int pgm_wait(node_t node);
int pgm_complete(node_t node);
int pgm_terminate(node_t node);

int pgm_degree(node_t node);
int pgm_degree_in(node_t node);
int pgm_degree_out(node_t node);

int pgm_is_dag(graph_t graph);

// convienience functions to allow number-based names instead of string-based names.

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

static int pgm_init_node(node_t* node, graph_t graph, unsigned int numerical_name)
{
	char name[PGM_NODE_NAME_LEN];
	snprintf(name, PGM_NODE_NAME_LEN, "%x", numerical_name);
	return pgm_init_node(node, graph, name);
}

static int pgm_find_node(node_t* node, graph_t graph, unsigned int numerical_name)
{
	char name[PGM_NODE_NAME_LEN];
	snprintf(name, PGM_NODE_NAME_LEN, "%x", numerical_name);
	return pgm_find_node(node, graph, name);
}

static int pgm_init_edge(edge_t* edge, node_t producer, node_t consumer, unsigned int numerical_name)
{
	char name[PGM_EDGE_NAME_LEN];
	snprintf(name, PGM_EDGE_NAME_LEN, "%x", numerical_name);
	return pgm_init_edge(edge, producer, consumer, name);
}

static int pgm_find_edge(edge_t* edge, node_t producer, node_t consumer, unsigned int numerical_name)
{
	char name[PGM_EDGE_NAME_LEN];
	snprintf(name, PGM_EDGE_NAME_LEN, "%x", numerical_name);
	return pgm_find_edge(edge, producer, consumer, name);
}

