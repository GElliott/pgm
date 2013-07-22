#include <iostream>
#include <thread>
#include <exception>
#include <stdexcept>
#include <vector>
#include <map>
#include <cassert>
#include <cstdint>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include "pgm.h"
#include "litmus.h"

//#define NO_LITMUS

using namespace boost;

#define CheckError(e) \
do { \
	int errcode = (e); \
	if(errcode != 0) { \
		fprintf(stderr, "Error %d @ %s:%s:%d\n",  \
			errcode, __FILE__, __FUNCTION__, __LINE__); \
	} \
} while(0)

#ifndef NO_LITMUS
#define nonpreemptive(statements) \
enter_np(); \
statements \
exit_np();
#else
#define nonpreemptive(statements) statements
#endif

struct rt_config
{
	bool syncRelease;
	int cluster;
	int clusterSize;
	int budget;

	uint64_t period_ns;
	uint64_t execution_ns;

	node_t node;
};

void work_thread(rt_config cfg)
{
	int ret = 0;

	bool isRoot = (pgm_degree_in(cfg.node) == 0);

	// claim the node and open up FIFOs, etc.
	CheckError(pgm_claim_node(cfg.node));
	
#ifndef NO_LITMUS
	// become a real-time task
	struct rt_task param;
	init_rt_task_param(&param);
	param.period = cfg.period_ns;
	param.exec_cost = cfg.execution_ns;
	if(cfg.cluster >= 0)
		param.cpu = cluster_to_first_cpu(cfg.cluster, cfg.clusterSize);
	param.budget_policy = (cfg.budget) ? PRECISE_ENFORCEMENT : NO_ENFORCEMENT;
	param.release_policy = (pgm_degree_in(cfg.node) == 0) ?
		TASK_PERIODIC : TASK_EARLY;

	ret = set_rt_task_param(gettid(), &param);
	assert(ret >= 0);

	ret = task_mode(LITMUS_RT_TASK);
	assert(ret == 0);

	if(cfg.syncRelease)
	{
		ret = wait_for_ts_release();
		assert(ret == 0);
	}
#endif

	int count = 0;
	do {
		// We become non-preemptive/boosted when we call
		// pgm_wait() to ensure BOUNDED priority inversions.
		//
		// TODO: We can remove this once the waiting mechanism
		// has been pushed down into the kernel (instead of a looping
		// select()).
		if(!isRoot)
		{
			nonpreemptive(ret = pgm_wait(cfg.node););
		}

		if(ret != TERMINATE)
		{
			CheckError(ret);

			// do job here
			if(isRoot)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			++count;

			if(isRoot && count > 10)
			{
				CheckError(pgm_terminate(cfg.node));
				break;
			}
			else
			{
				fprintf(stdout, "- %d fires\n", cfg.node.node);
				CheckError(pgm_complete(cfg.node));
			}
		}

		if(isRoot)
			sleep_next_period();
	} while(ret != TERMINATE);

	fprintf(stdout, "- %d terminates\n", cfg.node.node);
	
#ifndef NO_LITMUS
	task_mode(BACKGROUND_TASK);
#endif

	CheckError(pgm_release_node(cfg.node));
}

void parse_graph_description(
				const std::string& desc,
				const graph_t& g,
				std::vector<node_t>& nodes,
				std::vector<edge_t>& edges)
{
	// function does not need to be fast!

	std::set<std::string> nodeNames; // set to ensure uniqueness
	std::map<std::string, node_t> nodeMap;

	// create all the nodes. this must be done before
	// the edges.
	boost::split(nodeNames, desc, boost::is_any_of(",") || boost::is_any_of(":"));
	for(auto nStr(nodeNames.begin()); nStr != nodeNames.end(); ++nStr)
	{
		node_t n;
		CheckError(pgm_init_node(&n, g, nStr->c_str()));
		nodeMap.insert(std::make_pair(*nStr, n));
		nodes.push_back(n);
	}

	// create all the edges.
	std::vector<std::string> edgeDesc;
	boost::split(edgeDesc, desc, boost::is_any_of(","));
	for(auto eStr(edgeDesc.begin()); eStr != edgeDesc.end(); ++eStr)
	{
		std::vector<std::string> nodePair;
		boost::split(nodePair, *eStr, boost::is_any_of(":"));
		if(nodePair.size() != 2)
			throw std::runtime_error(std::string("Invalid edge description: ") + *eStr);
		
		edge_t e;
		CheckError(pgm_init_edge(&e, nodeMap[nodePair[0]], nodeMap[nodePair[1]],
				(std::string("edge_") + nodePair[0] + std::string("_") + nodePair[1]).c_str()));
		edges.push_back(e);
	}

	// TODO: Check for cycles. PGM is supposed to be a DAG.
}

void parse_graph_file(
				const std::string& filename,
				graph_t& g,
				std::vector<node_t>& nodes,
				std::vector<edge_t>& edges)
{
	throw std::runtime_error("Graph files not yet implemented.");
}

int main(int argc, char** argv)
{
	program_options::options_description opts("Options");
	opts.add_options()
		("wait,w", "Wait for release")
		("cluster,c", program_options::value<int>()->default_value(-1), "Cluster ID (or CPU ID)")
		("clusterSize,z", program_options::value<int>()->default_value(1), "Cluster size")
		("enforce,e", "Enable budget enforcement")
		("scale,s", program_options::value<double>()->default_value(1.0), "Change time scale")
		("graphfile", program_options::value<std::string>(), "File that describes PGM graph")
		("name,n", program_options::value<std::string>()->default_value(""), "Graph name")
		("graph,g", program_options::value<std::string>(), "Graph description")
		("graphDir,d", program_options::value<std::string>()->default_value("/dev/shm/graphs"),
		 				"Directory to hold PGM FIFOs")
		("continuation", "Graph depends on a graph of another process")
		;

	program_options::variables_map vm;

	try
	{
		program_options::store(program_options::parse_command_line(argc, argv, opts), vm);
	}
	catch(program_options::required_option& e)
	{
		std::cerr<<"Error: "<<e.what()<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}
	catch(program_options::error& e)
	{
		std::cerr<<"Error: "<<e.what()<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}
	catch(...)
	{
		std::cerr<<"Unknown error."<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}

	rt_config cfg =
	{
		.syncRelease = (vm.count("wait") != 0),
		.cluster = vm["cluster"].as<int>(),
		.clusterSize = vm["clusterSize"].as<int>(),
		.budget = (vm.count("budget") != 0),
		.period_ns = 0,
		.execution_ns = 0
	};

	std::string name = vm["name"].as<std::string>();
	std::string graphDir = vm["graphDir"].as<std::string>();
	int master = (vm.count("continuation") == 0);

	CheckError(pgm_init(graphDir.c_str(), master));

	graph_t g;
	std::vector<node_t> nodes;
	std::vector<edge_t> edges;

	try
	{
		if(vm.count("graph") != 0)
		{
			if(master)
				if(name != "")
					CheckError(pgm_init_graph(&g, name.c_str()));
				else
					CheckError(pgm_init_graph(&g, getpid()));
			else
				if(name != "")
					CheckError(pgm_find_graph(&g, name.c_str()));
				else
					assert(false);  // graph must be named if we're not master

			parse_graph_description(vm["graph"].as<std::string>(), g, nodes, edges);
		}
		else if(vm.count("graphfile") != 0)
		{
			parse_graph_file(vm["graphfile"].as<std::string>(), g, nodes, edges);
		}
		else
			throw std::runtime_error("Missing graph file or description");
	}
	catch(std::exception& e)
	{
		std::cerr<<"Error: "<<e.what()<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}

#ifndef NO_LITMUS
	init_litmus(); // prepare litmus
#endif

	// TODO: COMPUTE EXECUTION TIMES/PERIODS FOR EACH NODE
	cfg.period_ns = ms2ns(1000);
	cfg.execution_ns = ms2ns(100);

	// spawn of a thread for each node in graph
	std::vector<std::thread> threads;
	for(auto iter(nodes.begin() + 1); iter != nodes.end(); ++iter)
	{
		rt_config nodeCfg = cfg;
		nodeCfg.node = *iter;
		threads.push_back(std::thread(work_thread, nodeCfg));
	}

	// main thread handles first node
	cfg.node = nodes[0];
	work_thread(cfg);

	for(auto t(threads.begin()); t != threads.end(); ++t)
	{
		t->join();
	}

	CheckError(pgm_destroy_graph(g));
	
	return 0;
}
