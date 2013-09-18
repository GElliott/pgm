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
#include <boost/math/common_factor.hpp>

#include "pgm.h"
#include "litmus.h"

#define NO_LITMUS

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

struct node_compare
{
	bool operator()(const node_t& a, const node_t& b) const
	{
		assert(a.graph == b.graph);
		return(a.node < b.node);
	}
};

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
				sleep_next_period();
			}
		}
	} while(ret != TERMINATE);

	fprintf(stdout, "- %d terminates\n", cfg.node.node);
	
#ifndef NO_LITMUS
	task_mode(BACKGROUND_TASK);
#endif

	CheckError(pgm_release_node(cfg.node));
}

std::string make_edge_name(const std::string& a, const std::string& b)
{
	return std::string("edge_") + a + std::string("_") + b;
}

void parse_graph_description(
				const std::string& desc,
				const graph_t& g,
				std::vector<node_t>& nodes,
				std::vector<edge_t>& edges)
{
	// create all the nodes. this must be done before
	// the edges.
	// function does not need to be fast!

	// strip token information
	std::vector<std::string> nodeNameChunks;
	std::set<std::string> nodeNames; // set to ensure uniqueness
	boost::split(nodeNameChunks, desc, boost::is_any_of(",") || boost::is_any_of(":"));
	for(auto nStr(nodeNameChunks.begin()); nStr != nodeNameChunks.end(); ++nStr)
	{
		std::vector<std::string> tokenDesc;
		boost::split(tokenDesc, *nStr, boost::is_any_of("."));
		nodeNames.insert(tokenDesc[0]);
	}

	// process each uniquely named node
	std::map<std::string, node_t> nodeMap;
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
		if(nodePair.size() > 2)
		{
			throw std::runtime_error(std::string("Invalid edge description: ") + *eStr);
		}
		else if(nodePair.size() == 1)
		{
			// assume single-node graph
			continue;
		}

		int nr_produce = 1;
		int nr_consume = 1;
		int nr_threshold = 1;
		std::vector<std::string> tokenDesc;
		boost::split(tokenDesc, nodePair[0], boost::is_any_of("."));
		if(tokenDesc.size() > 1)
		{
			if(tokenDesc.size() != 2)
			{
				throw std::runtime_error(std::string("Invalid produce description: " + nodePair[0]));
			}
			nr_produce = boost::lexical_cast<int>(tokenDesc[1]);
			nodePair[0] = tokenDesc[0];
		}
		tokenDesc.clear();
		boost::split(tokenDesc, nodePair[1], boost::is_any_of("."));
		if(tokenDesc.size() > 1)
		{
			if(tokenDesc.size() > 4)
			{
				throw std::runtime_error(std::string("Invalid consume description: " + nodePair[1]));
			}
			nr_consume = boost::lexical_cast<int>(tokenDesc[1]);
			if(tokenDesc.size() == 3)
			{
				nr_threshold = boost::lexical_cast<int>(tokenDesc[2]);
			}
			nodePair[1] = tokenDesc[0];
		}

		edge_t e;
		CheckError(pgm_init_edge(&e, nodeMap[nodePair[0]], nodeMap[nodePair[1]],
				make_edge_name(nodePair[0], nodePair[1]).c_str(),
				nr_produce, nr_consume, nr_threshold));
		edges.push_back(e);

//		printf("edge %s: p:%d c:%d t:%d\n",
//				make_edge_name(nodePair[0], nodePair[1]).c_str(),
//				nr_produce, nr_consume, nr_threshold);
	}

	if(!pgm_is_dag(g))
	{
		throw std::runtime_error(std::string("graph is not acyclic"));
	}
}

struct rate
{
	uint64_t y; // interval (microseconds)
	uint64_t x; // number of arrivials in interval y

	bool operator==(const rate& other) const
	{
		return (x*other.y == other.x*y);
	}
};

void validate_rate(node_t n, const std::map<std::string, rate>& rates)
{
	bool valid = true;

	node_t *preds;
	int nr_preds;
	pgm_find_predecessors(n, &preds, &nr_preds);

	uint64_t scale = 1;
	std::vector<std::pair<node_t, rate> > preds_w_rates;
	preds_w_rates.reserve(nr_preds);
	for(int i = 0; i < nr_preds; ++i)
	{
		auto p = rates.find(std::string(pgm_name(preds[i])));
		if(p != rates.end())
		{
			scale *= p->second.y;
			preds_w_rates.push_back(std::make_pair(preds[i], p->second));
		}
	}

	for(int i = 1; i < (int)preds_w_rates.size(); ++i)
	{
		const rate& prev = preds_w_rates[i-1].second;
		const rate& cur = preds_w_rates[i].second;

		edge_t e_prev, e_cur;
		pgm_find_edge(&e_prev, n, preds_w_rates[i-1].first,
			make_edge_name(std::string(pgm_name(preds_w_rates[i-1].first)), std::string(pgm_name(n))).c_str());
		pgm_find_edge(&e_cur, n, preds_w_rates[i].first,
			make_edge_name(std::string(pgm_name(preds_w_rates[i].first)), std::string(pgm_name(n))).c_str());

		rate a = {pgm_nr_produce(e_prev) * prev.x * scale, prev.y * pgm_nr_consume(e_prev)};
		rate b = {pgm_nr_produce(e_cur) * cur.x * scale, cur.y * pgm_nr_consume(e_cur)};

		uint64_t p1 = (pgm_nr_produce(e_prev) * prev.x*(scale/prev.y)) / pgm_nr_consume(e_prev);
		uint64_t p2 = (pgm_nr_produce(e_cur) * cur.x*(scale/cur.y)) / pgm_nr_consume(e_cur);

		bool __valid = (a == b);

		if(!__valid)
		{
			printf("%s (%lu = %lu * (%lu / %lu)) "
				  "and %s (%lu = %lu * (%lu / %lu)) incompatible\n",
				  pgm_name(preds_w_rates[i-1].first),
				  p1, prev.x, scale, prev.y,
				  pgm_name(preds_w_rates[i].first),
				  p2, cur.x, scale, cur.y
				  );
			valid = __valid;
		}
	}

	if(valid)
	{
		printf("%s has valid predecessor execution rates\n", pgm_name(n));
	}
	else
	{
		printf("%s has INvalid predecessor execution rates!!!\n", pgm_name(n));
	}

	free(preds);
}

void parse_graph_rates(const std::string& rateString, graph_t g, std::map<node_t, double, node_compare>& periods_ms)
{
	// Computations must be done on integral values. We use microseconds,
	// but we assume the rates string is expressed in milliseconds.
	//
	// See Sec. 3.1 of "Supporting Soft Real-TIme DAG-based Systems on
    // Multiprocessors with No Utilization Loss" for formulas

	std::set<std::string> tovisit;
	std::map<std::string, rate> rateMap;

	std::vector<std::string> nodeTokens;
	boost::split(nodeTokens, rateString, boost::is_any_of(","));

	// initialize rates for the root tasks
	for(auto iter = nodeTokens.begin(); iter != nodeTokens.end(); ++iter)
	{
		std::vector<std::string> rateTokens;
		boost::split(rateTokens, *iter, boost::is_any_of(":"));

		if(rateTokens.size() != 3)
			throw std::runtime_error(std::string("Invalid rate: ") + *iter);

		rate r =
		{
			.y = (uint64_t)round(ms2us(boost::lexical_cast<double>(rateTokens[2]))),
			.x = boost::lexical_cast<uint64_t>(rateTokens[1])
		};

		rateMap.insert(std::make_pair(std::string(rateTokens[0]), r));
		tovisit.insert(rateTokens[0]);
	}

	// iteratively compute rates for all nodes
	while(!tovisit.empty())
	{
		auto thisNode = tovisit.begin();
		auto thisNodeRate = rateMap[*thisNode];
		node_t n;

		CheckError(pgm_find_node(&n, g, thisNode->c_str()));
		tovisit.erase(thisNode);

		node_t* successors = NULL;
		int numSuccessors;
		CheckError(pgm_find_successors(n, &successors, &numSuccessors));
		if(numSuccessors == 0)
			continue;

		for(int i = 0; i < numSuccessors; ++i)
		{
			const std::string sname(pgm_name(successors[i]));
			assert(!sname.empty());

			edge_t e;
			CheckError(pgm_find_edge(&e, n, successors[i],
				make_edge_name(std::string(pgm_name(n)), sname).c_str()));

			int produce, consume;
			produce = pgm_nr_produce(e);
			CheckError((produce < 1) ? -1 : 0);
			consume = pgm_nr_consume(e);
			CheckError((consume < 1) ? -1 : 0);
			uint64_t y = ((uint64_t)consume * thisNodeRate.y) / (boost::math::gcd(produce*thisNodeRate.x, (uint64_t)consume));

			auto found = rateMap.find(sname);
			if(found != rateMap.end())
			{
				validate_rate(successors[i], rateMap);

				rate oldRate = found->second;
				y = boost::math::lcm(y, oldRate.y);
//				printf("%s: (parent: %s) old:(%d,%d) new:(%d,%d)\n", sname.c_str(), pgm_name(n), (int)oldRate.x, (int)oldRate.y, (int)x, (int)y);
				if(y != oldRate.y)
				{
					uint64_t x = (y * produce * thisNodeRate.x) / (consume * thisNodeRate.y);
					rate newRate = {.y = y, .x = x};
					found->second = newRate;
					tovisit.insert(sname);
				}
			}
			else
			{
				uint64_t x = (y * produce * thisNodeRate.x) / (consume * thisNodeRate.y);
//				printf("%s: (parent: %s) new:(%d,%d)\n", sname.c_str(), pgm_name(n), (int)x, (int)y);
				rate newRate = {.y = y, .x = x};
				rateMap[sname] = newRate;
				tovisit.insert(sname);
			}
		}

		free(successors);
	}

	for(auto iter = rateMap.begin(), theEnd = rateMap.end();
		iter != theEnd;
		++iter)
	{
		node_t n;
		CheckError(pgm_find_node(&n, g, iter->first.c_str()));
		periods_ms[n] = us2ms((double)(iter->second.y)/iter->second.x);

		printf("%s: x:%d y:%d d:%f\n",
			iter->first.c_str(), (int)iter->second.x, (int)iter->second.y, periods_ms[n]);
	}
}

void parse_graph_exec(const std::string& execs, graph_t g, std::map<node_t, double, node_compare>& exec_ms)
{
	std::vector<std::string> nodeNames;
	boost::split(nodeNames, execs, boost::is_any_of(","));
	for(auto nStr(nodeNames.begin()); nStr != nodeNames.end(); ++nStr)
	{
		std::vector<std::string> nodeExecPair;
		boost::split(nodeExecPair, *nStr, boost::is_any_of(":"));

		if(nodeExecPair.size() != 2)
			throw std::runtime_error(std::string("Invalid execution time: " + *nStr));

		node_t n;
		CheckError(pgm_find_node(&n, g, nodeExecPair[0].c_str()));
		exec_ms[n] = boost::lexical_cast<double>(nodeExecPair[1]);	
	}
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
		("graph,g", program_options::value<std::string>(),
		 	"Graph edge description: [<name>[.produce]:<name>[.consume[.threshld]],]+ (do '<name>:' for single-node graph)")
		("rates,r", program_options::value<std::string>(),
		 	"Arrivial rates: [<name>:<#>:<interval>,]+ (interval in ms) (only for source nodes)")
		("execution,x", program_options::value<std::string>(),
		 	"Execution time requirements for nodes. [<name>:<time>,]+ (time in ms)")
		("graphDir,d", program_options::value<std::string>()->default_value("/dev/shm/graphs"),
		 				"Directory to hold PGM FIFOs")
		("continuation", "Graph depends on a sub-graph of another process")
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
	std::map<node_t, double, node_compare> periods;
	std::map<node_t, double, node_compare> executions;

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
			parse_graph_rates(vm["rates"].as<std::string>(), g, periods);
			exit(-1);
			parse_graph_exec(vm["execution"].as<std::string>(), g, executions);

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

	// spawn of a thread for each node in graph
	std::vector<std::thread> threads;
	for(auto iter(nodes.begin() + 1); iter != nodes.end(); ++iter)
	{
		rt_config nodeCfg = cfg;
		nodeCfg.node = *iter;
		nodeCfg.period_ns = ms2ns(periods[*iter]);
		nodeCfg.execution_ns = ms2ns(executions[*iter]);
		threads.push_back(std::thread(work_thread, nodeCfg));
	}

	// main thread handles first node
	// TODO: enforce that first node is a src node
	cfg.node = nodes[0];
	cfg.period_ns = ms2ns(periods[nodes[0]]);
	cfg.execution_ns = ms2ns(executions[nodes[0]]);
	work_thread(cfg);

	for(auto t(threads.begin()); t != threads.end(); ++t)
	{
		t->join();
	}

	CheckError(pgm_destroy_graph(g));
	
	return 0;
}
