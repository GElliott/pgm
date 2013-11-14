#include <iostream>
#include <thread>
#include <exception>
#include <stdexcept>
#include <vector>
#include <map>
#include <cassert>
#include <cstdint>

// TODO: Use std::chrono routines instead.
#include <sys/time.h>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/math/common_factor.hpp>

#include "pgm.h"
#include "litmus.h"

#define USE_LITMUS
//#define VERBOSE

using namespace boost;

#define CheckError(e) \
do { \
	int errcode = (e); \
	if(errcode != 0) { \
		fprintf(stderr, "Error %d @ %s:%s:%d\n",  \
			errcode, __FILE__, __FUNCTION__, __LINE__); \
	} \
} while(0)

// macro to boost priority of tasks waiting for tokens
// when compiled for Litmus.
#ifdef USE_LITMUS
// waiting task's priority is boosted as needed
#define litmus_pgm_wait(statements) \
	enter_pgm_wait(); \
	statements \
	exit_pgm_wait();
// signalling task's priority is unconditionally boosted,
// so just enter a non-preemptive section.
#define litmus_pgm_complete(statements) \
	enter_pgm_send(); \
	statements \
	exit_pgm_send();
#else
#define litmus_pgm_wait(statements) statements
#define litmus_pgm_complete(statements) statements
#endif

// trace macro for VERBOSE
#ifdef VERBOSE
#define T(...) fprintf(stdout, __VA_ARGS__)
#else
#define T(...)
#endif

#define likely(x)   __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

struct node_compare
{
	bool operator()(const node_t& a, const node_t& b) const
	{
		assert(a.graph == b.graph);
		return(a.node < b.node);
	}
};

struct edge_compare
{
	bool operator()(const edge_t& a, const edge_t& b) const
	{
		assert(a.graph == b.graph);
		return(a.edge < b.edge);
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

	uint64_t duration_ns;

	node_t node;
};

struct WorkingSet
{
	static std::map<edge_t, WorkingSet*, edge_compare> edgeToWs;

	typedef int chunk_t;

	volatile chunk_t* buf; // volatile to ensure compiler doesn't optimize reads/writes away
	int num;
	int cycle;

	WorkingSet(): buf(NULL) {}

	WorkingSet(int setSize, int _cycle = 1): buf(NULL), num(0), cycle(_cycle) {
		if (setSize != 0) {
			num = setSize/sizeof(WorkingSet::chunk_t) + (setSize % sizeof(WorkingSet::chunk_t) != 0);
			buf = new chunk_t[cycle * num];
		}
	}

	~WorkingSet()
	{
		delete [] const_cast<chunk_t*>(buf);
	}

	inline volatile WorkingSet::chunk_t* start(int c)
	{
		int offset = c % cycle;
		return buf + offset;
	}

	inline volatile WorkingSet::chunk_t* end(int c)
	{
		return start(c) + num;
	}
};

std::map<edge_t, WorkingSet*, edge_compare> WorkingSet::edgeToWs;


uint64_t cputime_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (s2ns((uint64_t)ts.tv_sec) + ts.tv_nsec);
}

uint64_t wctime_ns(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (s2ns((uint64_t)tv.tv_sec) + us2ns((uint64_t)tv.tv_usec));
}

void sleep_ns(uint64_t ns)
{
	int64_t seconds = ns / s2ns(1);
	ns -= s2ns(seconds);
	struct timespec ts = {.tv_sec = seconds, .tv_nsec = (int64_t)ns};
	nanosleep(&ts, NULL);
}

#if 0
uint64_t loop_once(void)
{
	// Do a busy loop over 4 pages (if sizeof(int) == 4).
	// Note: Loop affects the cache.

	static const size_t NUMS = 4096;
	static __thread int64_t num[NUMS];

	uint64_t s = 0;
	for (size_t i = 0; i < NUMS; ++i) {
		s += num[i]++;
	}

	return s;
}
#endif

inline void relax(void)
{
#if defined(__i386__) || defined(__x86_64__)
	asm volatile("pause\n");
#elif defined(__arm__)
	asm volatile("nop\n");
#else
#error "Unsupported arch."
#endif
}

uint64_t loop_once(void)
{
	// Do a busy loop without affecting the cache.
	//
	// On a 2.6GHz Intel Xeon, time to run the loop takes about
	// 4x the number of iterations (after 2^8 iterations).

	const uint64_t iters = 1ul<<14; // ~65us on 2.6GHz Intel Xeon
	uint64_t i;

	for(i = 0; i < iters; ++i) {
		relax();
	}

	return i;
}

uint64_t loop_for(uint64_t exec_time, uint64_t emergency_exit)
{
	uint64_t lastLoop = 0;
	uint64_t start = cputime_ns();
	uint64_t now = start;
	uint64_t loopStart;
	uint64_t tmp = 0;

	/* a tight loop that touches virtually no data */
	while (now + lastLoop < start + exec_time) {
		loopStart = now;
		tmp += loop_once();
		now = cputime_ns();
		lastLoop = now - loopStart;
		if (unlikely(emergency_exit && wctime_ns() > emergency_exit)) {
			throw std::runtime_error("Emergency Exit");
		}
	}

	return tmp;
}

inline WorkingSet::chunk_t __consume(WorkingSet* ws, int jobNum)
{
	WorkingSet::chunk_t temp = 0;
	for(volatile WorkingSet::chunk_t
			*p = ws->start(jobNum),
			*end = ws->end(jobNum);
		p < end; ++p) {
		temp += *p;
	}
	return temp;
}

WorkingSet::chunk_t consume(WorkingSet** ws, int numWs, int jobNum)
{
	WorkingSet::chunk_t temp = 0;
	for(int i = 0; i < numWs; ++i) {
		temp += __consume(ws[i], jobNum);
	}
	return temp;
}

inline void __produce(WorkingSet* ws, WorkingSet::chunk_t toWrite, int jobNum)
{
	for(volatile WorkingSet::chunk_t
			*p = ws->start(jobNum),
			*end = ws->end(jobNum);
		p < end; ++p) {
		*p = toWrite;
	}
}

void produce(WorkingSet** ws, int numWs, WorkingSet::chunk_t toWrite, int jobNum)
{
	for(int i = 0; i < numWs; ++i) {
		__produce(ws[i], toWrite, jobNum);
	}
}

bool job(const rt_config& cfg, int jobNum,
	std::vector<WorkingSet*>& consumeList,
	std::vector<WorkingSet*>& produceList,
	uint64_t programEnd)
{
	bool keepGoing = true;
	if (unlikely(wctime_ns() > programEnd)) {
		keepGoing = false;
	}
	else {
		WorkingSet::chunk_t temp = jobNum;

		uint64_t emergency_exit = programEnd + s2ns(1);

		// consume data from predecessors
		if(!consumeList.empty())
			temp = consume(&consumeList[0], consumeList.size(), jobNum);

		// execution time
		try {
			(void) loop_for(cfg.execution_ns, emergency_exit);
		}
		catch(const std::runtime_error& e) {
			fprintf(stderr, "!!! pgmrt/%d emergency exit!\n", gettid());
			fprintf(stderr, "Something is seriously wrong! Do not ignore this.\n");
			keepGoing = false;
		}

		// produce data for successors
		if(!produceList.empty())
			produce(&produceList[0], produceList.size(), temp, jobNum);
	}
	return keepGoing;
}

void work_thread(rt_config cfg)
{
	int ret = 0;

	bool isRoot = (pgm_degree_in(cfg.node) == 0);
	uint64_t bailoutTime = wctime_ns() + cfg.duration_ns;

	// claim the node and open up FIFOs, etc.
	CheckError(pgm_claim_node(cfg.node));


	std::vector<WorkingSet*> consumeWs, produceWs;

	// get pointers to the working sets attached to cfg.node
	{
		edge_t* edges;
		int numEdges;

		edges = NULL;
		numEdges = 0;
		CheckError(pgm_find_in_edges(cfg.node, &edges, &numEdges));
		for(int i = 0; i < numEdges; ++i) {
			auto edgeWithWs = WorkingSet::edgeToWs.find(edges[i]);
			if(edgeWithWs != WorkingSet::edgeToWs.end()) {
				WorkingSet* ws = edgeWithWs->second;
				consumeWs.push_back(ws);
			}
		}
		free(edges);
		T("%s has %d in-edges with working sets\n", pgm_name(cfg.node), (int)consumeWs.size());

		edges = NULL;
		numEdges = 0;
		CheckError(pgm_find_out_edges(cfg.node, &edges, &numEdges));
		for(int i = 0; i < numEdges; ++i) {
			auto edgeWithWs = WorkingSet::edgeToWs.find(edges[i]);
			if(edgeWithWs != WorkingSet::edgeToWs.end()) {
				WorkingSet* ws = edgeWithWs->second;
				produceWs.push_back(ws);
			}
		}
		free(edges);
		T("%s has %d out-edges with working sets\n", pgm_name(cfg.node), (int)produceWs.size());
	}

#ifdef USE_LITMUS
	// become a real-time task
	struct rt_task param;
	init_rt_task_param(&param);
	param.period = cfg.period_ns;
	param.exec_cost = cfg.execution_ns;
	if(cfg.cluster >= 0)
		param.cpu = cluster_to_first_cpu(cfg.cluster, cfg.clusterSize);
	param.budget_policy = (cfg.budget) ? PRECISE_ENFORCEMENT : NO_ENFORCEMENT;
	param.release_policy = (isRoot) ? TASK_PERIODIC : TASK_EARLY;

	ret = set_rt_task_param(gettid(), &param);
	assert(ret >= 0);

	ret = task_mode(LITMUS_RT_TASK);
	assert(ret == 0);

	if(isRoot)
		T("(i) %s is a root\n", pgm_name(cfg.node));

	if(cfg.syncRelease) {
		T("(x) %s waiting for release\n", pgm_name(cfg.node));
		ret = wait_for_ts_release();
		assert(ret == 0);
	}
#else
	uint64_t release_ns, response_ns;
#endif

	int count = 0;
	bool keepGoing;
	do {
		// We become non-preemptive/boosted when we call
		// pgm_wait() to ensure BOUNDED priority inversions.
		//
		// TODO: We can remove this once the waiting mechanism
		// has been pushed down into the kernel (instead of a looping
		// select()).
		if(!isRoot) {
			T("(x) %s waits for tokens\n", pgm_name(cfg.node));
			litmus_pgm_wait(ret = pgm_wait(cfg.node););
		}

#ifndef USE_LITMUS
		// Approximate the release time. May slip due to unbounded priority
		// inversions or scheduler delays.
		release_ns = wctime_ns();
#endif

		if(ret != TERMINATE) {
			CheckError(ret);

			// do job
			keepGoing = job(cfg, count++, consumeWs, produceWs, bailoutTime);

			// only allow roots to trigger a graph-wide bailout
			if(isRoot && !keepGoing) {
				CheckError(pgm_terminate(cfg.node));
				break;
			}
			else {
				T("(+) %s fired for %d time\n", pgm_name(cfg.node), count);

				litmus_pgm_complete(CheckError(pgm_complete(cfg.node)););

#ifdef USE_LITMUS
				sleep_next_period();
#else
				response_ns = wctime_ns() - release_ns;

				// Sources sleep until the next "periodic" release.
				if(isRoot && (response_ns < cfg.period_ns)) {
					uint64_t slack = cfg.period_ns - response_ns;
					sleep_ns(slack);
				}
#endif
			}
		}
	} while(ret != TERMINATE);

	T("(-) %s terminates\n", pgm_name(cfg.node));

#ifdef USE_LITMUS
	task_mode(BACKGROUND_TASK);
#endif

	CheckError(pgm_release_node(cfg.node));
}

std::string make_edge_name(const std::string& a, const std::string& b)
{
	std::string name(std::string("edge_") + a + std::string("_") + b);
	return name;
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
	for(auto nStr(nodeNameChunks.begin()); nStr != nodeNameChunks.end(); ++nStr) {
		std::vector<std::string> tokenDesc;
		boost::split(tokenDesc, *nStr, boost::is_any_of("."));
		nodeNames.insert(tokenDesc[0]);
	}

	// process each uniquely named node
	std::map<std::string, node_t> nodeMap;
	for(auto nStr(nodeNames.begin()); nStr != nodeNames.end(); ++nStr) {
		node_t n;
		CheckError(pgm_init_node(&n, g, nStr->c_str()));
		nodeMap.insert(std::make_pair(*nStr, n));
		nodes.push_back(n);
	}

	// create all the edges.
	std::vector<std::string> edgeDesc;
	boost::split(edgeDesc, desc, boost::is_any_of(","));
	for(auto eStr(edgeDesc.begin()); eStr != edgeDesc.end(); ++eStr) {
		std::vector<std::string> nodePair;
		boost::split(nodePair, *eStr, boost::is_any_of(":"));
		if(nodePair.size() > 2) {
			throw std::runtime_error(std::string("Invalid edge description: ") + *eStr);
		}
		else if(nodePair.size() == 1) {
			// assume single-node graph
			continue;
		}

		int nr_produce = 1;
		int nr_consume = 1;
		int nr_threshold = 1;
		std::vector<std::string> tokenDesc;
		boost::split(tokenDesc, nodePair[0], boost::is_any_of("."));
		if(tokenDesc.size() > 1) {
			if(tokenDesc.size() != 2) {
				throw std::runtime_error(std::string("Invalid produce description: " + nodePair[0]));
			}
			nr_produce = boost::lexical_cast<int>(tokenDesc[1]);
			nodePair[0] = tokenDesc[0];
		}
		tokenDesc.clear();
		boost::split(tokenDesc, nodePair[1], boost::is_any_of("."));
		if(tokenDesc.size() > 1) {
			if(tokenDesc.size() > 4) {
				throw std::runtime_error(std::string("Invalid consume description: " + nodePair[1]));
			}
			nr_consume = boost::lexical_cast<int>(tokenDesc[1]);
			if(tokenDesc.size() == 3) {
				nr_threshold = boost::lexical_cast<int>(tokenDesc[2]);
			}
			nodePair[1] = tokenDesc[0];
		}

		edge_t e;
		CheckError(pgm_init_edge(&e, nodeMap[nodePair[0]], nodeMap[nodePair[1]],
				make_edge_name(nodePair[0], nodePair[1]).c_str(),
				nr_produce, nr_consume, nr_threshold));
		edges.push_back(e);
	}

	if(!pgm_is_dag(g)) {
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
	for(int i = 0; i < nr_preds; ++i) {
		auto p = rates.find(std::string(pgm_name(preds[i])));
		if(p != rates.end()) {
			scale *= p->second.y;
			preds_w_rates.push_back(std::make_pair(preds[i], p->second));
		}
	}

	for(int i = 1; i < (int)preds_w_rates.size(); ++i) {
		const rate& prev = preds_w_rates[i-1].second;
		const rate& cur = preds_w_rates[i].second;

		edge_t e_prev, e_cur;
		CheckError(pgm_find_edge(&e_prev, preds_w_rates[i-1].first, n,
			make_edge_name(std::string(pgm_name(preds_w_rates[i-1].first)), std::string(pgm_name(n))).c_str()));
		CheckError(pgm_find_edge(&e_cur, preds_w_rates[i].first, n,
			make_edge_name(std::string(pgm_name(preds_w_rates[i].first)), std::string(pgm_name(n))).c_str()));

		rate a = {pgm_nr_produce(e_prev) * prev.x * scale, prev.y * pgm_nr_consume(e_prev)};
		rate b = {pgm_nr_produce(e_cur) * cur.x * scale, cur.y * pgm_nr_consume(e_cur)};

		uint64_t p1 = (pgm_nr_produce(e_prev) * prev.x*(scale/prev.y)) / pgm_nr_consume(e_prev);
		uint64_t p2 = (pgm_nr_produce(e_cur) * cur.x*(scale/cur.y)) / pgm_nr_consume(e_cur);

		bool __valid = (a == b);

		if(!__valid) {
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

	if(valid) {
		T("%s has valid predecessor execution rates\n", pgm_name(n));
	}
	else {
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
	for(auto iter = nodeTokens.begin(); iter != nodeTokens.end(); ++iter) {
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
	while(!tovisit.empty()) {
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

		for(int i = 0; i < numSuccessors; ++i) {
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
			if(found != rateMap.end()) {
				validate_rate(successors[i], rateMap);

				rate oldRate = found->second;
				y = boost::math::lcm(y, oldRate.y);
				if(y != oldRate.y) {
					uint64_t x = (y * produce * thisNodeRate.x) / (consume * thisNodeRate.y);
					rate newRate = {.y = y, .x = x};
					found->second = newRate;
					tovisit.insert(sname);
				}
			}
			else {
				uint64_t x = (y * produce * thisNodeRate.x) / (consume * thisNodeRate.y);
				rate newRate = {.y = y, .x = x};
				rateMap[sname] = newRate;
				tovisit.insert(sname);
			}
		}

		free(successors);
	}

	for(auto iter = rateMap.begin(), theEnd = rateMap.end();
		iter != theEnd;
		++iter) {
		node_t n;
		CheckError(pgm_find_node(&n, g, iter->first.c_str()));
		periods_ms[n] = us2ms((double)(iter->second.y)/iter->second.x);
	}
}

void parse_graph_exec(const std::string& execs, graph_t g, std::map<node_t, double, node_compare>& exec_ms)
{
	std::vector<std::string> nodeNames;
	boost::split(nodeNames, execs, boost::is_any_of(","));
	for(auto nStr(nodeNames.begin()); nStr != nodeNames.end(); ++nStr) {
		std::vector<std::string> nodeExecPair;
		boost::split(nodeExecPair, *nStr, boost::is_any_of(":"));

		if(nodeExecPair.size() != 2)
			throw std::runtime_error(std::string("Invalid execution time: " + *nStr));

		node_t n;
		CheckError(pgm_find_node(&n, g, nodeExecPair[0].c_str()));
		exec_ms[n] = boost::lexical_cast<double>(nodeExecPair[1]);
	}
}

void parse_graph_wss(const std::string& wss, graph_t g, std::map<edge_t, double, edge_compare>& wss_kb)
{
	if (wss.empty())
		return;

	std::vector<std::string> nodeNames;
	boost::split(nodeNames, wss, boost::is_any_of(","));
	for(auto nStr(nodeNames.begin()); nStr != nodeNames.end(); ++nStr) {
		std::vector<std::string> edgeWssDesc;
		boost::split(edgeWssDesc, *nStr, boost::is_any_of(":"));

		if(edgeWssDesc.size() != 3)
			throw std::runtime_error(std::string("Invalid working set size: " + *nStr));

		edge_t e;
		node_t p, c;

		CheckError(pgm_find_node(&p, g, edgeWssDesc[0].c_str()));
		CheckError(pgm_find_node(&c, g, edgeWssDesc[1].c_str()));
		CheckError(pgm_find_edge(&e, p, c,
			make_edge_name(edgeWssDesc[0], edgeWssDesc[1]).c_str()));
		wss_kb[e] = boost::lexical_cast<double>(edgeWssDesc[2])*1024;
	}
}

void parse_graph_cluster(const std::string& cluster, graph_t g, std::map<node_t, double, node_compare>& cluster_id)
{
	if (cluster.empty())
		return;

	std::vector<std::string> nodeNames;
	boost::split(nodeNames, cluster, boost::is_any_of(","));
	for(auto nStr(nodeNames.begin()); nStr != nodeNames.end(); ++nStr) {
		std::vector<std::string> nodeClusterPair;
		boost::split(nodeClusterPair, *nStr, boost::is_any_of(":"));

		if(nodeClusterPair.size() != 2)
			throw std::runtime_error(std::string("Invalid cluster ID: " + *nStr));

		node_t n;
		CheckError(pgm_find_node(&n, g, nodeClusterPair[0].c_str()));
		cluster_id[n] = boost::lexical_cast<double>(nodeClusterPair[1]);
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
		("cluster,c", program_options::value<std::string>()->default_value(""), "CPU assignment for each node [<name>:<cluster or CPU ID>,]")
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
		("wss,b", program_options::value<std::string>()->default_value(""),
			"Working set size requirements for edges. [<name>:<name>:<size>,]+ (size in kilobytes)")
		("wsCycle", program_options::value<int>()->default_value(1),
			"Number of working sets allocated to each node, which are cycled through on produce/consume.")
		("graphDir,d", program_options::value<std::string>()->default_value("/dev/shm/graphs"),
			"Directory to hold PGM FIFOs")
		("duration", program_options::value<double>()->default_value(-1), "Time to run (seconds).")
		("continuation", "Graph depends on a sub-graph of another process")
		;

	program_options::positional_options_description pos;
	pos.add("duration", -1);

	program_options::variables_map vm;

	try {
		program_options::store(program_options::command_line_parser(argc, argv).
						options(opts).positional(pos).run(), vm);
	}
	catch(program_options::required_option& e) {
		std::cerr<<"Error: "<<e.what()<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}
	catch(program_options::error& e) {
		std::cerr<<"Error: "<<e.what()<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}
	catch(...) {
		std::cerr<<"Unknown error."<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}

	rt_config cfg =
	{
		.syncRelease = (vm.count("wait") != 0),
		.cluster = -1,
		.clusterSize = vm["clusterSize"].as<int>(),
		.budget = (vm.count("budget") != 0),
		.period_ns = 0,
		.execution_ns = 0,
		.duration_ns = (uint64_t)s2ns(vm["duration"].as<double>())
	};

	int wsCycle = vm["wsCycle"].as<int>();

	std::string name = vm["name"].as<std::string>();
	std::string graphDir = vm["graphDir"].as<std::string>();
	int master = (vm.count("continuation") == 0);

	CheckError(pgm_init(graphDir.c_str(), master));

	graph_t g;
	std::vector<node_t> nodes;
	std::vector<edge_t> edges;
	std::map<node_t, double, node_compare> periods;
	std::map<node_t, double, node_compare> executions;
	std::map<edge_t, double, edge_compare> wss;
	std::map<node_t, double, node_compare> clusters;

	try {
		if(vm.count("graph") != 0) {
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
			parse_graph_exec(vm["execution"].as<std::string>(), g, executions);
			parse_graph_wss(vm["wss"].as<std::string>(), g, wss);
			parse_graph_cluster(vm["cluster"].as<std::string>(), g, clusters);

		}
		else if(vm.count("graphfile") != 0) {
			parse_graph_file(vm["graphfile"].as<std::string>(), g, nodes, edges);
		}
		else {
			throw std::runtime_error("Missing graph file or description");
		}
	}
	catch(std::exception& e) {
		std::cerr<<"Error: "<<e.what()<<std::endl;
		opts.print(std::cout);
		exit(-1);
	}

#ifdef USE_LITMUS
	init_litmus(); // prepare litmus
#endif

	// set up working sets for all edges
	for(auto ws(wss.begin()), theEnd(wss.end()); ws != theEnd; ++ws) {
		WorkingSet::edgeToWs[ws->first] = new WorkingSet(ws->second, wsCycle);
	}

	// spawn of a thread for each node in graph
	std::vector<std::thread> threads;
	for(auto iter(nodes.begin() + 1); iter != nodes.end(); ++iter) {
		rt_config nodeCfg = cfg;
		nodeCfg.cluster = clusters[*iter];
		nodeCfg.node = *iter;
		nodeCfg.period_ns = ms2ns(periods[*iter]);
		nodeCfg.execution_ns = ms2ns(executions[*iter]);
		threads.push_back(std::thread(work_thread, nodeCfg));
	}

	// main thread handles first node
	cfg.cluster = clusters[nodes[0]];
	cfg.node = nodes[0];
	cfg.period_ns = ms2ns(periods[nodes[0]]);
	cfg.execution_ns = ms2ns(executions[nodes[0]]);
	work_thread(cfg);

	for(auto t(threads.begin()); t != threads.end(); ++t) {
		t->join();
	}

	for(auto ws(WorkingSet::edgeToWs.begin()), theEnd(WorkingSet::edgeToWs.end());
		ws != theEnd;
		++ws) {
		delete ws->second;
	}

	CheckError(pgm_destroy_graph(g));
	return 0;
}
