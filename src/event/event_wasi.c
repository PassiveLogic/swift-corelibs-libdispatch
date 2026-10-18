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

// wasm32-wasip1-threads builds with the atomics feature, and -pthread also
// defines _REENTRANT. Plain wasip1 defines neither.
#if defined(__wasm_atomics__) || defined(_REENTRANT)
#error the WASI event backend assumes a single-threaded (wasip1) target
#endif

// Single-threaded WASI has no worker or manager thread. A poke only records
// pending work. The sole thread drains it at a pump point: a blocking wait
// (shims/lock.c), dispatch_main(), or _dispatch_wasi_event_loop_perform().
// One drain step runs the first phase with work: due timers, the manager
// queue, the main queue (not from a blocking wait), one root item.
// Main and root steps alternate. Roots are served round-robin, so QoS orders
// only the first pass.
// Timers use the shared heap. Only the nearest deadline per clock is kept.

struct _dispatch_wasi_timeout_s {
	uint64_t dwt_deadline; // in the timer's own clock domain
	bool dwt_armed;
};

static struct _dispatch_wasi_timeout_s _dispatch_wasi_timeout[DISPATCH_CLOCK_COUNT];

static bool _dispatch_wasi_root_pending[DISPATCH_ROOT_QUEUE_COUNT];
static size_t _dispatch_wasi_next_root = DISPATCH_ROOT_QUEUE_COUNT - 1;
static bool _dispatch_wasi_main_pending;
static bool _dispatch_wasi_mgr_pending;
static bool _dispatch_wasi_main_ran_last;
// A poke inside a drain step records work only. The running pump takes it.
static bool _dispatch_wasi_draining;
static void (*_dispatch_wasi_host_schedule)(void *);
static void *_dispatch_wasi_host_context;
static bool _dispatch_wasi_host_turn_scheduled;
static bool _dispatch_wasi_host_schedule_in_progress;
static bool _dispatch_wasi_performing;
// A timer armed before registration needs one host turn, because the host
// reads the timer delay only after a perform call.
static bool _dispatch_wasi_armed_timer_unseen;

static void
_dispatch_wasi_schedule_host_turn(void)
{
	if (_dispatch_wasi_host_turn_scheduled) return;
	_dispatch_wasi_host_turn_scheduled = true;
	_dispatch_wasi_host_schedule_in_progress = true;
	_dispatch_wasi_host_schedule(_dispatch_wasi_host_context);
	_dispatch_wasi_host_schedule_in_progress = false;
}

DISPATCH_ALWAYS_INLINE
static inline bool
_dispatch_wasi_root_work_pending(void)
{
	for (size_t i = 0; i < countof(_dispatch_wasi_root_pending); i++) {
		if (_dispatch_wasi_root_pending[i]) return true;
	}
	return false;
}

DISPATCH_ALWAYS_INLINE
static inline bool
_dispatch_wasi_work_pending(void)
{
	return _dispatch_wasi_mgr_pending || _dispatch_wasi_main_pending ||
			_dispatch_wasi_root_work_pending();
}

// True when another drain step can act without a wait.
static bool
_dispatch_wasi_has_runnable_work(void)
{
	if (_dispatch_wasi_work_pending()) return true;
	if (_dispatch_timers_heap[0].dth_dirty_bits) return true;
	uint64_t next_timer = _dispatch_wasi_next_timer_ns();
	return next_timer && next_timer <= _dispatch_uptime();
}

// A poke records work and never drains. Outside a drain or perform, it asks
// a registered host scheduler for one turn.
static void
_dispatch_wasi_note_pending(void)
{
	if (_dispatch_wasi_host_schedule && !_dispatch_wasi_draining &&
			!_dispatch_wasi_performing) {
		_dispatch_wasi_schedule_host_turn();
	}
}

// After a drain step outside perform, ask the host for a turn if work
// remains or a timer was armed before registration. A pump that keeps
// running can leave that turn empty.
static void
_dispatch_wasi_hand_off_pending(void)
{
	if (_dispatch_wasi_host_schedule && !_dispatch_wasi_host_turn_scheduled &&
			!_dispatch_wasi_performing &&
			(_dispatch_wasi_armed_timer_unseen ||
			_dispatch_wasi_has_runnable_work())) {
		_dispatch_wasi_note_pending();
	}
}

#pragma mark dispatch_unote_t

// Descriptor and signal sources are not implemented. Registering one
// crashes, because a cancelled source would leave the sole thread waiting
// forever. Timer and data sources do not register here.
bool
_dispatch_unote_register_muxed(dispatch_unote_t du)
{
	DISPATCH_CLIENT_CRASH(du._du->du_filter,
			"Descriptor and signal sources unsupported on WASI");
}

void
_dispatch_unote_resume_muxed(dispatch_unote_t du)
{
	// unreachable: _dispatch_unote_register_muxed never succeeds
	DISPATCH_INTERNAL_CRASH(du._du->du_filter,
			"Resume of an unregistered muxed unote");
}

bool
_dispatch_unote_unregister_muxed(dispatch_unote_t du)
{
	// unreachable: _dispatch_unote_register_muxed never succeeds
	DISPATCH_INTERNAL_CRASH(du._du->du_filter,
			"Unregister of an unregistered muxed unote");
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

	_dispatch_wasi_timeout[clock].dwt_deadline = range.delay +
			_dispatch_time_now_cached(clock, nows);
	_dispatch_wasi_timeout[clock].dwt_armed = true;
}

void
_dispatch_event_loop_timer_delete(dispatch_timer_heap_t dth DISPATCH_UNUSED,
		uint32_t tidx)
{
	_dispatch_wasi_timeout[DISPATCH_TIMER_CLOCK(tidx)].dwt_armed = false;
}

// Returns the nearest armed deadline in the _dispatch_uptime() domain, or 0.
// A deadline on another clock is projected with that clock's current value.
// WASI has no clock-change notification, so a wall-clock change takes
// effect at the next wake.
uint64_t
_dispatch_wasi_next_timer_ns(void)
{
	uint64_t uptime = 0;
	uint64_t next = 0;
	for (size_t i = 0; i < countof(_dispatch_wasi_timeout); i++) {
		if (!_dispatch_wasi_timeout[i].dwt_armed) continue;
		uint64_t deadline = _dispatch_wasi_timeout[i].dwt_deadline;
		if (i != DISPATCH_CLOCK_UPTIME) {
			if (!uptime) uptime = _dispatch_uptime();
			uint64_t now = _dispatch_time_now((dispatch_clock_t)i);
			deadline = uptime + (deadline > now ? deadline - now : 0);
		}
		if (!next || deadline < next) next = deadline;
	}
	return next;
}

DISPATCH_ALWAYS_INLINE
static inline bool
_dispatch_wasi_merge_due_timers(void)
{
	bool fired = false;
	for (size_t i = 0; i < countof(_dispatch_wasi_timeout); i++) {
		if (_dispatch_wasi_timeout[i].dwt_armed &&
				_dispatch_wasi_timeout[i].dwt_deadline <=
						_dispatch_time_now((dispatch_clock_t)i)) {
			_dispatch_event_merge_timer((dispatch_clock_t)i);
			fired = true;
		}
	}
	return fired;
}

#pragma mark sleeping

// Without poll_oneoff clock support (as in WasmKit 0.1.x) every sleep fails.
// After the first failure, log once and return at once. Each caller loops to
// its own deadline.
static bool _dispatch_wasi_cannot_sleep;

void
_dispatch_wasi_sleep_until(uint64_t uptime_ns)
{
	uint64_t now = _dispatch_uptime();
	if (uptime_ns <= now || _dispatch_wasi_cannot_sleep) return;
	uint64_t delta = uptime_ns - now;
	struct timespec ts = {
		.tv_sec = (time_t)(delta / NSEC_PER_SEC),
		.tv_nsec = (long)(delta % NSEC_PER_SEC),
	};
	// Sleep on the clock that _dispatch_uptime() reads. wasi-libc nanosleep()
	// uses CLOCK_REALTIME. clock_nanosleep() returns the error, not errno.
	int rc = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
	if (unlikely(rc != 0)) {
		_dispatch_wasi_cannot_sleep = true;
		_dispatch_log("clock_nanosleep() failed (%d), waits poll the clock",
				rc);
	}
}

#pragma mark cooperative drain

bool
_dispatch_wasi_drain_step(bool include_main)
{
	// The scheduler callback must not run Dispatch. A pump inside it would
	// run blocks on the stack of the dispatch_async that asked for the turn.
	if (unlikely(_dispatch_wasi_host_schedule_in_progress)) {
		DISPATCH_CLIENT_CRASH(0, "Scheduler ran Dispatch inline");
	}
	if (_dispatch_wasi_draining) {
		// Drains do not nest, because the queue state machinery is not
		// reentrant. The outer drain takes the work.
		return false;
	}
	_dispatch_wasi_draining = true;

	// Step 1: Hide a borrowed wlh, such as the _dispatch_fake_wlh of
	// dispatch_async_and_wait, from the drains. Never release it.
	void *borrowed_wlh = _dispatch_thread_getspecific(dispatch_wlh_key);
	_dispatch_thread_setspecific(dispatch_wlh_key, NULL);

	// Step 2: Run the first phase that has work (see the file header).
	bool did_work = true;
	if (_dispatch_wasi_merge_due_timers() ||
			_dispatch_timers_heap[0].dth_dirty_bits) {
		// A due timer counts as work. Firing it queues the timer handler.
		_dispatch_event_loop_drain_timers(_dispatch_timers_heap,
				DISPATCH_TIMER_COUNT);
	} else if (_dispatch_wasi_mgr_pending) {
		_dispatch_wasi_mgr_pending = false;
		_dispatch_wasi_mgr_queue_drain();
	} else if (include_main && _dispatch_wasi_main_pending &&
			!(_dispatch_wasi_main_ran_last &&
			_dispatch_wasi_root_work_pending())) {
		// Main and the roots take turns, so main work that re-posts itself
		// cannot starve the roots.
		_dispatch_wasi_main_pending = false;
		_dispatch_wasi_main_ran_last = true;
		_dispatch_wasi_main_queue_drain();
	} else {
		did_work = false;
		// Round-robin: while other roots wait, the next scan starts below the
		// root that ran.
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
			_dispatch_wasi_main_ran_last = false;
			_dispatch_wasi_root_queue_drain(&_dispatch_root_queues[i]);
			bool keep_rotating = roots_were_waiting ||
					_dispatch_wasi_root_pending[i];
			_dispatch_wasi_next_root = keep_rotating && i ? i - 1 :
					countof(_dispatch_wasi_root_pending) - 1;
			did_work = true;
			break;
		}
	}

	// Step 3: Restore the borrowed wlh. Hand leftover work to the host.
	_dispatch_thread_setspecific(dispatch_wlh_key, borrowed_wlh);
	_dispatch_wasi_draining = false;
	_dispatch_wasi_hand_off_pending();
	return did_work;
}

bool
_dispatch_wasi_main_queue_pending(void)
{
	return _dispatch_wasi_main_pending;
}

void
_dispatch_wasi_drain(void)
{
	while (_dispatch_wasi_drain_step(true)) {
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
		DISPATCH_INTERNAL_CRASH(dq, "Poke of a non-global root queue");
	}
	_dispatch_wasi_root_pending[idx] = true;
	_dispatch_wasi_note_pending();
}

void
_dispatch_wasi_main_queue_poke(void)
{
	_dispatch_wasi_main_pending = true;
	_dispatch_wasi_note_pending();
}

#pragma mark host event loop SPI

void
_dispatch_wasi_event_loop_set_scheduler(
		dispatch_wasi_event_loop_schedule_t schedule, void *context)
{
	if (_dispatch_wasi_host_schedule) {
		DISPATCH_CLIENT_CRASH(0, "Scheduler already registered");
	}
	_dispatch_wasi_host_schedule = schedule;
	_dispatch_wasi_host_context = context;
	// Hand over work and timers from before registration. Inside a drain,
	// the running pump does it when its step ends.
	_dispatch_wasi_armed_timer_unseen = _dispatch_wasi_next_timer_ns() != 0;
	_dispatch_wasi_hand_off_pending();
}

bool
_dispatch_wasi_event_loop_perform(unsigned long max_steps,
		bool consumes_scheduled_turn)
{
	// Step 1: Crash on a call that the SPI does not allow.
	if (!_dispatch_wasi_host_schedule) {
		DISPATCH_CLIENT_CRASH(0, "Scheduler not registered");
	}
	if (!max_steps) {
		DISPATCH_CLIENT_CRASH(0, "Zero step budget");
	}
	if (_dispatch_wasi_host_schedule_in_progress) {
		DISPATCH_CLIENT_CRASH(0, "Perform called from the scheduler");
	}
	if (_dispatch_wasi_draining) {
		DISPATCH_CLIENT_CRASH(0, "Perform called from a work item");
	}

	// Step 2: Consume the turn if this call owns it. Clear the timer latch.
	if (consumes_scheduled_turn) {
		_dispatch_wasi_host_turn_scheduled = false;
	}
	_dispatch_wasi_armed_timer_unseen = false;

	// Step 3: Run up to max_steps drain steps. Pokes here request no turn.
	_dispatch_wasi_performing = true;
	for (unsigned long step = 0; step < max_steps; step++) {
		if (!_dispatch_wasi_drain_step(true)) break;
	}
	_dispatch_wasi_performing = false;

	// Step 4: Request one follow-up turn if runnable work remains.
	bool more_work = _dispatch_wasi_has_runnable_work();
	if (more_work) {
		_dispatch_wasi_schedule_host_turn();
	}
	return more_work;
}

int64_t
_dispatch_wasi_event_loop_next_timer_delay(void)
{
	uint64_t deadline = _dispatch_wasi_next_timer_ns();
	if (!deadline) return -1;
	uint64_t now = _dispatch_uptime();
	if (deadline <= now) return 0;
	return (int64_t)MIN(deadline - now, INT64_MAX);
}

#pragma mark dispatch_loop

void
_dispatch_event_loop_atfork_child(void)
{
	// wasi-libc has no fork(), so no child process exists to reset.
}

void
_dispatch_event_loop_poke(dispatch_wlh_t wlh,
		uint64_t dq_state DISPATCH_UNUSED, uint32_t flags DISPATCH_UNUSED)
{
	if (wlh == DISPATCH_WLH_MANAGER) {
		_dispatch_wasi_mgr_pending = true;
		_dispatch_wasi_note_pending();
		return;
	}
	// Without DISPATCH_USE_KEVENT_WORKLOOP, every poke caller passes
	// DISPATCH_WLH_MANAGER.
	DISPATCH_INTERNAL_CRASH((uintptr_t)wlh,
			"Unexpected non-manager event loop poke");
}

DISPATCH_NOINLINE
void
_dispatch_event_loop_drain(uint32_t flags DISPATCH_UNUSED)
{
	// Only the manager thread loop calls this, and it never runs on WASI.
	// _dispatch_wasi_mgr_queue_drain drains the manager queue instead.
	DISPATCH_INTERNAL_CRASH(0, "Manager event loop cannot run");
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
