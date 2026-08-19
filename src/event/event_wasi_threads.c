/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#include "internal.h"
#if DISPATCH_EVENT_BACKEND_WASI_THREADS

// Experimental event backend for wasm32-unknown-wasip1-threads.
//
// Threaded WASI has real worker threads, so the queue machinery runs the
// upstream shape: pokes wake workers, blocking waits block. What it does not
// have is any poll wakeup object (no eventfd, no self-pipe: wasip1 cannot
// create pipes or sockets), so the manager thread parks on a pthread
// condition variable instead of a poller. That supports timers and manager
// work. File-descriptor and signal event sources need a bounded-slice
// poll_oneoff thread and are not implemented yet; registering one crashes
// with a named message.

#include <pthread.h>
#include <time.h>
#include <errno.h>

#if !DISPATCH_USE_MGR_THREAD
#error unsupported configuration
#endif

#if !defined(_REENTRANT)
#error this backend requires the wasip1-threads target (-pthread)
#endif

typedef struct dispatch_wasi_threads_timeout_s {
	uint64_t dwt_deadline; // uptime-anchored, nanoseconds
	bool dwt_armed;
} dispatch_wasi_threads_timeout_s;

static dispatch_wasi_threads_timeout_s
		_dispatch_wasi_threads_timeout[DISPATCH_CLOCK_COUNT];

static pthread_mutex_t _dispatch_wasi_threads_mgr_mutex =
		PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _dispatch_wasi_threads_mgr_cond;
static bool _dispatch_wasi_threads_mgr_poked;
static dispatch_once_t _dispatch_wasi_threads_init_pred;

static void
_dispatch_wasi_threads_init(void *context DISPATCH_UNUSED)
{
	pthread_condattr_t attr;
	(void)dispatch_assume_zero(pthread_condattr_init(&attr));
	(void)dispatch_assume_zero(
			pthread_condattr_setclock(&attr, CLOCK_MONOTONIC));
	(void)dispatch_assume_zero(
			pthread_cond_init(&_dispatch_wasi_threads_mgr_cond, &attr));
	(void)dispatch_assume_zero(pthread_condattr_destroy(&attr));

	// hand the manager queue to its root queue so the manager thread spawns
	_dispatch_trace_item_push(_dispatch_mgr_q.do_targetq, &_dispatch_mgr_q);
	dx_push(_dispatch_mgr_q.do_targetq, &_dispatch_mgr_q, 0);
}

#pragma mark unotes

bool
_dispatch_unote_register_muxed(dispatch_unote_t du DISPATCH_UNUSED)
{
	DISPATCH_CLIENT_CRASH(0, "file-descriptor and signal dispatch sources "
			"are not supported on threaded WASI yet");
}

void
_dispatch_unote_resume_muxed(dispatch_unote_t du DISPATCH_UNUSED)
{
	DISPATCH_CLIENT_CRASH(0, "file-descriptor and signal dispatch sources "
			"are not supported on threaded WASI yet");
}

bool
_dispatch_unote_unregister_muxed(dispatch_unote_t du DISPATCH_UNUSED)
{
	DISPATCH_CLIENT_CRASH(0, "file-descriptor and signal dispatch sources "
			"are not supported on threaded WASI yet");
}

#pragma mark timers

static void
_dispatch_event_merge_timer(dispatch_clock_t clock)
{
	dispatch_timer_heap_t dth = _dispatch_timers_heap;
	uint32_t tidx = DISPATCH_TIMER_INDEX(clock, 0);

	_dispatch_wasi_threads_timeout[clock].dwt_armed = false;

	_dispatch_timers_heap_dirty(dth, tidx);
	dth[tidx].dth_needs_program = true;
	dth[tidx].dth_armed = false;
}

void
_dispatch_event_loop_timer_arm(dispatch_timer_heap_t dth DISPATCH_UNUSED,
		uint32_t tidx, dispatch_timer_delay_s range,
		dispatch_clock_now_cache_t nows)
{
	dispatch_clock_t clock = DISPATCH_TIMER_CLOCK(tidx);

	// all clocks advance in nanoseconds; anchoring every deadline on the
	// uptime clock keeps a single sleepable domain (the documented
	// wall-clock limitation of the cooperative backend applies here too)
	uint64_t deadline = range.delay +
			_dispatch_time_now_cached(DISPATCH_CLOCK_UPTIME, nows);
	(void)dispatch_assume_zero(
			pthread_mutex_lock(&_dispatch_wasi_threads_mgr_mutex));
	_dispatch_wasi_threads_timeout[clock].dwt_deadline = deadline;
	_dispatch_wasi_threads_timeout[clock].dwt_armed = true;
	(void)dispatch_assume_zero(
			pthread_cond_signal(&_dispatch_wasi_threads_mgr_cond));
	(void)dispatch_assume_zero(
			pthread_mutex_unlock(&_dispatch_wasi_threads_mgr_mutex));
}

void
_dispatch_event_loop_timer_delete(dispatch_timer_heap_t dth DISPATCH_UNUSED,
		uint32_t tidx)
{
	dispatch_clock_t clock = DISPATCH_TIMER_CLOCK(tidx);
	(void)dispatch_assume_zero(
			pthread_mutex_lock(&_dispatch_wasi_threads_mgr_mutex));
	_dispatch_wasi_threads_timeout[clock].dwt_armed = false;
	(void)dispatch_assume_zero(
			pthread_mutex_unlock(&_dispatch_wasi_threads_mgr_mutex));
}

// caller must hold _dispatch_wasi_threads_mgr_mutex
static uint64_t
_dispatch_wasi_threads_next_deadline(void)
{
	uint64_t next = 0;
	for (size_t i = 0; i < countof(_dispatch_wasi_threads_timeout); i++) {
		if (!_dispatch_wasi_threads_timeout[i].dwt_armed) continue;
		uint64_t deadline = _dispatch_wasi_threads_timeout[i].dwt_deadline;
		if (!next || deadline < next) next = deadline;
	}
	return next;
}

#pragma mark dispatch_loop

void
_dispatch_event_loop_atfork_child(void)
{
}

void
_dispatch_event_loop_poke(dispatch_wlh_t wlh DISPATCH_UNUSED,
		uint64_t dq_state DISPATCH_UNUSED, uint32_t flags DISPATCH_UNUSED)
{
	dispatch_once_f(&_dispatch_wasi_threads_init_pred, NULL,
			_dispatch_wasi_threads_init);
	(void)dispatch_assume_zero(
			pthread_mutex_lock(&_dispatch_wasi_threads_mgr_mutex));
	_dispatch_wasi_threads_mgr_poked = true;
	(void)dispatch_assume_zero(
			pthread_cond_signal(&_dispatch_wasi_threads_mgr_cond));
	(void)dispatch_assume_zero(
			pthread_mutex_unlock(&_dispatch_wasi_threads_mgr_mutex));
}

DISPATCH_NOINLINE
void
_dispatch_event_loop_drain(uint32_t flags)
{
	if (flags & KEVENT_FLAG_IMMEDIATE) {
		// nothing to collect eagerly: there is no poller, and due timers
		// are the manager thread's job (it wakes on its own timedwait)
		return;
	}

	(void)dispatch_assume_zero(
			pthread_mutex_lock(&_dispatch_wasi_threads_mgr_mutex));
	for (;;) {
		if (_dispatch_wasi_threads_mgr_poked) break;
		uint64_t next = _dispatch_wasi_threads_next_deadline();
		uint64_t now = _dispatch_uptime();
		if (next && next <= now) break;
		if (next) {
			uint64_t delta = next - now;
			struct timespec ts;
			(void)dispatch_assume_zero(clock_gettime(CLOCK_MONOTONIC, &ts));
			ts.tv_sec += (time_t)(delta / NSEC_PER_SEC);
			ts.tv_nsec += (long)(delta % NSEC_PER_SEC);
			if (ts.tv_nsec >= (long)NSEC_PER_SEC) {
				ts.tv_sec += 1;
				ts.tv_nsec -= (long)NSEC_PER_SEC;
			}
			int rc = pthread_cond_timedwait(&_dispatch_wasi_threads_mgr_cond,
					&_dispatch_wasi_threads_mgr_mutex, &ts);
			if (rc != 0 && rc != ETIMEDOUT) {
				DISPATCH_INTERNAL_CRASH(rc, "pthread_cond_timedwait");
			}
		} else {
			(void)dispatch_assume_zero(
					pthread_cond_wait(&_dispatch_wasi_threads_mgr_cond,
							&_dispatch_wasi_threads_mgr_mutex));
		}
	}
	_dispatch_wasi_threads_mgr_poked = false;

	uint64_t now = _dispatch_uptime();
	for (size_t i = 0; i < countof(_dispatch_wasi_threads_timeout); i++) {
		if (_dispatch_wasi_threads_timeout[i].dwt_armed &&
				_dispatch_wasi_threads_timeout[i].dwt_deadline <= now) {
			_dispatch_event_merge_timer((dispatch_clock_t)i);
		}
	}
	(void)dispatch_assume_zero(
			pthread_mutex_unlock(&_dispatch_wasi_threads_mgr_mutex));
}

void
_dispatch_event_loop_cancel_waiter(dispatch_sync_context_t dsc)
{
	(void)dsc;
}

void
_dispatch_event_loop_wake_owner(dispatch_sync_context_t dsc,
		dispatch_wlh_t wlh, uint64_t old_state, uint64_t new_state)
{
	(void)dsc; (void)wlh; (void)old_state; (void)new_state;
}

void
_dispatch_event_loop_wait_for_ownership(dispatch_sync_context_t dsc)
{
	if (dsc->dsc_release_storage) {
		_dispatch_queue_release_storage(dsc->dc_data);
	}
}

void
_dispatch_event_loop_end_ownership(dispatch_wlh_t wlh, uint64_t old_state,
		uint64_t new_state, uint32_t flags)
{
	(void)wlh; (void)old_state; (void)new_state; (void)flags;
}

#if DISPATCH_WLH_DEBUG
void
_dispatch_event_loop_assert_not_owned(dispatch_wlh_t wlh)
{
	(void)wlh;
}
#endif

void
_dispatch_event_loop_leave_immediate(uint64_t dq_state)
{
	(void)dq_state;
}

#endif // DISPATCH_EVENT_BACKEND_WASI_THREADS
