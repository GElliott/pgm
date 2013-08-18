#include "pgm.h"

#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <set>
#include <string>
#include <sstream>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>

using namespace std;
using namespace boost;
using namespace boost::interprocess;
using namespace boost::filesystem;

struct pgm_edge
{
	char name[PGM_EDGE_NAME_LEN];
	
	int producer;
	int consumer;
	
	int nr_produce;
	int nr_consume;
	int nr_threshold;

	// fd_in and fd_out may be the same
	// if different ends of the FIFO are
	// open by different processes.
	
	int fd_in;
	int fd_out;
	
	/* Future: Add stats on thresholds, etc. */
}__attribute__((packed));

struct pgm_node
{
	char name[PGM_NODE_NAME_LEN];
	
	int in[PGM_MAX_IN_DEGREE];
	int out[PGM_MAX_OUT_DEGREE];
	
	int nr_in;
	int nr_out;
	
	pid_t owner;
	
}__attribute__((packed));

struct pgm_graph
{
	int in_use;
	char name[PGM_GRAPH_NAME_LEN];
	
	pthread_mutex_t lock;
	
	int nr_nodes;
	struct pgm_node nodes[PGM_MAX_NODES];
	
	int nr_edges;
	struct pgm_edge edges[PGM_MAX_EDGES];
}__attribute__((packed));

static string mem_name;
static __thread bool is_graph_master = false;

static managed_shared_memory *graphMem = NULL;

static struct pgm_graph* graphs;
static path graphPath;

static int safePath(const path& dir)
{		
	static const path allowed[] = {
		path("/dev/shm"),
		path("/home"),
		path("/tmp")
	};
	
	path dirAbs;
	if(dir.is_relative())
	{
		dirAbs = current_path();
		dirAbs /= dir;
	}
	else
	{
		dirAbs = dir;
	}

	if(dirAbs.empty() || dirAbs == path("/"))
		return -1;

	int failures = 0;
	const int nrValidStems = sizeof(allowed)/sizeof(allowed[0]);
	for(int i = 0; i < nrValidStems; ++i)
	{
		for(path::iterator compare(allowed[i].begin()), toCheck(dirAbs.begin());
			compare != allowed[i].end();
			++compare, ++toCheck)
		{
			if(*compare != *toCheck || toCheck == dirAbs.end())
			{
				++failures;
				break;
			}
		}
	}
	
	if(nrValidStems == failures)
		return -1;
	return 0;
}

static int prepare_dir(const path& graphDir)
{
	int ret = -1;
	
	if(0 != safePath(graphDir))
	{
		fprintf(stderr, "PGM failure: %s is an invalid path.\n", graphDir.string().c_str());
		goto out;
	}
	
	if(exists(graphDir) && !is_directory(graphDir))
	{
		fprintf(stderr, "PGM failure: %s is a file.\n", graphDir.string().c_str());
		goto out;
	}
	
	if(boost::filesystem::equivalent(graphDir, current_path()))
	{
		fprintf(stderr, "PGM failure: current working directory cannot be the same as graph directory.\n");
		goto out;
	}
	
	if(!exists(graphDir))
	{
	create_dir:
		if(!create_directories(graphDir))
		{
			fprintf(stderr, "PGM failure: could not create directory %s\n",
					graphDir.string().c_str());
			goto out;
		}
	}
	
	if(!filesystem::is_empty(graphDir))
	{
		if(0 == remove_all(graphDir))
		{
			fprintf(stderr, "PGM failure: unable to remove files in %s\n",
					graphDir.string().c_str());
			goto out;
		}
		goto create_dir; // i know. this makes a child cry somewhere in the world.
	}
	
	ret = 0;
	
out:
	return ret;
}

static string get_mem_name(const path& graphDir)
{
	const char* graphName = "graphMem.dat";
	boost::hash<std::string> string_hash;
	stringstream ss;
	size_t hash;
	
	hash = string_hash(graphDir.string());
	ss<<hex<<hash<<"_"<<graphName;
	
	return ss.str();
}

static int prepare_graph_mem(const path& graphDir)
{
	int ret = -1;
	string memName = get_mem_name(graphDir);
	
	// allocate twice as much space as we really need, just to be safe.
	size_t memsize = sizeof(struct pgm_graph) * PGM_MAX_GRAPHS * 2;
	
	// make sure there's nothing hanging around
	shared_memory_object::remove(memName.c_str());
	
	graphMem = new managed_shared_memory(create_only, memName.c_str(), memsize);
	if(!graphMem)
	{
		fprintf(stderr, "PGM failure: could not create shared memory file %s\n",
				memName.c_str());
		goto out;
	}
	
	graphs = graphMem->construct<struct pgm_graph>("struct pgm_graph graphs")[PGM_MAX_GRAPHS]();
	if(!graphs)
	{
		fprintf(stderr, "PGM failure: shared memory allocation failure.\n");
		goto out;
	}
	memset(graphs, 0, sizeof(struct pgm_graph)*PGM_MAX_GRAPHS);
	
	ret = 0;
	is_graph_master = true;
	mem_name = memName;
	
out:
	return ret;
}



static int open_graph_mem(const path& graphDir, const int timeout_s = 60)
{
	int ret = -1;
	int time = 0;
	string memName = get_mem_name(graphDir);
	
	do {
		try
		{
			if(!graphMem)
				graphMem = new managed_shared_memory(open_only, memName.c_str());
		}
		catch (...)
		{
			sleep(1);
			
			if(timeout_s == ++time)
				goto out;
		}
	} while (!graphMem);

	graphs = graphMem->find<struct pgm_graph>("struct pgm_graph graphs").first;
	ret = 0;
	
out:
	return ret;
}


int pgm_init(const char* dir, int create)
{
	int ret = -1;
	path graphDir(dir);
	
	if(graphDir.is_relative())
	{
		graphDir = current_path();
		graphDir /= dir;
	}
	
	if(create)
	{
		ret = prepare_dir(graphDir);
		if(0 != ret)
			goto out;
		
		ret = prepare_graph_mem(graphDir);
		if(0 != ret)
			goto out;
	}
	else
	{
		ret = open_graph_mem(graphDir);
		if(0 != ret)
			goto out;
	}

	graphPath = graphDir;
	
out:
	return ret;
}

int pgm_destroy(void)
{
	if(!graphMem)
		return -1;
	
	graphs = 0;
	delete graphMem;
	graphMem = 0;
	
	if(is_graph_master)
		shared_memory_object::remove(mem_name.c_str());
	
	return 0;
}


static int prepare_graph(graph_t* graph, const char* graph_name)
{
	int ret = -1;
	struct pgm_graph *g;
	size_t len;
	
	*graph = -1;
	for(int i = 0; i < PGM_MAX_GRAPHS; ++i)
	{
		if(!graphs[i].in_use)
		{
			*graph = i;
			break;
		}
	}
	
	if(*graph == -1)
	{
		fprintf(stderr, "PGM failure: out of graph slots\n");
		goto out;
	}
	
	g = &graphs[*graph];
	memset(g, 0, sizeof(struct pgm_graph));
	g->in_use = 1;
	
	len = strnlen(graph_name, PGM_GRAPH_NAME_LEN);
	if(len <= 0 || len > PGM_GRAPH_NAME_LEN)
	{
		fprintf(stderr, "PGM failure: bad graph name length: %d\n", (int)len);
		goto out;
	}

	strncpy(g->name, graph_name, PGM_GRAPH_NAME_LEN);
	
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&g->lock, &attr);
	pthread_mutexattr_destroy(&attr);
	
	ret = 0;

out:
	return ret;
}

static int open_graph(graph_t* graph, const char* graph_name)
{
	size_t len = strlen(graph_name);
	
	*graph = -1;
	for(int i = 0; i < PGM_MAX_GRAPHS; ++i)
	{
		if(0 == strncmp(graphs[i].name, graph_name, len))
		{
			*graph = i;
			break;
		}
	}
	
//	if(0 != *graph)
//		fprintf(stderr, "PGM failure: could not find graph %s.\n", graph_name);
	
	return (*graph == -1) ? -1: 0;
}

int pgm_find_graph(graph_t* graph, const char* graph_name)
{
	int ret = -1;
	size_t len;
	
	if(!graphMem)
		goto out;
	
	len = strnlen(graph_name, PGM_GRAPH_NAME_LEN);
	if(len <= 0 || len > PGM_GRAPH_NAME_LEN)
		goto out;

	ret = open_graph(graph, graph_name);
	
out:
	return ret;
}

int pgm_init_graph(graph_t* graph, const char* graph_name)
{
	int ret = -1;
	size_t len;
	
	if(!graphMem)
		goto out;
	
	len = strnlen(graph_name, PGM_GRAPH_NAME_LEN);
	if(len <= 0 || len > PGM_GRAPH_NAME_LEN)
		goto out;
	
	if(is_graph_master && 0 != pgm_find_graph(graph, graph_name))
		ret = prepare_graph(graph, graph_name);
out:
	return ret;
}

static int is_valid_handle(graph_t graph)
{
	return(graph >= 0 && graph <= PGM_MAX_GRAPHS);
}

static int is_valid_graph(graph_t graph)
{
	return (graphs != 0) && is_valid_handle(graph) && graphs[graph].in_use;
}

static std::string fifo_name(pgm_graph* g, pgm_node* producer, pgm_node* consumer, pgm_edge* edge)
{
	boost::hash<std::string> string_hash;
	size_t hash = string_hash(graphPath.string());
	stringstream ss;
	ss<<hash<<"_"<<g->name<<"_"<<producer->name<<"_"<<consumer->name<<"_"<<edge->name<<".edge";
	return ss.str();
}

static int create_fifo(pgm_graph* g, pgm_node* producer, pgm_node* consumer, pgm_edge* edge)
{
	int ret = -1;
	string fifoName(fifo_name(g, producer, consumer, edge));
	
	path fifoPath(graphPath);
	fifoPath /= fifoName;
	
	// TODO: See what boost can do here.
	ret = mkfifo(fifoPath.string().c_str(), S_IRUSR | S_IWUSR);
	if(0 != ret)
	{
		fprintf(stderr, "PGM failure: failed to make FIFO %s. Error %d: %s\n",
				fifoPath.string().c_str(), errno, strerror(errno));
	}
	return ret;
}

static int destroy_fifo(pgm_graph* g, pgm_node* producer, pgm_node* consumer, pgm_edge* edge)
{
	int ret = -1;
	string fifoName(fifo_name(g, producer, consumer, edge));
	
	path fifoPath(graphPath);
	fifoPath /= fifoName;
	
	if(!exists(fifoPath))
		goto out;
	if(!remove(fifoPath))
		goto out;
		
	ret = 0;
	
out:
	return ret;
}

static void __destroy_graph(struct pgm_graph* g)
{
	for(int i = 0; i < g->nr_edges; ++i)
	{
		destroy_fifo(g,
					 &(g->nodes[g->edges[i].producer]),
					 &(g->nodes[g->edges[i].consumer]),
					 &(g->edges[i]));
	}
	
	g->in_use = 0;
	g->nr_nodes = 0;
	memset(g->name, 0, sizeof(g->name));
	memset(g->nodes, 0, sizeof(g->nodes));
}

int pgm_destroy_graph(graph_t graph)
{
	int ret = -1;
	int abort = 0;
	struct pgm_graph* g;
	
	if(!graphMem)
		goto out;
	if(!is_graph_master)
		goto out;
	if(!is_valid_graph(graph))
		goto out;
	
	g = &graphs[graph];
	
	
	pthread_mutex_lock(&g->lock);
	for(int i = 0; i < g->nr_nodes; ++i)
	{
		if(g->nodes[i].owner != 0)
		{
			fprintf(stderr, "PGM failure: node %s still in use by %d\n",
					g->nodes[i].name, g->nodes[i].owner);
			abort = 1;
			goto out_unlock;
		}
	}
	
out_unlock:
	if(!abort)
	{
		__destroy_graph(g);
		pthread_mutex_unlock(&g->lock);
		ret = 0;
	}
	else
	{
		pthread_mutex_unlock(&g->lock);
	}

out:
	return ret;
}


int pgm_init_node(node_t* node, graph_t graph, const char* name)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;
	size_t len;

	if(!node || !is_valid_graph(graph))
		goto out;
	if(!is_graph_master)
		goto out;
	len = strnlen(name, PGM_NODE_NAME_LEN);
	if(len <= 0 || len > PGM_NODE_NAME_LEN)
		goto out;
	
	g = &graphs[graph];
	pthread_mutex_lock(&g->lock);
	
	if(g->nr_nodes + 1 == PGM_MAX_NODES)
	{
		fprintf(stderr, "PGM failure: no more available nodes for graph %s.\n",
				g->name);
		goto out_unlock;
	}
	
	node->graph = graph;
	node->node = (g->nr_nodes)++;
	n = &g->nodes[node->node];

	// memset just to be safe...
	memset(n, 0, sizeof(*n));
	strncpy(n->name, name, len);
	ret = 0;
	
out_unlock:
	pthread_mutex_unlock(&g->lock);
out:
	return ret;
}

int pgm_find_node(node_t* node, graph_t graph, const char* name)
{
	int ret = -1;
	size_t len;
	struct pgm_graph* g;
	
	if(!node || !is_valid_graph(graph))
		goto out;
	len = strnlen(name, PGM_NODE_NAME_LEN);
	if(len <= 0 || len > PGM_NODE_NAME_LEN)
		goto out;
	
	g = &graphs[graph];

	pthread_mutex_lock(&g->lock);
	for(int i = 0; i < g->nr_nodes; ++i)
	{
		if(0 == strncmp(g->nodes[i].name, name, len))
		{
			node->graph = graph;
			node->node = i;
			ret = 0;
			break;
		}
	}
	pthread_mutex_unlock(&g->lock);

out:
	return ret;
}

int pgm_init_edge(edge_t* edge, node_t producer, node_t consumer, const char* name,
	int produce, int consume, int threshold)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_edge* e;
	struct pgm_node* np;
	struct pgm_node* nc;
	size_t len;
	
	if(!edge || (producer.graph != consumer.graph) || !is_valid_graph(producer.graph))
		goto out;
	if(!is_graph_master)
		goto out;
	len = strnlen(name, PGM_EDGE_NAME_LEN);
	if(len <= 0 || len > PGM_EDGE_NAME_LEN)
		goto out;
	
	g = &graphs[producer.graph];
	pthread_mutex_lock(&g->lock);
	
	if(g->nr_edges + 1 == PGM_MAX_EDGES)
	{
		fprintf(stderr, "PGM failure: no more available edges for graph %s.\n",
				g->name);
		goto out_unlock;
	}
	if(g->nr_nodes <= producer.node || g->nr_nodes <= consumer.node)
	{
		fprintf(stderr, "PGM failure: invalid nodes.\n");
		goto out_unlock;
	}
	
	edge->graph = producer.graph;
	edge->edge = (g->nr_edges)++;
	e = &g->edges[edge->edge];
	
	np = &g->nodes[producer.node];
	if(np->nr_out+1 == PGM_MAX_OUT_DEGREE)
		goto out_unlock;
	nc = &g->nodes[consumer.node];
	if(nc->nr_in+1 == PGM_MAX_IN_DEGREE)
		goto out_unlock;
	
	np->out[np->nr_out++] = edge->edge;
	nc->in[nc->nr_in++] = edge->edge;

	// memset just to be safe...
	memset(e, 0, sizeof(*e));
	strncpy(e->name, name, len);
	e->producer = producer.node;
	e->consumer = consumer.node;
	e->nr_produce = produce;
	e->nr_consume = consume;
	e->nr_threshold = threshold;

	ret = create_fifo(g, np, nc, e);
	
out_unlock:
	pthread_mutex_unlock(&g->lock);
out:
	return ret;
}

int pgm_find_edge(edge_t* edge, node_t producer, node_t consumer, const char* name)
{
	int ret = -1;
	struct pgm_graph* g;
	size_t len;
	
	if(!edge || (producer.graph != consumer.graph) || !is_valid_graph(producer.graph))
		goto out;
	len = strnlen(name, PGM_EDGE_NAME_LEN);
	if(len <= 0 || len > PGM_EDGE_NAME_LEN)
		goto out;
	
	g = &graphs[producer.graph];
	
	pthread_mutex_lock(&g->lock);
	for(int i = 0; i < g->nr_edges; ++i)
	{
		if(0 == strncmp(g->edges[i].name, name, len))
		{
			int found = 0;
			pgm_node *np = &g->nodes[producer.node];
			pgm_node *nc = &g->nodes[consumer.node];
		
			for(int j = 0; j < np->nr_out; ++j)
			{
				if(i == np->out[j])
				{
					found++;
					break;
				}
			}
			for(int j = 0; j < nc->nr_in; ++j)
			{
				if(i == nc->in[j])
				{
					found++;
					break;
				}
			}
			
			if(found != 2)
				goto out_unlock;
			
			edge->graph = producer.graph;
			edge->edge = i;
			ret = 0;
			break;
		}
	}
	
out_unlock:
	pthread_mutex_unlock(&g->lock);
out:
	return ret;
}

const char* pgm_name(node_t node)
{
	const char* name = NULL;

	struct pgm_graph* g;
	struct pgm_node* n;

	if(!is_valid_graph(node.graph))
		goto out;

	g = &graphs[node.graph];
	n = &g->nodes[node.node];
	name = n->name;

out:
	return name;
}

int pgm_nr_produce(edge_t edge)
{
	int produced = -1;
	struct pgm_graph* g;

	if(!is_valid_graph(edge.graph))
		goto out;

	g = &graphs[edge.graph];
	produced = g->edges[edge.edge].nr_produce;

out:
	return produced;
}

int pgm_nr_consume(edge_t edge)
{
	int consumed = -1;
	struct pgm_graph* g;

	if(!is_valid_graph(edge.graph))
		goto out;

	g = &graphs[edge.graph];
	consumed = g->edges[edge.edge].nr_consume;

out:
	return consumed;
}

int pgm_nr_threshold(edge_t edge)
{
	int threshold = -1;
	struct pgm_graph* g;

	if(!is_valid_graph(edge.graph))
		goto out;

	g = &graphs[edge.graph];
	threshold = g->edges[edge.edge].nr_threshold;

out:
	return threshold;
}


int pgm_degree(node_t node)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;

	if(!is_valid_graph(node.graph))
		goto out;

	g = &graphs[node.graph];
	n = &g->nodes[node.node];
	ret = n->nr_in + n->nr_out;

out:
	return ret;
}

int pgm_degree_in(node_t node)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;

	if(!is_valid_graph(node.graph))
		goto out;

	g = &graphs[node.graph];
	n = &g->nodes[node.node];
	ret = n->nr_in;

out:
	return ret;
}

int pgm_degree_out(node_t node)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;

	if(!is_valid_graph(node.graph))
		goto out;

	g = &graphs[node.graph];
	n = &g->nodes[node.node];
	ret = n->nr_out;

out:
	return ret;
}

int pgm_find_successors(node_t node, node_t** successors, int* num)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;

	if(!is_valid_graph(node.graph))
		goto out;

	ret = 0;
	g = &graphs[node.graph];
	n = &g->nodes[node.node];

	*num = n->nr_out;
	if(*num == 0)
	{
		*successors = NULL;
		goto out;
	}

	*successors = (node_t*)malloc((*num) * sizeof(node_t));
	for(int i = 0; i < *num; ++i)
	{
		const pgm_node* const _succ = &g->nodes[g->edges[n->out[i]].consumer];
		node_t succ =
		{
			.graph = node.graph,
			.node = (int)(_succ - &g->nodes[0])
		};
		(*successors)[i] = succ;
	}

out:
	return ret;
}

int pgm_find_predecessors(node_t node, node_t** predecessors, int* num)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;

	if(!is_valid_graph(node.graph))
		goto out;

	ret = 0;
	g = &graphs[node.graph];
	n = &g->nodes[node.node];

	*num = n->nr_in;
	if(*num == 0)
	{
		*predecessors = NULL;
		goto out;
	}

	*predecessors = (node_t*)malloc((*num) * sizeof(node_t));
	for(int i = 0; i < *num; ++i)
	{
		const pgm_node* const _pred = &g->nodes[g->edges[n->in[i]].producer];
		node_t pred =
		{
			.graph = node.graph,
			.node = (int)(_pred - &g->nodes[0])
		};
		(*predecessors)[i] = pred;
	}

out:
	return ret;
}

static int dag_visit(
	const struct pgm_graph* const g,
	const struct pgm_node* const n,
	std::set<std::string>& visited,
	std::set<std::string>& path)
{
	const std::string name(n->name);

	// recursive DFS to detect cycles
	if(visited.find(name) == visited.end())
	{
		visited.insert(name);
		path.insert(name);

		for(int i = 0; i < n->nr_out; ++i)
		{
			const struct pgm_node* const successor = &(g->nodes[g->edges[n->out[i]].consumer]);
			const std::string successor_name(successor->name);

			// already appears on this path?
			if(path.find(successor_name) != path.end())
				return 0;

			// visit successor
			if(!dag_visit(g, successor, visited, path))
				return 0;
		}

		path.erase(path.find(name));
	}

	return 1;
}

int pgm_is_dag(graph_t graph)
{
	int isDag = 1; // assume true
	if(!is_valid_graph(graph))
	{
		isDag = 0;
	}
	else
	{
		const struct pgm_graph* const g = &graphs[graph];
		std::set<std::string> visited;

		// there might be multiple roots or even unconnected nodes,
		// so iterate over the set until all have been visited or
		// graph proven not to be a dag.
		for(int i = 0; i < g->nr_nodes && 1 == isDag; ++i)
		{
			const pgm_node* const n = &(g->nodes[i]);
			if(visited.find(std::string(n->name)) == visited.end())
			{
				std::set<std::string> path;
				isDag = dag_visit(g, n, visited, path);
			}
		}
	}

	return isDag;
}

int pgm_claim_node(node_t node, pid_t tid)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;
	
	if(!is_valid_graph(node.graph))
		goto out;
	
	g = &graphs[node.graph];
	
	pthread_mutex_lock(&g->lock);
	{
		if(node.node < 0 || node.node >= g->nr_nodes)
		{
			pthread_mutex_unlock(&g->lock);
			goto out;
		}
		
		n = &g->nodes[node.node];
		n->owner = tid;
	}
	pthread_mutex_unlock(&g->lock);
	
	// open connections to the FIFOs.
	//   in-edges first
	for(int i = 0; i < n->nr_in; ++i)
	{
		struct pgm_edge* e = &g->edges[n->in[i]];
		struct pgm_node* p = &g->nodes[e->producer];
		
		path fifoPath(graphPath);
		fifoPath /= fifo_name(g, p, n, e);
		
		e->fd_in = open(fifoPath.string().c_str(), O_RDONLY | O_NONBLOCK);
		if(e->fd_in == -1)
		{
			fprintf(stderr, "PGM failure: could not open inbound edge %s/%s\n",
					g->name, e->name);
			assert(false);
		}
	}
	//   out-edges second
	for(int i = 0; i < n->nr_out; ++i)
	{
		struct pgm_edge* e = &g->edges[n->out[i]];
		struct pgm_node* c = &g->nodes[e->consumer];
		
		path fifoPath(graphPath);
		fifoPath /= fifo_name(g, n, c, e);
		
		e->fd_out = open(fifoPath.string().c_str(), O_WRONLY);
		if(e->fd_out == -1)
		{
			fprintf(stderr, "PGM failure: could not open outbound edge %s/%s\n",
					g->name, e->name);
			assert(false);
		}
	}
	
	ret = 0;
	
out:
	return ret;
}

int pgm_release_node(node_t node, pid_t tid)
{
	int ret = -1;
	struct pgm_graph* g;
	struct pgm_node* n;
	
	if(!is_valid_graph(node.graph))
		goto out;
	
	g = &graphs[node.graph];
	n = &g->nodes[node.node];
	
	pthread_mutex_lock(&g->lock);

	if(node.node < 0 || node.node >= g->nr_nodes || n->owner != tid)
		goto out_unlock;
	
	// close connections to the FIFOs.
	//   out-edges first
	for(int i = 0; i < n->nr_out; ++i)
	{
		struct pgm_edge* e = &g->edges[n->out[i]];
		if(0 != close(e->fd_out))
		{
			fprintf(stderr, "PGM failure: could not close outbound edge %s/%s\n",
					g->name, e->name);
			assert(false);
		}
		e->fd_out = 0;
	}
	//   out-edges second
	for(int i = 0; i < n->nr_in; ++i)
	{
		struct pgm_edge* e = &g->edges[n->in[i]];
		if(0 != close(e->fd_in))
		{
			fprintf(stderr, "PGM failure: could not close inbound edge %s/%s\n",
					g->name, e->name);
			assert(false);
		}
		e->fd_in = 0;
	}
	
	n->owner = 0;
	
	ret = 0;
	
out_unlock:
	pthread_mutex_unlock(&g->lock);
	
out:
	return ret;
}

static int pgm_send(struct pgm_graph* g, struct pgm_node* n, uint8_t msg)
{
	int ret = 0;
	
//	fprintf(stdout, "- %s has %d out edges.\n", n->name, n->nr_out);
	
	for(int i = 0; i < n->nr_out; ++i)
	{
		struct pgm_edge* e = &g->edges[n->out[i]];
		ssize_t bytes = write(e->fd_out, (void*)&msg, sizeof(msg));
		
		if(bytes != sizeof(msg))
		{
			ret = -1;
			
			if(bytes == -1)
				fprintf(stderr, "PGM failure: failed to write msg to "
						"edge %s/%s from node %s/%s. Error %d: %s\n",
						g->name, e->name, g->name, n->name, errno, strerror(errno));
			else if (bytes < (ssize_t)sizeof(msg))
				fprintf(stderr, "PGM failure: failed to write entire msg "
						"to edge %s/%s from node %s/%s.\n",
						g->name, e->name, g->name, n->name);
			else
				assert(false); // we wrote too much? just crash.
		}
	}
	return ret;
}


#if (PGM_MAX_IN_DEGREE > 32 && PGM_MAX_IN_DEGREE <= 64)
typedef uint64_t pgm_fd_mask_t;
#elif (PGM_MAX_IN_DEGREE > 0 && PGM_MAX_IN_DEGREE <= 32)
typedef uint32_t pgm_fd_mask_t;
#else
//#error "Invalid value for PGM_MAX_IN_DEGREE."
typedef uint32_t pgm_fd_mask_t;
#endif

enum eWaitStatus
{
	WaitSuccess = 0,
	WaitTimeout,
	WaitError
};

static eWaitStatus pgm_wait_for_edges(pgm_fd_mask_t* to_wait, struct pgm_graph* g, struct pgm_node* n)
{
	fd_set set;
	pgm_fd_mask_t b;
	int sum, i, scanned;
	
	while(*to_wait)
	{
		FD_ZERO(&set);
		sum = 0;
		
		// build the set
		for(i = 0, b = 1; i < n->nr_in; ++i, b <<= 1)
		{
			if(*to_wait & b)
			{
				FD_SET(g->edges[n->in[i]].fd_in, &set);
				sum += g->edges[n->in[i]].fd_in;
			}
		}
		
		int nr_ready = select(sum + 1, &set, NULL, NULL, NULL);
		if(nr_ready == 0)
			return WaitTimeout;
		if(nr_ready == -1)
			return WaitError;
		
		scanned = 0;
		for(i = 0, b = 1; i < n->nr_in && scanned < nr_ready; ++i, b <<= 1)
		{
			if(FD_ISSET(g->edges[n->in[i]].fd_in, &set))
			{
				*to_wait = *to_wait & ~b;
				++scanned;
			}
		}
	}
	
	return WaitSuccess;
}


static int pgm_recv(struct pgm_graph* g, struct pgm_node* n)
{
	int ret = -1;
	ssize_t bytes;
	uint8_t v;
	
//	fprintf(stdout, "+ %s has %d in edges.\n", n->name, n->nr_in);
	
	// brainfart. easier way?
	pgm_fd_mask_t to_wait = ~((pgm_fd_mask_t)0) >> (sizeof(to_wait)*8 - n->nr_in);
	
retry:
	while(to_wait)
	{
		enum eWaitStatus stat = pgm_wait_for_edges(&to_wait, g, n);
		switch(stat)
		{
			case WaitTimeout:
				break;
			case WaitError:
				fprintf(stderr, "PGM failure: select() error for node %s/%s. Error %d: %s\n",
						g->name, n->name, errno, strerror(errno));
				goto out;
			default:
				assert(!to_wait);  // unkown error...
		}
	}
	
	// all edges are ready for reading
	for(int i = 0; i < n->nr_in; ++i)
	{
		struct pgm_edge* e = &g->edges[n->in[i]];
		bytes = read(e->fd_in, &v, sizeof(v));
		if(bytes > 0)
		{
			;
		}
		else if(bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			fprintf(stderr, "PGM warning: spurious return from select() for "
					"edge %s/%s of node %s/%s. Error %d: %s\n",
					g->name, e->name, g->name, n->name, errno, strerror(errno));
			to_wait = ~((uint32_t)0) >> (sizeof(to_wait)*8 - (i + 1));
			goto retry;
		}
		else
		{
			fprintf(stderr, "PGM failure: read() error for edge %s/%s of "
					"node %s/%s. Error %d: %s\n",
					g->name, e->name, g->name, n->name, errno, strerror(errno));
			goto out;
		}
		
		if(TERMINATE == v)
		{
			return TERMINATE;
		}
	}
	
	ret = 0;
	
out:
	return ret;
}

int pgm_wait(node_t node)
{
	int ret = -1;
	struct pgm_graph* g = &graphs[node.graph];
	struct pgm_node* n = &g->nodes[node.node];
	
	// no locking or error checking for the sake of speed.
	// we assume initialization is done. use higher-level constructs, such
	// as barriers, to ensure clean bring-up and shutdown.
	
	ret = pgm_recv(g, n);
		
	if(ret == TERMINATE)
	{
		pgm_terminate(node);
	}
	
	return ret;
}

int pgm_complete(node_t node)
{
	int ret = -1;
	struct pgm_graph* g = &graphs[node.graph];
	struct pgm_node* n = &g->nodes[node.node];

	// no locking or error checking for the sake of speed.
	// we assume initialization is done. use higher-level constructs, such
	// as barriers, to ensure clean bring-up and shutdown.

	ret = pgm_send(g, n, TOKEN);
	
	return ret;
}

int pgm_terminate(node_t node)
{
	int ret = -1;
	struct pgm_graph* g = &graphs[node.graph];
	struct pgm_node* n = &g->nodes[node.node];
	
	// no locking or error checking for the sake of speed.
	// we assume initialization is done. use higher-level constructs, such
	// as barriers, to ensure clean bring-up and shutdown.
	
	ret = pgm_send(g, n, TERMINATE);
	
	return ret;
}

