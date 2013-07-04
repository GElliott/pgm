pgm
===

A soft real-time implementation of the Processing Graph Method (PGM) on top of Litmus^RT

Released as open source under the germs of the GNU General Public License (GPL2).

Overview
========

This project aims to create a soft real-time implementation of the U.S. Navy's
Processing Graph Method (PGM) on top of Litmus^RT---a real-time patch to the
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

[1] Litmus^RT: http://www.litmus-rt.org
--on github: https://github.com/LITMUS-RT/litmus-rt

[2] C. Liu and J. H. Anderson, "Supporting Soft Real-Time DAG-based Systems on
Multiprocessors with No Utilization Loss", Proceedings of the 31st IEEE Real-Time
Systems Symposium, pp. 3-13, December 2010.
--available here: http://www.cs.unc.edu/~anderson/papers/rtss10b.pdf
