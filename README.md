PGM^RT
======

A soft real-time implementation of the Processing Graph Method (PGM) on top of LITMUS^RT

Overview
========

This project aims to create a soft real-time implementation of the U.S. Navy's
Processing Graph Method (PGM). It can run on any POSIX-compatible system, but PGM^RT
also includes optimizations to run on top of LITMUS^RT---a real-time patch to the
Linux kernel [1].

Recent work by C. Liu and J. Anderson developed methods for global scheduling
of graph-based real-time applications on multiprocessors [2]. They showed
that soft real-time guarantees (bounded deadline tardiness) can be made with
no utilization loss. That is, a multiprocessor system can be fully loaded
with work while ensuring that no processing deadline is missed by more than
a bounded (predictable) amount.

Graph-based workloads can be found in pipelined software architectures. Such
architectures are very common to multimedia processing. For example, image and
audio encoding/decoding are often processed by a series of filters. These
pipelines often split into parallel pipelines only to be joined at a later
stage. Therefore, general graph-based support is necessary instead of a
serial pipeline model. Soft real-time guarantees for these workloads are
clearly desirable, but are not realized due to a lack of operating system
support.

[1] LITMUS^RT: [http://www.litmus-rt.org](http://www.litmus-rt.org)

on github: [https://github.com/LITMUS-RT/litmus-rt](https://github.com/LITMUS-RT/litmus-rt)

[2] C. Liu and J. H. Anderson, "Supporting Soft Real-Time DAG-based Systems on
Multiprocessors with No Utilization Loss", Proceedings of the 31st IEEE Real-Time
Systems Symposium, pp. 3-13, December 2010.

available here: [http://www.cs.unc.edu/~anderson/papers/rtss10b.pdf](http://www.cs.unc.edu/~anderson/papers/rtss10b.pdf)

Compilation
===========

PGM^RT supports POSIX-compatible systems. However, better performance for
real-time scheduling can be achieved by using the LITMUS^RT Linux kernel.

To enable LITMUS^RT support, uncomment 'flags-litmus' line in the Makefile.

You must patch LITMUS^RT (version 2013.1) to support PGM^RT. Source
code is available here: [http://wiki.litmus-rt.org/litmus/Publications](http://wiki.litmus-rt.org/litmus/Publications)

Publications
============
G. Elliott, N. Kim, J. Erickson, C. Liu, and J. Anderson, "Response-Time
Minimization of Automotive-Inspired Dataflows on Multicore Platforms",
Proceedings of the 20th IEEE International Conference on Embedded and Real-Time
Computing Systems and Applications, August 2014.

available here: [http://www.cs.unc.edu/~anderson/papers/rtcsa14b_long.pdf](http://www.cs.unc.edu/~anderson/papers/rtcsa14b_long.pdf)

License
=======

PGM^RT is released under the Revised BSD License:

Copyright (c) 2014, Glenn Elliott
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of the PGM^RT nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL GLENN ELLIOTT BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
