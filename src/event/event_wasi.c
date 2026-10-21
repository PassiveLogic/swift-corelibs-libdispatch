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
#include <poll.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef _WASI_EMULATED_SIGNAL
#include <signal.h>
#endif

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
// True while the poll harvest walks the muxnote list. Pokes then only record
// work, as in a drain step, so a merge handler cannot free the muxnotes that
// the harvest walks.
static bool _dispatch_wasi_harvesting;
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

// A poke records work and never drains. Outside a drain, a harvest, or
// perform, it asks a registered host scheduler for one turn.
static void
_dispatch_wasi_note_pending(void)
{
	if (_dispatch_wasi_host_schedule && !_dispatch_wasi_draining &&
			!_dispatch_wasi_harvesting && !_dispatch_wasi_performing) {
		_dispatch_wasi_schedule_host_turn();
	}
}

// After a drain step or a harvest outside perform, ask the host for a turn if
// work remains or a timer was armed before registration. A pump that keeps
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

#pragma mark dispatch_muxnote_t

// File-descriptor readiness rides WASI preview1's poll_oneoff (via wasi-libc
// poll(2)), merged into the cooperative drain at every wait point. Regular
// files are never polled: POSIX says they are always ready, and some hosts
// (Node's uvwasi) error on fd subscriptions for them, so they are marked
// always-ready at registration the way the epoll backend handles EPERM.
// Signal sources ride wasi-libc's _WASI_EMULATED_SIGNAL emulation: an
// in-process raise() invokes the handler installed at registration
// synchronously, which records the delivery for the next harvest. There is no
// asynchronous or cross-process signal delivery on any WASI version.
typedef struct dispatch_muxnote_s {
	LIST_ENTRY(dispatch_muxnote_s) dmn_list;
	LIST_HEAD(, dispatch_unote_linkage_s) dmn_readers_head;
	LIST_HEAD(, dispatch_unote_linkage_s) dmn_writers_head;
	uint32_t  dmn_ident;           // fd, or signal number
	uint16_t  dmn_events;          // POLLIN|POLLOUT wanted (fd muxnotes)
	uint16_t  dmn_disarmed_events; // delivered, awaiting EV_DISPATCH rearm
	int8_t    dmn_filter;          // EVFILT_READ (fds; writers share) or EVFILT_SIGNAL
	bool      dmn_always_ready;    // regular file/directory: never polled
#ifdef _WASI_EMULATED_SIGNAL
	void    (*dmn_prev_sig_handler)(int); // restored at unregistration
#endif
} *dispatch_muxnote_t;

static LIST_HEAD(, dispatch_muxnote_s) _dispatch_wasi_muxnotes;

#ifdef _WASI_EMULATED_SIGNAL
#ifndef NSIG
#define NSIG 33
#endif
static unsigned long _dispatch_wasi_signal_pending[NSIG];
static bool _dispatch_wasi_signal_pending_any;

static void
_dispatch_wasi_signal_handler(int signo)
{
	// invoked synchronously from the caller's raise(); merged by
	// _dispatch_wasi_poll_harvest at the next pump point (a blocking wait,
	// a dispatch_main() park, or a host turn)
	if (signo > 0 && signo < NSIG) {
		_dispatch_wasi_signal_pending[signo]++;
		_dispatch_wasi_signal_pending_any = true;
	}
}
#endif // _WASI_EMULATED_SIGNAL

DISPATCH_ALWAYS_INLINE
static inline uint16_t
_dispatch_muxnote_armed_events(dispatch_muxnote_t dmn)
{
	return dmn->dmn_events & (uint16_t)~dmn->dmn_disarmed_events;
}

static dispatch_muxnote_t
_dispatch_muxnote_find(uint32_t ident, int8_t filter)
{
	dispatch_muxnote_t dmn;
	if (filter == EVFILT_WRITE) filter = EVFILT_READ;
	LIST_FOREACH(dmn, &_dispatch_wasi_muxnotes, dmn_list) {
		if (dmn->dmn_ident == ident && dmn->dmn_filter == filter) {
			break;
		}
	}
	return dmn;
}

static dispatch_muxnote_t
_dispatch_muxnote_create(dispatch_unote_t du, uint16_t events)
{
	dispatch_muxnote_t dmn;
	int8_t filter = du._du->du_filter;
	uint32_t ident = du._du->du_ident;
	bool always_ready = false;
#ifdef _WASI_EMULATED_SIGNAL
	void (*prev_sig_handler)(int) = SIG_DFL;
#endif

	switch (filter) {
	case EVFILT_WRITE:
		filter = EVFILT_READ;
		DISPATCH_FALLTHROUGH;
	case EVFILT_READ: {
		struct stat sb;
		if (fstat((int)ident, &sb) < 0) {
			DISPATCH_CLIENT_CRASH(ident, "dispatch source file descriptor "
					"is not open");
		}
		if (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode)) {
			// always ready per POSIX; also keeps them out of the poll set,
			// which hosts like Node's uvwasi reject for regular files
			always_ready = true;
		} else {
			// capability probe: limited hosts (browser WASI shims) support
			// only single clock subscriptions in poll_oneoff and reject fd
			// subscriptions; surface that at registration, not as a hang
			struct pollfd pfd = { .fd = (int)ident, .events = (short)events };
			if (poll(&pfd, 1, 0) < 0) {
				if (errno == ENOTSUP || errno == ENOSYS) {
					DISPATCH_CLIENT_CRASH(errno, "this WASI runtime does not "
							"support file-descriptor readiness "
							"(poll_oneoff fd subscriptions)");
				}
				DISPATCH_CLIENT_CRASH(errno, "poll() failed for dispatch "
						"source file descriptor");
			}
			if (pfd.revents & POLLNVAL) {
				DISPATCH_CLIENT_CRASH(ident, "dispatch source file "
						"descriptor is not open");
			}
		}
		break;
	}
#ifdef _WASI_EMULATED_SIGNAL
	case EVFILT_SIGNAL: {
		int signo = (int)ident;
		if (signo <= 0 || signo >= NSIG) {
			DISPATCH_CLIENT_CRASH(ident, "invalid signal number for "
					"dispatch source");
		}
		// in-process delivery only: the handler runs synchronously inside
		// raise(); nothing external can send a signal to a WASI guest. Save
		// the application's disposition so unregistration can restore it.
		prev_sig_handler = signal(signo, _dispatch_wasi_signal_handler);
		if (prev_sig_handler == SIG_ERR) {
			DISPATCH_CLIENT_CRASH(ident, "signal() failed for dispatch "
					"signal source");
		}
		break;
	}
#endif // _WASI_EMULATED_SIGNAL
	default:
		DISPATCH_CLIENT_CRASH(filter,
				"unsupported dispatch source type on WASI");
	}

	dmn = _dispatch_calloc(1, sizeof(struct dispatch_muxnote_s));
	LIST_INIT(&dmn->dmn_readers_head);
	LIST_INIT(&dmn->dmn_writers_head);
	dmn->dmn_ident = ident;
	dmn->dmn_filter = filter;
	dmn->dmn_events = events;
	dmn->dmn_always_ready = always_ready;
#ifdef _WASI_EMULATED_SIGNAL
	dmn->dmn_prev_sig_handler = prev_sig_handler;
#endif
	return dmn;
}

DISPATCH_ALWAYS_INLINE
static inline uint16_t
_dispatch_unote_required_events(dispatch_unote_t du)
{
	switch (du._du->du_filter) {
	case EVFILT_WRITE:
		return POLLOUT;
	case EVFILT_SIGNAL:
		return 0;
	default:
		return POLLIN;
	}
}

bool
_dispatch_unote_register_muxed(dispatch_unote_t du)
{
	uint16_t events = _dispatch_unote_required_events(du);
	dispatch_muxnote_t dmn;

	dmn = _dispatch_muxnote_find(du._du->du_ident, du._du->du_filter);
	if (dmn) {
		dmn->dmn_events |= events;
		dmn->dmn_disarmed_events &= (uint16_t)~events;
	} else {
		dmn = _dispatch_muxnote_create(du, events);
		LIST_INSERT_HEAD(&_dispatch_wasi_muxnotes, dmn, dmn_list);
	}

	dispatch_unote_linkage_t dul = _dispatch_unote_get_linkage(du);
	if (events & POLLOUT) {
		LIST_INSERT_HEAD(&dmn->dmn_writers_head, dul, du_link);
	} else {
		LIST_INSERT_HEAD(&dmn->dmn_readers_head, dul, du_link);
	}
	dul->du_muxnote = dmn;
	_dispatch_unote_state_set(du, DISPATCH_WLH_ANON, DU_STATE_ARMED);
	return true;
}

void
_dispatch_unote_resume_muxed(dispatch_unote_t du)
{
	dispatch_muxnote_t dmn = _dispatch_unote_get_linkage(du)->du_muxnote;
	dispatch_assert(_dispatch_unote_registered(du));
	uint16_t events = _dispatch_unote_required_events(du);
	dmn->dmn_disarmed_events &= (uint16_t)~events;
}

bool
_dispatch_unote_unregister_muxed(dispatch_unote_t du)
{
	dispatch_unote_linkage_t dul = _dispatch_unote_get_linkage(du);
	dispatch_muxnote_t dmn = dul->du_muxnote;

	LIST_REMOVE(dul, du_link);
	_LIST_TRASH_ENTRY(dul, du_link);
	dul->du_muxnote = NULL;

	if (LIST_EMPTY(&dmn->dmn_readers_head)) {
		dmn->dmn_events &= (uint16_t)~POLLIN;
		dmn->dmn_disarmed_events &= (uint16_t)~POLLIN;
	}
	if (LIST_EMPTY(&dmn->dmn_writers_head)) {
		dmn->dmn_events &= (uint16_t)~POLLOUT;
		dmn->dmn_disarmed_events &= (uint16_t)~POLLOUT;
	}
	if (LIST_EMPTY(&dmn->dmn_readers_head) &&
			LIST_EMPTY(&dmn->dmn_writers_head)) {
#ifdef _WASI_EMULATED_SIGNAL
		if (dmn->dmn_filter == EVFILT_SIGNAL) {
			// restore the application's disposition, drop this signal's
			// undelivered count, and keep the aggregate latch truthful
			if (signal((int)dmn->dmn_ident, dmn->dmn_prev_sig_handler) ==
					SIG_ERR) {
				DISPATCH_INTERNAL_CRASH(dmn->dmn_ident, "signal() failed "
						"restoring the disposition of an unregistered "
						"dispatch signal source");
			}
			_dispatch_wasi_signal_pending[dmn->dmn_ident] = 0;
			bool any = false;
			for (int s = 1; s < NSIG; s++) {
				any = any || _dispatch_wasi_signal_pending[s] != 0;
			}
			_dispatch_wasi_signal_pending_any = any;
		}
#endif
		LIST_REMOVE(dmn, dmn_list);
		free(dmn);
	}
	_dispatch_unote_state_set(du, DU_STATE_UNREGISTERED);
	return true;
}

#pragma mark event harvesting

static void
_dispatch_wasi_merge_fd_event(dispatch_muxnote_t dmn, uint16_t revents)
{
	dispatch_unote_linkage_t dul, dul_next;
	// wasi has no FIONREAD/SIOCINQ equivalent; report one readable/writable
	// unit the way the epoll backend does when the buffer-size ioctls are
	// unavailable
	uintptr_t data = 1;

	dmn->dmn_disarmed_events |= (revents & (POLLIN | POLLOUT));

	if (revents & POLLIN) {
		LIST_FOREACH_SAFE(dul, &dmn->dmn_readers_head, du_link, dul_next) {
			dispatch_unote_t du = _dispatch_unote_linkage_get_unote(dul);
			// consumed by dux_merge_evt()
			_dispatch_retain_unote_owner(du);
			dispatch_assert(dux_needs_rearm(du._du));
			_dispatch_unote_state_clear_bit(du, DU_STATE_ARMED);
			os_atomic_store2o(du._dr, ds_pending_data, ~data, relaxed);
			dux_merge_evt(du._du, EV_ADD|EV_ENABLE|EV_DISPATCH, data, 0);
		}
	}
	if (revents & POLLOUT) {
		LIST_FOREACH_SAFE(dul, &dmn->dmn_writers_head, du_link, dul_next) {
			dispatch_unote_t du = _dispatch_unote_linkage_get_unote(dul);
			// consumed by dux_merge_evt()
			_dispatch_retain_unote_owner(du);
			dispatch_assert(dux_needs_rearm(du._du));
			_dispatch_unote_state_clear_bit(du, DU_STATE_ARMED);
			os_atomic_store2o(du._dr, ds_pending_data, ~data, relaxed);
			dux_merge_evt(du._du, EV_ADD|EV_ENABLE|EV_DISPATCH, data, 0);
		}
	}
}

static void
_dispatch_wasi_merge_hangup(dispatch_muxnote_t dmn)
{
	dispatch_unote_linkage_t dul, dul_next;
	// deliver EOF to every unote and stop watching the descriptor,
	// mirroring the epoll backend's EPOLLHUP handling
	dmn->dmn_disarmed_events = dmn->dmn_events;

	LIST_FOREACH_SAFE(dul, &dmn->dmn_readers_head, du_link, dul_next) {
		dispatch_unote_t du = _dispatch_unote_linkage_get_unote(dul);
		// consumed by dux_merge_evt()
		_dispatch_retain_unote_owner(du);
		dispatch_unote_state_t du_state = _dispatch_unote_state(du);
		du_state |= DU_STATE_NEEDS_DELETE;
		du_state &= ~DU_STATE_ARMED;
		_dispatch_unote_state_set(du, du_state);
		os_atomic_store2o(du._dr, ds_pending_data, ~(uintptr_t)0, relaxed);
		dux_merge_evt(du._du, EV_DELETE|EV_DISPATCH, 0, 0);
	}
	LIST_FOREACH_SAFE(dul, &dmn->dmn_writers_head, du_link, dul_next) {
		dispatch_unote_t du = _dispatch_unote_linkage_get_unote(dul);
		// consumed by dux_merge_evt()
		_dispatch_retain_unote_owner(du);
		dispatch_unote_state_t du_state = _dispatch_unote_state(du);
		du_state |= DU_STATE_NEEDS_DELETE;
		du_state &= ~DU_STATE_ARMED;
		_dispatch_unote_state_set(du, du_state);
		os_atomic_store2o(du._dr, ds_pending_data, ~(uintptr_t)0, relaxed);
		dux_merge_evt(du._du, EV_DELETE|EV_DISPATCH, 0, 0);
	}
}

#ifdef _WASI_EMULATED_SIGNAL
static bool
_dispatch_wasi_merge_pending_signals(void)
{
	if (!_dispatch_wasi_signal_pending_any) return false;
	_dispatch_wasi_signal_pending_any = false;
	bool merged = false;
	for (int signo = 1; signo < NSIG; signo++) {
		unsigned long count = _dispatch_wasi_signal_pending[signo];
		if (!count) continue;
		// consume unconditionally: a count with no live muxnote must not
		// linger and merge as phantom deliveries into a source registered
		// later for the same signal
		_dispatch_wasi_signal_pending[signo] = 0;
		dispatch_muxnote_t dmn =
				_dispatch_muxnote_find((uint32_t)signo, EVFILT_SIGNAL);
		if (!dmn) continue;
		dispatch_unote_linkage_t dul, dul_next;
		LIST_FOREACH_SAFE(dul, &dmn->dmn_readers_head, du_link, dul_next) {
			dispatch_unote_t du = _dispatch_unote_linkage_get_unote(dul);
			// consumed by dux_merge_evt()
			_dispatch_retain_unote_owner(du);
			dispatch_assert(!dux_needs_rearm(du._du));
			// accumulate (SOURCE_ADD_DATA): a second harvest before the
			// handler latches must not overwrite an undelivered count
			os_atomic_add2o(du._dr, ds_pending_data, count, relaxed);
			dux_merge_evt(du._du, EV_ADD|EV_ENABLE|EV_CLEAR, count, 0);
			merged = true;
		}
	}
	return merged;
}
#else
#define _dispatch_wasi_merge_pending_signals() false
#endif // _WASI_EMULATED_SIGNAL

bool
_dispatch_wasi_has_event_sources(void)
{
	dispatch_muxnote_t dmn;
#ifdef _WASI_EMULATED_SIGNAL
	// a pending (already raised) signal is immediate progress; an armed but
	// idle signal source is not: nothing can raise() while the sole thread
	// is parked
	if (_dispatch_wasi_signal_pending_any) return true;
#endif
	LIST_FOREACH(dmn, &_dispatch_wasi_muxnotes, dmn_list) {
		if (dmn->dmn_filter != EVFILT_SIGNAL &&
				_dispatch_muxnote_armed_events(dmn)) {
			return true;
		}
	}
	return false;
}

// Harvest ready events: pending emulated signals, always-ready descriptors,
// and poll(2) readiness. timeout_ms only applies to the poll() step, and only
// when nothing was already merged; -1 waits indefinitely. Returns true when
// any event was merged (the merged handlers are now queued for the drain).
static bool _dispatch_wasi_poll_harvest_locked(int timeout_ms);

static bool
_dispatch_wasi_poll_harvest(int timeout_ms)
{
	// merge pokes must not request host turns while the muxnote list is
	// being walked (see _dispatch_wasi_harvesting)
	dispatch_assert(!_dispatch_wasi_harvesting);
	_dispatch_wasi_harvesting = true;
	bool merged = _dispatch_wasi_poll_harvest_locked(timeout_ms);
	_dispatch_wasi_harvesting = false;
	// a timed wait can return right after this harvest without another
	// drain step; the merged handlers must still reach the host
	if (merged) _dispatch_wasi_hand_off_pending();
	return merged;
}

// Poll set storage, grown geometrically and reused across harvests: the
// armed-source count is unbounded (it tracks client registrations), so a
// fixed cap would be a load-dependent crash at an arbitrary wait point.
static struct pollfd *_dispatch_wasi_pfds;
static dispatch_muxnote_t *_dispatch_wasi_pfd_dmn;
static size_t _dispatch_wasi_pfd_capacity;

static void
_dispatch_wasi_pollset_reserve(size_t cnt)
{
	if (likely(cnt < _dispatch_wasi_pfd_capacity)) return;
	size_t cap = _dispatch_wasi_pfd_capacity ? _dispatch_wasi_pfd_capacity : 16;
	while (cap <= cnt) cap *= 2;
	struct pollfd *pfds = realloc(_dispatch_wasi_pfds, cap * sizeof(*pfds));
	dispatch_muxnote_t *dmns = realloc(_dispatch_wasi_pfd_dmn,
			cap * sizeof(*dmns));
	if (unlikely(!pfds || !dmns)) {
		DISPATCH_INTERNAL_CRASH(cap, "failed to grow the WASI poll set");
	}
	_dispatch_wasi_pfds = pfds;
	_dispatch_wasi_pfd_dmn = dmns;
	_dispatch_wasi_pfd_capacity = cap;
}

// EOF starvation guard. Some hosts never set poll_oneoff's
// FD_READWRITE_HANGUP flag (wasmtime 47 for pipes, empirically), and
// preview1 offers no other EOF signal (nbytes is reported as a constant 1,
// fd_filestat_get gives no pipe fill), so to the guest a pipe whose peer
// closed is indistinguishable from a readable one: the handler keeps firing.
// That matches Darwin, where descriptors stay readable at EOF and the client
// is expected to read 0 and cancel - but if the client never cancels, the
// sole WASI thread's park loop itself becomes the spin (~500k handler
// fires/sec, measured), silently. Hosts that do report hangup never get
// here: the POLLHUP path below delivers EOF and stops watching the
// descriptor (the epoll backend's EPOLLHUP discipline). For the hosts that
// cannot report it, the port's no-silent-spin policy turns the hot loop
// into a named crash: a park whose poll() reports readiness 100000 times
// within two seconds is spinning, not sleeping - that rate means the polls
// return instantly (under 20us each on average), which a demand-driven
// stream cannot sustain because the reader draining the descriptor makes
// the poll block again. (A per-poll elapsed-time cutoff would be the more
// obvious detector, but host scheduling jitter - Node's event loop pauses
// every few hundred polls - resets it indefinitely; the windowed rate is
// immune to jitter.)
#define DISPATCH_WASI_READY_POLL_SPIN_LIMIT 100000
#define DISPATCH_WASI_READY_POLL_SPIN_WINDOW_NS (2 * NSEC_PER_SEC)
static uint32_t _dispatch_wasi_ready_poll_count;
static uint64_t _dispatch_wasi_ready_poll_window_start;

static bool
_dispatch_wasi_poll_harvest_locked(int timeout_ms)
{
	dispatch_muxnote_t dmn, dmn_next;
	bool merged = false;

	if (_dispatch_wasi_merge_pending_signals()) {
		merged = true;
	}

	nfds_t cnt = 0;
	LIST_FOREACH_SAFE(dmn, &_dispatch_wasi_muxnotes, dmn_list, dmn_next) {
		if (dmn->dmn_filter == EVFILT_SIGNAL) continue;
		uint16_t events = _dispatch_muxnote_armed_events(dmn);
		if (!events) continue;
		if (dmn->dmn_always_ready) {
			_dispatch_wasi_merge_fd_event(dmn, events);
			merged = true;
			continue;
		}
		_dispatch_wasi_pollset_reserve(cnt);
		_dispatch_wasi_pfds[cnt].fd = (int)dmn->dmn_ident;
		_dispatch_wasi_pfds[cnt].events = (short)events;
		_dispatch_wasi_pfds[cnt].revents = 0;
		_dispatch_wasi_pfd_dmn[cnt] = dmn;
		cnt++;
	}
	if (!cnt) return merged;
	struct pollfd *pfds = _dispatch_wasi_pfds;
	dispatch_muxnote_t *pfd_dmn = _dispatch_wasi_pfd_dmn;

	int effective_timeout_ms = merged ? 0 : timeout_ms;
	int rc = poll(pfds, cnt, effective_timeout_ms);
	if (rc < 0) {
		if (errno == EINTR) return merged;
		if (errno == EBADF) {
			// every descriptor in this set belongs to an armed source, so a
			// whole-call BADF - how wasmtime reports a subscription on a
			// closed fd - can only mean one of them was closed while armed
			DISPATCH_CLIENT_CRASH(errno, "file descriptor closed while "
					"dispatch source is armed");
		}
		DISPATCH_CLIENT_CRASH(errno, "poll() failed for armed dispatch "
				"source file descriptors");
	}
	if (effective_timeout_ms && rc > 0) {
		if (_dispatch_wasi_ready_poll_count++ == 0) {
			_dispatch_wasi_ready_poll_window_start = _dispatch_uptime();
		}
		if (unlikely(_dispatch_wasi_ready_poll_count >=
				DISPATCH_WASI_READY_POLL_SPIN_LIMIT)) {
			if (_dispatch_uptime() - _dispatch_wasi_ready_poll_window_start <=
					DISPATCH_WASI_READY_POLL_SPIN_WINDOW_NS) {
				int stuck_fd = pfds[0].fd;
				for (nfds_t i = 0; i < cnt; i++) {
					if (pfds[i].revents) {
						stuck_fd = pfds[i].fd;
						break;
					}
				}
				DISPATCH_CLIENT_CRASH(stuck_fd, "file descriptor for an "
						"armed dispatch source is permanently ready with "
						"the sole thread parked (EOF without "
						"dispatch_source_cancel, or an always-ready "
						"descriptor); cancel fd sources once read() "
						"returns 0");
			}
			_dispatch_wasi_ready_poll_count = 0;
		}
	}
	for (nfds_t i = 0; rc > 0 && i < cnt; i++) {
		uint16_t revents = (uint16_t)pfds[i].revents;
		if (!revents) continue;
		if (revents & POLLNVAL) {
			// the per-subscription shape of the same condition: wasi-libc's
			// ppoll maps a subscription-level BADF error to POLLNVAL (hosts
			// like wasmtime instead fail the whole call, the EBADF branch
			// above)
			DISPATCH_CLIENT_CRASH(pfds[i].fd, "file descriptor closed "
					"while dispatch source is armed");
		}
		if (revents & (POLLIN | POLLOUT)) {
			_dispatch_wasi_merge_fd_event(pfd_dmn[i],
					revents & (POLLIN | POLLOUT));
			merged = true;
		}
		if (revents & (POLLHUP | POLLERR)) {
			_dispatch_wasi_merge_hangup(pfd_dmn[i]);
			merged = true;
		}
	}
	return merged;
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

// Wait for the given uptime deadline, or for any armed event source
// (file-descriptor readiness, a pending emulated signal) to produce work,
// whichever comes first. deadline_uptime_ns == 0 waits for events alone;
// callers must then guarantee an event source is armed. Merged events queue
// their handlers, so the caller's drain loop makes progress on return.
void
_dispatch_wasi_wait_for_events(uint64_t deadline_uptime_ns)
{
	// bounded slice instead of an infinite poll() timeout: WasmKit's host
	// traps on a poll_oneoff with fd subscriptions and no clock subscription
	// (the wasi-libc encoding of timeout -1). Every caller loops and
	// re-checks its condition, so a spurious hourly wakeup is harmless.
	int timeout_ms = 60 * 60 * 1000;
	if (deadline_uptime_ns) {
		uint64_t now = _dispatch_uptime();
		if (deadline_uptime_ns <= now) {
			timeout_ms = 0;
		} else {
			// round up: waking at most 1ms after a timer deadline is within
			// normal timer leeway, waking early would busy-loop
			uint64_t delta_ms =
					(deadline_uptime_ns - now + NSEC_PER_MSEC - 1) /
					NSEC_PER_MSEC;
			timeout_ms = delta_ms > INT_MAX ? INT_MAX : (int)delta_ms;
		}
	}
	if (!_dispatch_wasi_has_event_sources()) {
		// no fd or signal events can arrive: a plain clock sleep keeps
		// nanosecond precision and stays a single poll_oneoff clock
		// subscription, which limited hosts (browser WASI shims) support
		if (deadline_uptime_ns) {
			_dispatch_wasi_sleep_until(deadline_uptime_ns);
		} else {
			DISPATCH_INTERNAL_CRASH(0, "waiting for events with no armed "
					"event source on WASI");
		}
		return;
	}
	_dispatch_wasi_poll_harvest(timeout_ms);
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
	if (_dispatch_wasi_draining || _dispatch_wasi_harvesting) {
		DISPATCH_CLIENT_CRASH(0, "Perform called from a work item");
	}

	// Step 2: Consume the turn if this call owns it. Clear the timer latch.
	if (consumes_scheduled_turn) {
		_dispatch_wasi_host_turn_scheduled = false;
	}
	_dispatch_wasi_armed_timer_unseen = false;

	// Step 3: Run up to max_steps drain steps. Pokes here request no turn.
	_dispatch_wasi_performing = true;
	_dispatch_wasi_poll_harvest(0);
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
