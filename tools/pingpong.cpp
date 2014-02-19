// Copyright (c) 2014, Glenn Elliott
// All rights reserved.

// Quick and dirty tool to investigate the signalling latencies
// of FIFO queues and POSIX Message Queues.
//
// Need to run as root to get accurate measurements (must run
// as SCHED_FIFO).

#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <cstring>

#include <boost/thread.hpp>

#include <unistd.h>
#include <mqueue.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;
using namespace std::chrono;

int counter = 0;
int thresh = 1000;

int rtprio = sched_get_priority_max(SCHED_FIFO)/2;

struct Args
{
	bool is_master;
	string* fifo_names;
	string* mq_names;
	boost::barrier* b;
};

void fifo_worker(Args args)
{
	int fd_out;
	int fd_in;
	uint8_t token = 0;

	if(args.is_master)
	{
		fd_out = open(args.fifo_names[!args.is_master].c_str(), O_WRONLY);
		fd_in = open(args.fifo_names[args.is_master].c_str(), O_RDONLY | O_NONBLOCK);
	}
	else
	{
		fd_in = open(args.fifo_names[args.is_master].c_str(), O_RDONLY | O_NONBLOCK);
		fd_out = open(args.fifo_names[!args.is_master].c_str(), O_WRONLY);
	}

	assert(fd_in != -1 && fd_out != -1);

	{
		struct sched_param param;
		memset(&param, 0, sizeof(param));
		param.sched_priority = rtprio;
		sched_setscheduler(0 /* self */, SCHED_FIFO, &param);

		cpu_set_t* cpu_set = CPU_ALLOC(sysconf(_SC_NPROCESSORS_ONLN));
		size_t cpu_set_sz = CPU_ALLOC_SIZE(sysconf(_SC_NPROCESSORS_ONLN));
		CPU_ZERO_S(cpu_set_sz, cpu_set);
		CPU_SET_S(args.is_master, cpu_set_sz, cpu_set);
		sched_setaffinity(0 /* self */, cpu_set_sz, cpu_set);
	}

	args.b->wait();

	auto start = high_resolution_clock::now();
	__sync_synchronize();

	for(int i = 0; i < thresh; ++i)
	{
		bool signalled = false;
		if(args.is_master)
		{
			++token;
			ssize_t ret = write(fd_out, &token, sizeof(token));
			assert(ret != -1);
			while(!signalled)
			{
				ssize_t bread = read(fd_in, &token, sizeof(token));
				signalled = (bread != -1 && bread != 0);
			}
		}
		else
		{
			while(!signalled)
			{
				ssize_t bread = read(fd_in, &token, sizeof(token));
				signalled = (bread != -1 && bread != 0);
			}
			++token;
			ssize_t ret = write(fd_out, &token, sizeof(token));
			assert(ret != -1);
		}
	}

	__sync_synchronize();
	auto end = high_resolution_clock::now();

	uint64_t elapsed = duration_cast<nanoseconds>(end - start).count();
	uint64_t avg_round = elapsed / thresh;

	string name = (args.is_master) ? "A" : "B";

	printf("(fifo) %s: time: %lu (ns)   average rtt: %lu (ns)   token value: %d\n", name.c_str(), elapsed, avg_round, (int)token);
}

void mq_worker(Args args)
{
	mqd_t fd_out;
	mqd_t fd_in;
	uint8_t token = 0;

	mq_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.mq_flags = O_NONBLOCK;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(token);

	mode_t mode  = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

	if(args.is_master)
	{
		fd_out = mq_open(args.mq_names[!args.is_master].c_str(), O_WRONLY | O_CREAT, mode, &attr);
		if(fd_out == -1) perror("bad open for writing");
		args.b->wait();

		fd_in = mq_open(args.mq_names[args.is_master].c_str(), O_RDONLY | O_CREAT, mode, &attr);
		if(fd_in == -1) perror("bad open for reading");
		args.b->wait();

		mq_unlink(args.mq_names[!args.is_master].c_str());
	}
	else
	{
		fd_out = mq_open(args.mq_names[!args.is_master].c_str(), O_WRONLY | O_CREAT, mode, &attr);
		if(fd_out == -1) perror("bad open for writing");
		args.b->wait();

		fd_in = mq_open(args.mq_names[args.is_master].c_str(), O_RDONLY | O_CREAT, mode, &attr);
		if(fd_in == -1) perror("bad open for reading");
		args.b->wait();

		mq_unlink(args.mq_names[!args.is_master].c_str());
	}

	assert(fd_in != -1 && fd_out != -1);

	{
		struct sched_param param;
		memset(&param, 0, sizeof(param));
		param.sched_priority = rtprio;
		sched_setscheduler(0 /* self */, SCHED_FIFO, &param);

		cpu_set_t* cpu_set = CPU_ALLOC(sysconf(_SC_NPROCESSORS_ONLN));
		size_t cpu_set_sz = CPU_ALLOC_SIZE(sysconf(_SC_NPROCESSORS_ONLN));
		CPU_ZERO_S(cpu_set_sz, cpu_set);
		CPU_SET_S(args.is_master, cpu_set_sz, cpu_set);
		sched_setaffinity(0 /* self */, cpu_set_sz, cpu_set);
	}

	args.b->wait();

	auto start = high_resolution_clock::now();
	__sync_synchronize();

	for(int i = 0; i < thresh; ++i)
	{
		bool signalled = false;
		if(args.is_master)
		{
			++token;
			ssize_t ret = mq_send(fd_out, (char*)&token, sizeof(token), rtprio);
			assert(ret != -1);
			while(!signalled)
			{
				ssize_t bread = mq_receive(fd_in, (char*)&token, sizeof(token), NULL);
				signalled = (bread != -1 && bread != 0);
			}
		}
		else
		{
			while(!signalled)
			{
				ssize_t bread = mq_receive(fd_in, (char*)&token, sizeof(token), NULL);
				signalled = (bread != -1 && bread != 0);
			}
			++token;
			ssize_t ret = mq_send(fd_out, (char*)&token, sizeof(token), rtprio);
			assert(ret != -1);
		}
	}

	__sync_synchronize();
	auto end = high_resolution_clock::now();

	uint64_t elapsed = duration_cast<nanoseconds>(end - start).count();
	uint64_t avg_round = elapsed / thresh;

	string name = (args.is_master) ? "A" : "B";

	printf("(mq) %s: time: %lu (ns)   average rtt: %lu (ns)   token value: %d\n", name.c_str(), elapsed, avg_round, (int)token);
}

int main(int argc, char** argv)
{
	if(argc != 2)
	{
		fprintf(stderr, "usage: pingpong <count>\n");
		exit(-1);
	}

	thresh = atoi(argv[1]);

	string fifoNames[] = { string("/tmp/fifo_atob"), string("/tmp/fifo_btoa") };
	string mqNames[] = { string("/mq_atob"), string("/mq_btoa") };

	mkfifo(fifoNames[0].c_str(), S_IRUSR | S_IWUSR);
	mkfifo(fifoNames[1].c_str(), S_IRUSR | S_IWUSR);

	boost::barrier syncbar(2);

	Args a = { .is_master = true, .fifo_names = fifoNames, .mq_names = mqNames, .b = &syncbar };
	Args b = { .is_master = false, .fifo_names = fifoNames, .mq_names = mqNames, .b = &syncbar };

	thread fifoWorkers[] = {
		thread(fifo_worker, a),
		thread(fifo_worker, b)
	};

	fifoWorkers[0].join();
	fifoWorkers[1].join();

	unlink(fifoNames[0].c_str());
	unlink(fifoNames[1].c_str());

	thread mqWorkers[] = {
		thread(mq_worker, a),
		thread(mq_worker, b)
	};

	mqWorkers[0].join();
	mqWorkers[1].join();
}

