#pragma once

#define PGM_CONFIG

/*
 CONDITIONAL COMPILATION:    PGM_USE_OPTIMIZED_MSG_PASSING

 Use pthread_mutex and condition variables to signal between nodes when a node
 should have sufficient tokens to run. Token counters and synchronization
 variables are stored in shared memory.

 Note: This option is not required if USE_FIFOS is asserted. However, this may
 cause tasks to have to incrementally read tokens from FIFOs as they become
 available.
 */
#define PGM_USE_OPTIMIZED_MSG_PASSING

/*
 CONDITIONAL COMPILATION:    PGM_USE_PTHREAD_SYNC
 PRE-COND: defined(PGM_USE_OPTIMIZED_MSG_PASSING)

 Use pthread's sleeping-mutex and condition variables
 for token signalling. Custom spinlocks and condition
 variables are used if this option is disabled.
 */
/*#define PGM_USE_PTHREAD_SYNC*/

/*
 CONDITIONAL COMPILATION:    PGM_INTERRUPT_DISABLING_LOCKS
 PRE-COND: !defined(PGM_USE_OPTIMIZED_MSG_PASSING)

 Disable interrupts while PGM-internal spinlocks are held.

 Note: Your program MUST run as root. Also, a crash while
 running with interrupts disabled may ruin your day (may
 need to hard-reboot).
 */
/*#define PGM_INTERRUPT_DISABLING_LOCKS*/

/*
 CONDITIONAL COMPILATION:    PGM_PRIVATE
 PRE-COND: defined(PGM_USE_OPTIMIZED_MSG_PASSING)

 Optimizes performance for graphs used by a single
 process/address space.

 Note: Enabling this option will BREAK graphs split
 across processes.
*/
#define PGM_PRIVATE

/*
 CONDITIONAL COMPILATION:    PGM_USE_FIFOS

 Transmit tokens via Unix FIFOs. Each byte represents one token.

 Note: This option is relatively useless while termination relies upon the
 TERMINATE token. Once the TERMINATE token is no longer needed, then the user
 should be able to transmit arbitrary bytes.

 TODO: Develop out-of-band method to signal termination that can work via
 both FIFOs and sockets. (The goal is to allow sockets for network-based PGM.
 FIFO support is a small step in this direction.) Of course, some sort of
 network discovery protocol will also be necessary to replace shared memory
 segments found in this current implementation.
 */
/*#define PGM_USE_FIFOS*/


/*** VALIDATE CONFGURATION ***/

/*
 PGM_USE_OPTIMIZED_MSG_PASSING and PGM_USE_FIFOS can be combined, but at
 least one must be asserted.
 */
#if !defined(PGM_USE_FIFOS) && !defined(PGM_USE_OPTIMIZED_MSG_PASSING)
#error "Cannot disable FIFOs without optimized message passing!"
#endif

#if defined(PGM_USE_PTHREAD_SYNC) && defined(PGM_INTERRUPT_DISABLING_LOCKS)
#error "Cannot disable interrupts while using pthread sleep-locks."
#endif

#if defined(PGM_INTERRUPT_DISABLING_LOCKS) && defined(PGM_USE_FIFOS)
#warn "Beware of blocking reads/writes while interrupts are disabled."
#endif

#if defined(PGM_PRIVATE)   && \
	defined(PGM_USE_FIFOS) && \
   !defined(PGM_USE_OPTIMIZED_MSG_PASSING)
#warn "PGM_PRIVATE does not make sense without PGM_USE_OPTIMIZED_MSG_PASSING!"
#endif
