/*
 * WASI event backend for libdispatch (single-threaded cooperative port).
 *
 * WebAssembly/WASI has no epoll/kqueue, no signals, and (in the single-threaded
 * configuration) no worker threads. "The event loop" here is therefore just a
 * timer facility: armed deadlines are tracked per clock, and the cooperative
 * run loop (_dispatch_wasi_runloop_main in queue.c) blocks on a libc sleep
 * (-> wasi poll_oneoff) until the nearest deadline, then marks the expired
 * clocks so the generic timer machinery fires their handlers.
 *
 * File-descriptor / signal sources are not supported in this configuration:
 * _dispatch_unote_register_muxed returns false so dispatch_source surfaces the
 * limitation immediately rather than silently never firing.
 */

#include "internal.h"

#if DISPATCH_EVENT_BACKEND_WASI

#include <time.h>

#pragma mark timers

// Absolute deadline per clock (in that clock's ns), or UINT64_MAX when disarmed.
static uint64_t _dispatch_wasi_timer_target[DISPATCH_CLOCK_COUNT] = {
	[DISPATCH_CLOCK_UPTIME]    = UINT64_MAX,
	[DISPATCH_CLOCK_MONOTONIC] = UINT64_MAX,
	[DISPATCH_CLOCK_WALL]      = UINT64_MAX,
};

static void
_dispatch_wasi_merge_timer(dispatch_clock_t clock)
{
	dispatch_timer_heap_t dth = _dispatch_timers_heap;
	uint32_t tidx = DISPATCH_TIMER_INDEX(clock, 0);

	_dispatch_wasi_timer_target[clock] = UINT64_MAX;
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
	uint64_t target = range.delay + _dispatch_time_now_cached(clock, nows);
	_dispatch_wasi_timer_target[clock] = (target < INT64_MAX) ? target : UINT64_MAX;
}

void
_dispatch_event_loop_timer_delete(dispatch_timer_heap_t dth DISPATCH_UNUSED,
		uint32_t tidx)
{
	_dispatch_wasi_timer_target[DISPATCH_TIMER_CLOCK(tidx)] = UINT64_MAX;
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
	// Single-threaded: no blocked manager thread to wake. The cooperative run
	// loop re-checks the manager queue on its next turn.
}

DISPATCH_NOINLINE
void
_dispatch_event_loop_drain(uint32_t flags)
{
	if (flags & KEVENT_FLAG_IMMEDIATE) {
		return;
	}

	// Find the nearest armed deadline across all clocks.
	dispatch_clock_now_cache_s nows = { };
	uint64_t min_delay = UINT64_MAX;
	for (dispatch_clock_t c = 0; c < DISPATCH_CLOCK_COUNT; c++) {
		uint64_t target = _dispatch_wasi_timer_target[c];
		if (target == UINT64_MAX) continue;
		uint64_t now = _dispatch_time_now_cached(c, &nows);
		uint64_t d = (target > now) ? (target - now) : 0;
		if (d < min_delay) min_delay = d;
	}

	if (min_delay == UINT64_MAX) {
		// No armed timers. In a single-threaded wasm there is nothing else that
		// could wake us, so do not block (the run loop is expected to exit via a
		// completion handler, e.g. exit()).
		return;
	}

	if (min_delay) {
		struct timespec req = {
			(time_t)(min_delay / NSEC_PER_SEC),
			(long)(min_delay % NSEC_PER_SEC),
		};
		// On wasi this lowers to poll_oneoff(CLOCK) — a real (blocking) sleep on
		// servers / worker threads.
		nanosleep(&req, NULL);
	}

	// Mark every clock whose deadline has now passed so the generic timer drain
	// (_dispatch_event_loop_drain_anon_timers) fires their handlers next turn.
	dispatch_clock_now_cache_s nows2 = { };
	for (dispatch_clock_t c = 0; c < DISPATCH_CLOCK_COUNT; c++) {
		uint64_t target = _dispatch_wasi_timer_target[c];
		if (target == UINT64_MAX) continue;
		uint64_t now = _dispatch_time_now_cached(c, &nows2);
		if (now >= target) {
			_dispatch_wasi_merge_timer(c);
		}
	}
}

#pragma mark dispatch_sync / ownership (no-ops; single-threaded never blocks here)

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

#pragma mark unotes (fd / signal sources unsupported single-threaded)

bool
_dispatch_unote_register_muxed(dispatch_unote_t du)
{
	// No fd/signal readiness on single-threaded wasi (poll_oneoff fd subs are
	// unsupported in the browser shim). Surface the limitation to the source.
	(void)du;
	return false;
}

void
_dispatch_unote_resume_muxed(dispatch_unote_t du)
{
	(void)du;
}

bool
_dispatch_unote_unregister_muxed(dispatch_unote_t du)
{
	(void)du;
	return true;
}

#endif // DISPATCH_EVENT_BACKEND_WASI
