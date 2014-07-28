// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

/* A program for running a complex PGM application. */

#include <iostream>
#include <thread>
#include <exception>
#include <stdexcept>
#include <vector>
#include <map>
#include <cassert>
#include <cstdint>

#include <errno.h>
#include <string.h>

// TODO: Use std::chrono routines instead.
#include <sys/time.h>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/math/common_factor.hpp>

#include "pgm.h"

#ifdef _USE_LITMUS
#include "litmus.h"
#else
// some useful routines from litmus that are useful
// without using litmus all together.
#define s2ns(s)   ((s)*1000000000LL)
#define s2us(s)   ((s)*1000000LL)
#define s2ms(s)   ((s)*1000LL)
#define ms2ns(ms) ((ms)*1000000LL)
#define ms2us(ms) ((ms)*1000LL)
#define us2ns(us) ((us)*1000LL)
#define ns2s(ns)  ((ns)/1000000000LL)
#define ns2ms(ns) ((ns)/1000000LL)
#define ns2us(ns) ((ns)/1000LL)
#define us2ms(us) ((us)/1000LL)
#define us2s(us)  ((us)/1000000LL)
#define ms2s(ms)  ((ms)/1000LL)
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <unistd.h>
inline pid_t gettid(void)
{
	return syscall(__NR_gettid);
}
#endif

//#define VERBOSE

using namespace boost;

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

// macro to boost priority of tasks waiting for tokens
// when compiled for Litmus.
#ifdef _USE_LITMUS
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
#define T(...) do { fprintf(stdout, __VA_ARGS__); fflush(stdout); } while (0)
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
	int budget;

	uint64_t phase_ns;
	uint64_t period_ns;
	uint64_t execution_ns;
	uint64_t discount_ns;
	uint64_t loop_for_ns;
	int      split_factor;

	uint64_t expected_etoe;

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

uint64_t loop_for(WorkingSet** ws, int numWs, int jobNum, uint64_t exec_time, uint64_t emergency_exit)
{
	const uint64_t CHUNK_SIZE = (4*1024)/sizeof(WorkingSet::chunk_t);
	uint64_t start = cputime_ns();
	WorkingSet::chunk_t tmp = 0;
	WorkingSet::chunk_t *p, *e;
	int curWs = 0;
	uint64_t lastLoop = 0;
	uint64_t now = start;
	uint64_t loopStart;

	/* Make sure we note the current time before initialization. */
	__sync_synchronize();

	if(numWs > 0) {
		/* Cast away volatile because it's now okay to store these
		   values in registers after the initial load. */
		p = const_cast<WorkingSet::chunk_t*>(ws[curWs]->start(jobNum));
		e = const_cast<WorkingSet::chunk_t*>(ws[curWs]->end(jobNum));
	}
	else {
		p = NULL;
		e = NULL;
	}

	while (now + lastLoop < start + exec_time)
	{
		loopStart = now;

		if(numWs > 0) {
			for(uint64_t count = 0; count < CHUNK_SIZE; ++count) {
				tmp += *p++;
				if(p == e) {
					/* We're done with this edge. Move on to the next. */
					curWs = (curWs+1 == numWs) ? 0 : curWs+1;
					p = const_cast<WorkingSet::chunk_t*>(ws[curWs]->start(jobNum));
					e = const_cast<WorkingSet::chunk_t*>(ws[curWs]->end(jobNum));
				}
			}
		}
		else {
			tmp += loop_once();
		}

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
	if (unlikely(programEnd && wctime_ns() > programEnd)) {
		keepGoing = false;
	}
	else {
		WorkingSet::chunk_t temp = jobNum;

		// let overrun by a second
		uint64_t emergency_exit = (programEnd) ? programEnd + s2ns(1) : 0;

		// consume data from predecessors
		if(!consumeList.empty())
			temp = consume(&consumeList[0], consumeList.size(), jobNum);

		// execution time
		try {
			(void) loop_for(&consumeList[0], consumeList.size(), jobNum, cfg.loop_for_ns, emergency_exit);
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


pthread_barrier_t worker_exit_barrier;

void work_thread(rt_config cfg)
{
	int ret = 0;
	int degree_in = pgm_get_degree_in(cfg.node);
	int degree_out = pgm_get_degree_out(cfg.node);
	bool isSrc = (degree_in == 0);

	// claim the node and open up FIFOs, etc.
	CheckError(pgm_claim_node(cfg.node));

	// how long should we loop, accounting for time spent reading/writing
	if (cfg.execution_ns < cfg.discount_ns)
	{
		T("!!!WARNING!!! %d: read/write discount execeeds execution time.\n", pgm_get_name(cfg.node));
		cfg.loop_for_ns = 0;
	}
	else
	{
		cfg.loop_for_ns = cfg.execution_ns - cfg.discount_ns;
	}

	std::vector<WorkingSet*> consumeWs, produceWs;

	// get pointers to the working sets attached to cfg.node
	{
		edge_t* edges = (edge_t*)calloc(degree_in, sizeof(edge_t));
		int numEdges;

		edges = NULL;
		numEdges = 0;
		CheckError(pgm_get_edges_in(cfg.node, edges, numEdges));
		for(int i = 0; i < numEdges; ++i) {
			auto edgeWithWs = WorkingSet::edgeToWs.find(edges[i]);
			if(edgeWithWs != WorkingSet::edgeToWs.end()) {
				WorkingSet* ws = edgeWithWs->second;
				consumeWs.push_back(ws);
			}
		}
		free(edges);
		T("%s has %d in-edges with working sets\n", pgm_get_name(cfg.node), (int)consumeWs.size());

		edges = (edge_t*)calloc(degree_out, sizeof(edge_t));
		numEdges = 0;
		CheckError(pgm_get_edges_out(cfg.node, edges, numEdges));
		for(int i = 0; i < numEdges; ++i) {
			auto edgeWithWs = WorkingSet::edgeToWs.find(edges[i]);
			if(edgeWithWs != WorkingSet::edgeToWs.end()) {
				WorkingSet* ws = edgeWithWs->second;
				produceWs.push_back(ws);
			}
		}
		free(edges);
		T("%s has %d out-edges with working sets\n", pgm_get_name(cfg.node), (int)produceWs.size());
	}

#ifdef _USE_LITMUS
	bool isSink = (degree_out == 0);

	// become a real-time task
	struct rt_task param;
	init_rt_task_param(&param);
//	param.phase = cfg.phase_ns;
	param.period = cfg.period_ns;
	param.exec_cost = cfg.execution_ns;
	param.split = cfg.split_factor;
	if(cfg.cluster >= 0)
		param.cpu = domain_to_first_cpu(cfg.cluster);
	param.budget_policy = (cfg.budget) ? PRECISE_ENFORCEMENT : NO_ENFORCEMENT;
	param.release_policy = (isSrc) ? TASK_PERIODIC : TASK_EARLY;

	if (isSrc && isSink)
		param.pgm_type = PGM_SRC_SINK;
	else if(isSrc)
		param.pgm_type = PGM_SRC;
	else if(isSink)
		param.pgm_type = PGM_SINK;
	else
		param.pgm_type = PGM_INTERNAL;

	param.pgm_expected_etoe = cfg.expected_etoe;

	if(cfg.cluster >= 0) {
		// Set our CPU affinity mask to put us on our cluster's
		// CPUs. This must be done prior to entering real-time mode.
		ret = be_migrate_to_domain(cfg.cluster);
		assert(ret == 0);
	}
	ret = set_rt_task_param(gettid(), &param);
	assert(ret >= 0);

	ret = task_mode(LITMUS_RT_TASK);
	assert(ret == 0);

	if(isSrc)
		T("(i) %s is a src\n", pgm_get_name(cfg.node));
	if(isSink)
		T("(i) %s is a sink\n", pgm_get_name(cfg.node));

	T("(i) %s exec: %lu\n", pgm_get_name(cfg.node), cfg.execution_ns);

	if(cfg.syncRelease) {
		T("(x) %s waiting for release\n", pgm_get_name(cfg.node));
		ret = wait_for_ts_release();
		assert(ret == 0);
	}
#else
	uint64_t release_ns, response_ns;

	// approximate a phased release
	sleep_ns(cfg.phase_ns);
#endif

	uint64_t bailoutTime = (isSrc) ? wctime_ns() + cfg.duration_ns : 0;

	int count = 0;
	bool keepGoing;
	do {
		// We become non-preemptive/boosted when we call
		// pgm_wait() to ensure BOUNDED priority inversions.
		//
		// Note: We can remove this once the waiting mechanism
		// has been pushed down into the OS kernel.
		if(!isSrc) {
			T("(x) %s waits for tokens\n", pgm_get_name(cfg.node));
			litmus_pgm_wait(ret = pgm_wait(cfg.node););
		}

#ifndef _USE_LITMUS
		// Approximate the release time. May slip due to unbounded priority
		// inversions or scheduler delays.
		release_ns = wctime_ns();
#endif

		if(ret != PGM_TERMINATE) {
			CheckError(ret);

			// do job
			T("(x) %s starts work @ %lu.\n", pgm_get_name(cfg.node), cputime_ns());
			keepGoing = job(cfg, count++, consumeWs, produceWs, bailoutTime);
			T("(x) %s   ends work @ %lu.\n", pgm_get_name(cfg.node), cputime_ns());

			// only allow roots to trigger a graph-wide bailout
			if(isSrc && !keepGoing) {
				CheckError(pgm_terminate(cfg.node));
				break;
			}
			else {
				T("(+) %s fired for %d time\n", pgm_get_name(cfg.node), count);

				litmus_pgm_complete(CheckError(pgm_complete(cfg.node)););

#ifdef _USE_LITMUS
				sleep_next_period();
#else
				response_ns = wctime_ns() - release_ns;

				// Sources sleep until the next "periodic" release.
				if(isSrc && (response_ns < cfg.period_ns)) {
					uint64_t slack = cfg.period_ns - response_ns;
					sleep_ns(slack);
				}
#endif
			}
		}
	} while(ret != PGM_TERMINATE);

	T("(-) %s terminates\n", pgm_get_name(cfg.node));

#ifdef _USE_LITMUS
	task_mode(BACKGROUND_TASK);
#endif

	pthread_barrier_wait(&worker_exit_barrier);

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
		edge_attr_t cv_attr;
		memset(&cv_attr, 0, sizeof(cv_attr));
		cv_attr.type = pgm_cv_edge;
		cv_attr.nr_produce = nr_produce;
		cv_attr.nr_consume = nr_consume;
		cv_attr.nr_threshold = nr_threshold;
		CheckError(pgm_init_edge(&e, nodeMap[nodePair[0]], nodeMap[nodePair[1]],
				make_edge_name(nodePair[0], nodePair[1]).c_str(), &cv_attr));
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

	int nr_preds = pgm_get_degree_in(n);
	node_t *preds = (node_t*)calloc(nr_preds, sizeof(node_t));
	CheckError(pgm_get_predecessors(n, preds, nr_preds));

	uint64_t scale = 1;
	std::vector<std::pair<node_t, rate> > preds_w_rates;
	preds_w_rates.reserve(nr_preds);
	for(int i = 0; i < nr_preds; ++i) {
		auto p = rates.find(std::string(pgm_get_name(preds[i])));
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
			make_edge_name(std::string(pgm_get_name(preds_w_rates[i-1].first)), std::string(pgm_get_name(n))).c_str()));
		CheckError(pgm_find_edge(&e_cur, preds_w_rates[i].first, n,
			make_edge_name(std::string(pgm_get_name(preds_w_rates[i].first)), std::string(pgm_get_name(n))).c_str()));

		rate a = {pgm_get_nr_produce(e_prev) * prev.x * scale, prev.y * pgm_get_nr_consume(e_prev)};
		rate b = {pgm_get_nr_produce(e_cur) * cur.x * scale, cur.y * pgm_get_nr_consume(e_cur)};

		uint64_t p1 = (pgm_get_nr_produce(e_prev) * prev.x*(scale/prev.y)) / pgm_get_nr_consume(e_prev);
		uint64_t p2 = (pgm_get_nr_produce(e_cur) * cur.x*(scale/cur.y)) / pgm_get_nr_consume(e_cur);

		bool __valid = (a == b);

		if(!__valid) {
			printf("%s (%lu = %lu * (%lu / %lu)) "
				  "and %s (%lu = %lu * (%lu / %lu)) incompatible\n",
				  pgm_get_name(preds_w_rates[i-1].first),
				  p1, prev.x, scale, prev.y,
				  pgm_get_name(preds_w_rates[i].first),
				  p2, cur.x, scale, cur.y
				  );
			valid = __valid;
		}
	}

	if(valid) {
		T("%s has valid predecessor execution rates\n", pgm_get_name(n));
	}
	else {
		printf("%s has INvalid predecessor execution rates!!!\n", pgm_get_name(n));
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

		int numSuccessors = pgm_get_degree_out(n);
		node_t* successors = (node_t*)calloc(numSuccessors, sizeof(node_t));
		CheckError(pgm_get_successors(n, successors, numSuccessors));
		if(numSuccessors == 0)
			continue;

		for(int i = 0; i < numSuccessors; ++i) {
			const std::string sname(pgm_get_name(successors[i]));
			assert(!sname.empty());

			edge_t e;
			CheckError(pgm_find_edge(&e, n, successors[i],
				make_edge_name(std::string(pgm_get_name(n)), sname).c_str()));

			int produce, consume;
			produce = pgm_get_nr_produce(e);
			CheckError((produce < 1) ? -1 : 0);
			consume = pgm_get_nr_consume(e);
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
	if (execs.empty())
		return;

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

void parse_graph_split_factor(const std::string& splits, graph_t g, std::map<node_t, int, node_compare>& split_factors)
{
	if (splits.empty())
		return;

	std::vector<std::string> nodeNames;
	boost::split(nodeNames, splits, boost::is_any_of(","));
	for(auto nStr(nodeNames.begin()); nStr != nodeNames.end(); ++nStr) {
		std::vector<std::string> nodeSplitPair;
		boost::split(nodeSplitPair, *nStr, boost::is_any_of(":"));
		if (nodeSplitPair.size() != 2)
			throw std::runtime_error(std::string("Invalid split factor: " + *nStr));

		node_t n;
		int sf;

		CheckError(pgm_find_node(&n, g, nodeSplitPair[0].c_str()));

		sf = boost::lexical_cast<int>(nodeSplitPair[1]);
		if (sf < 0)
			throw std::runtime_error(std::string("Invalid split factor: " + *nStr));
		split_factors[n] = sf;
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

double producer_period(edge_t edge, void* user)
{
	const std::map<node_t, double, node_compare>& periods = *(std::map<node_t, double, node_compare>*)(user);
	node_t producer = pgm_get_producer(edge);
	int nr_thresh = pgm_get_nr_threshold(edge);
	int nr_produce = pgm_get_nr_produce(edge);
	int jobs_to_thresh = nr_thresh/nr_produce + (nr_thresh%nr_produce != 0);

	std::map<node_t, double, node_compare>::const_iterator search = periods.find(producer);
	assert(search != periods.end());
	return search->second * jobs_to_thresh;
}

int main(int argc, char** argv)
{
	program_options::options_description opts("Options");
	opts.add_options()
		("wait,w", "Wait for release")
		("cluster,c", program_options::value<std::string>()->default_value(""), "CPU assignment for each node [<name>:<cluster or CPU ID>,]")
		("clusterSize,z", program_options::value<int>()->default_value(1), "Cluster size (ignored)")
		("enforce,e", "Enable budget enforcement")
		("graphfile", program_options::value<std::string>(), "File that describes PGM graph")
		("name,n", program_options::value<std::string>()->default_value(""), "Graph name")
		("graph,g", program_options::value<std::string>(),
		 	"Graph edge description: [<name>[.produce]:<name>[.consume[.threshld]],]+ (do '<name>:' for single-node graph)")
		("rates,r", program_options::value<std::string>(),
		 	"Arrivial rates: [<name>:<#>:<interval>,]+ (interval in ms) (only for source nodes)")
		("execution,x", program_options::value<std::string>(),
		 	"Execution time requirements for nodes. [<name>:<time>,]+ (time in ms)")
		("discount", program_options::value<std::string>()->default_value(""),
		 	"Execution time DISCOUNT for nodes. [<name>:<time>,]+ (time in ms)")
		("etoe", program_options::value<double>()->default_value(0.0), "Longest-path sum of periods in graph.")
		("wss,b", program_options::value<std::string>()->default_value(""),
			"Working set size requirements for edges. [<name>:<name>:<size>,]+ (size in kilobytes)")
		("split,v", program_options::value<std::string>()->default_value(""),
		 	"Job split factor of task for split-supporting schedulers [<name>:<split_factor>,]+. (default: 1)")
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

	// TODO: If "etoe" was not given, then compute it from the graph structure

	rt_config cfg =
	{
		.syncRelease = (vm.count("wait") != 0),
		.cluster = -1,
		.budget = (vm.count("budget") != 0),
		.phase_ns = 0,
		.period_ns = 0,
		.execution_ns = 0,
		.discount_ns = 0,
		.loop_for_ns = 0,
		.split_factor = 1,
		.expected_etoe = (uint64_t)ms2ns(vm["etoe"].as<double>()),
		.duration_ns = (uint64_t)s2ns(vm["duration"].as<double>())
	};

	int wsCycle = vm["wsCycle"].as<int>();
	std::string name = vm["name"].as<std::string>();
	std::string graphDir = vm["graphDir"].as<std::string>();
	int master = (vm.count("continuation") == 0);

	if(name != "") {
		graphDir += name;
	}
	else {
		char pidStr[PGM_GRAPH_NAME_LEN];
		snprintf(pidStr, PGM_GRAPH_NAME_LEN, "%x", getpid());
		graphDir += std::string("_") + std::string(pidStr);
	}

	CheckError(pgm_init(graphDir.c_str(), master));

	graph_t g;
	std::vector<node_t> nodes;
	std::vector<edge_t> edges;
	std::map<node_t, double, node_compare> periods;
	std::map<node_t, double, node_compare> executions;
	std::map<node_t, double, node_compare> discounts;
	std::map<node_t, int,    node_compare> split_factors;
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
			parse_graph_exec(vm["discount"].as<std::string>(), g, discounts);
			parse_graph_split_factor(vm["split"].as<std::string>(), g, split_factors);
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

#ifdef _USE_LITMUS
	init_litmus(); // prepare litmus
#endif

	// set up working sets for all edges
	for(auto ws(wss.begin()), theEnd(wss.end()); ws != theEnd; ++ws) {
		WorkingSet::edgeToWs[ws->first] = new WorkingSet(ws->second, wsCycle);
	}

	pthread_barrier_init(&worker_exit_barrier, NULL, nodes.size());
	// spawn of a thread for each node in graph
	std::vector<std::thread> threads;
	for(auto iter(nodes.begin() + 1); iter != nodes.end(); ++iter) {
		rt_config nodeCfg = cfg;
		nodeCfg.cluster = clusters[*iter];
		nodeCfg.node = *iter;
		nodeCfg.phase_ns = ms2ns(pgm_get_max_depth(*iter, producer_period, &periods));
		nodeCfg.period_ns = ms2ns(periods[*iter]);
		nodeCfg.execution_ns = ms2ns(executions[*iter]);
		nodeCfg.discount_ns = ms2ns(discounts[*iter]);

		if(split_factors.find(*iter) != split_factors.end())
			nodeCfg.split_factor = split_factors[*iter];

		threads.push_back(std::thread(work_thread, nodeCfg));
	}

	// main thread handles first node
	cfg.cluster = clusters[nodes[0]];
	cfg.node = nodes[0];
	cfg.phase_ns = ms2ns(pgm_get_max_depth(nodes[0], producer_period, &periods));
	cfg.period_ns = ms2ns(periods[nodes[0]]);
	cfg.execution_ns = ms2ns(executions[nodes[0]]);
	cfg.discount_ns = ms2ns(discounts[nodes[0]]);
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
