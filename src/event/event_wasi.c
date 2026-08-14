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
#if DISPATCH_EVENT_BACKEND_WASI

#include <time.h>

#if !DISPATCH_USE_MGR_THREAD
#error unsupported configuration
#endif

// wasm32-wasip1-threads compiles with the atomics feature (and -pthread
// additionally defines _REENTRANT); plain wasip1 defines neither
#if defined(__wasm_atomics__) || defined(_REENTRANT)
#error the WASI event backend assumes a single-threaded (wasip1) target
#endif

// WASI (wasm32-unknown-wasip1) is single-threaded: there is no manager
// thread, no worker thread pool, and no blocking wait primitive. This
// backend implements a cooperative drain instead: pokes record pending work
// (root queues, the main queue, the manager queue) and the sole thread
// drains it eagerly, either right away when it is not already draining, or
// from the blocking-wait loops in shims/lock.c and from dispatch_main().
// One drain step runs, in priority order: due timers (they unblock waits
// and re-fill the queues), then the manager queue (it arms timers and
// finishes source setup), then the thread-bound main queue, then one pending
// root item. A new root scan starts at the highest QoS, then rotates among
// roots that remain pending so one queue cannot hold the sole thread.
//
// Timers keep their generic heap; this backend only tracks the nearest
// armed deadline per clock so that idle waits can sleep until a timer is
// due and fire it (see _dispatch_wasi_drain_one).

struct _dispatch_wasi_timeout_s {
	uint64_t dwt_deadline; // in the _dispatch_uptime() clock domain
	bool dwt_armed;
};

static struct _dispatch_wasi_timeout_s _dispatch_wasi_timeout[DISPATCH_CLOCK_COUNT];

static bool _dispatch_wasi_root_pending[DISPATCH_ROOT_QUEUE_COUNT];
static size_t _dispatch_wasi_next_root = DISPATCH_ROOT_QUEUE_COUNT - 1;
static bool _dispatch_wasi_main_pending;
static bool _dispatch_wasi_mgr_pending;
static bool _dispatch_wasi_draining;

#pragma mark dispatch_unote_t

bool
_dispatch_unote_register_muxed(dispatch_unote_t du)
{
	// File-descriptor and signal dispatch sources are unsupported on
	// single-threaded WASI. Returning false would merely unregister the
	// unote silently (_dispatch_source_refs_finalize_unregistration), so
	// fail loudly instead.
	DISPATCH_CLIENT_CRASH(du._du->du_filter,
			"file-descriptor and signal dispatch sources are "
			"unsupported on single-threaded WASI");
}

void
_dispatch_unote_resume_muxed(dispatch_unote_t du DISPATCH_UNUSED)
{
	// never reached: registration crashes
}

bool
_dispatch_unote_unregister_muxed(dispatch_unote_t du DISPATCH_UNUSED)
{
	// never reached: registration crashes
	return true;
}

#pragma mark timers

static void
_dispatch_event_merge_timer(dispatch_clock_t clock)
{
	dispatch_timer_heap_t dth = _dispatch_timers_heap;
	uint32_t tidx = DISPATCH_TIMER_INDEX(clock, 0);

	_dispatch_wasi_timeout[clock].dwt_armed = false;

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

	// range.delay is relative to "now" in the timer's own clock domain; all
	// clocks advance in nanoseconds, so anchoring the deadline on the uptime
	// clock keeps a single sleepable domain for _dispatch_wasi_next_timer_ns()
	_dispatch_wasi_timeout[clock].dwt_deadline = range.delay +
			_dispatch_time_now_cached(DISPATCH_CLOCK_UPTIME, nows);
	_dispatch_wasi_timeout[clock].dwt_armed = true;
}

void
_dispatch_event_loop_timer_delete(dispatch_timer_heap_t dth DISPATCH_UNUSED,
		uint32_t tidx)
{
	_dispatch_wasi_timeout[DISPATCH_TIMER_CLOCK(tidx)].dwt_armed = false;
}

uint64_t
_dispatch_wasi_next_timer_ns(void)
{
	uint64_t next = 0;
	for (size_t i = 0; i < countof(_dispatch_wasi_timeout); i++) {
		if (!_dispatch_wasi_timeout[i].dwt_armed) continue;
		uint64_t deadline = _dispatch_wasi_timeout[i].dwt_deadline;
		if (!next || deadline < next) next = deadline;
	}
	return next;
}

DISPATCH_ALWAYS_INLINE
static inline bool
_dispatch_wasi_merge_due_timers(void)
{
	uint64_t now = _dispatch_uptime();
	bool fired = false;
	for (size_t i = 0; i < countof(_dispatch_wasi_timeout); i++) {
		if (_dispatch_wasi_timeout[i].dwt_armed &&
				_dispatch_wasi_timeout[i].dwt_deadline <= now) {
			_dispatch_event_merge_timer((dispatch_clock_t)i);
			fired = true;
		}
	}
	return fired;
}

#pragma mark sleeping

void
_dispatch_wasi_sleep_until(uint64_t uptime_ns)
{
	uint64_t now = _dispatch_uptime();
	if (uptime_ns <= now) return;
	uint64_t delta = uptime_ns - now;
	struct timespec ts = {
		.tv_sec = (time_t)(delta / NSEC_PER_SEC),
		.tv_nsec = (long)(delta % NSEC_PER_SEC),
	};
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
	}
}

void
_dispatch_wasi_sleep_briefly_or_until(uint64_t deadline_uptime_ns)
{
	uint64_t next_timer = _dispatch_wasi_next_timer_ns();
	if (next_timer && next_timer < deadline_uptime_ns) {
		deadline_uptime_ns = next_timer;
	}
	_dispatch_wasi_sleep_until(deadline_uptime_ns);
}

#pragma mark cooperative drain

bool
_dispatch_wasi_drain_one(void)
{
	if (_dispatch_wasi_draining) {
		// no nested drains: the queue state machinery (thread frames, wlh,
		// dq_state drain locks) is not reentrant on the sole thread. The
		// outer drain picks deferred work up.
		return false;
	}
	_dispatch_wasi_draining = true;
	bool did_work = true;
	if (_dispatch_wasi_merge_due_timers() ||
			_dispatch_timers_heap[0].dth_dirty_bits) {
		// a due timer counts as a pending item (see shims/lock.h): firing it
		// pushes the timer source's handler onto its target queue
		_dispatch_event_loop_drain_timers(_dispatch_timers_heap,
				DISPATCH_TIMER_COUNT);
	} else if (_dispatch_wasi_mgr_pending) {
		_dispatch_wasi_mgr_pending = false;
		_dispatch_wasi_mgr_queue_drain();
	} else if (_dispatch_wasi_main_pending) {
		_dispatch_wasi_main_pending = false;
		_dispatch_wasi_main_queue_drain();
	} else {
		did_work = false;
		// Start at the highest QoS, then continue below the root queue that
		// last ran so a self-replenishing queue cannot starve the others.
		for (size_t offset = 0;
				offset < countof(_dispatch_wasi_root_pending); offset++) {
			size_t i = (_dispatch_wasi_next_root +
					countof(_dispatch_wasi_root_pending) - offset) %
					countof(_dispatch_wasi_root_pending);
			if (!_dispatch_wasi_root_pending[i]) continue;
			bool roots_were_waiting = false;
			for (size_t j = 0;
					j < countof(_dispatch_wasi_root_pending); j++) {
				roots_were_waiting |= j != i &&
						_dispatch_wasi_root_pending[j];
			}
			_dispatch_wasi_root_pending[i] = false;
			_dispatch_wasi_root_queue_drain(&_dispatch_root_queues[i]);
			bool keep_rotating = roots_were_waiting ||
					_dispatch_wasi_root_pending[i];
			_dispatch_wasi_next_root = keep_rotating && i ? i - 1 :
					countof(_dispatch_wasi_root_pending) - 1;
			did_work = true;
			break;
		}
	}
	_dispatch_wasi_draining = false;
	return did_work;
}

void
_dispatch_wasi_drain(void)
{
	while (_dispatch_wasi_drain_one()) {
	}
}

bool
_dispatch_wasi_in_drain(void)
{
	return _dispatch_wasi_draining;
}

void
_dispatch_wasi_root_queue_poke(dispatch_queue_global_t dq)
{
	size_t idx = (size_t)(dq - _dispatch_root_queues);
	if (unlikely(idx >= DISPATCH_ROOT_QUEUE_COUNT)) {
		DISPATCH_INTERNAL_CRASH(dq, "Poke of a non-global root queue on WASI");
	}
	_dispatch_wasi_root_pending[idx] = true;
	if (!_dispatch_wasi_draining) {
		_dispatch_wasi_drain();
	}
}

void
_dispatch_wasi_main_queue_poke(void)
{
	_dispatch_wasi_main_pending = true;
	if (!_dispatch_wasi_draining) {
		_dispatch_wasi_drain();
	}
}

#pragma mark dispatch_loop

void
_dispatch_event_loop_atfork_child(void)
{
}

void
_dispatch_event_loop_poke(dispatch_wlh_t wlh,
		uint64_t dq_state DISPATCH_UNUSED, uint32_t flags DISPATCH_UNUSED)
{
	if (wlh == DISPATCH_WLH_MANAGER) {
		_dispatch_wasi_mgr_pending = true;
		if (!_dispatch_wasi_draining) {
			_dispatch_wasi_drain();
		}
		return;
	}
	// every poke caller compiled outside DISPATCH_USE_KEVENT_WORKLOOP
	// passes DISPATCH_WLH_MANAGER
	DISPATCH_INTERNAL_CRASH((uintptr_t)wlh,
			"unexpected non-manager event loop poke on WASI");
}

DISPATCH_NOINLINE
void
_dispatch_event_loop_drain(uint32_t flags DISPATCH_UNUSED)
{
	// only reachable from the manager thread loop, which never runs on
	// single-threaded WASI (the manager queue is drained cooperatively by
	// _dispatch_wasi_mgr_queue_drain instead)
	DISPATCH_INTERNAL_CRASH(0, "manager event loop cannot run on WASI");
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

#endif // DISPATCH_EVENT_BACKEND_WASI
