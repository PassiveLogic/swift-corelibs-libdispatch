# Audit: blocking-wait call sites vs. the WASI pumping drain

The WASI port satisfies blocking waits by *pumping*: draining pending
dispatch work (and, since the event-source work, harvesting fd readiness and
emulated signals) from inside the wait loop. Pumping can run client callbacks
beneath a caller that is sitting inside a blocking API. This audit enumerates
every call site that reaches the pumping primitives
(`_dispatch_sema4_wait`, `_dispatch_sema4_timedwait`,
`_dispatch_wait_on_address`, and `_dispatch_thread_event_wait`, which is a
thin wrapper over the sema4 pair) and classifies each, so the re-entrancy
surface is a reviewed inventory instead of an open question.

Two invariants hold at every pumping site:

1. **The waiter holds no internal locks while parked.** All four primitives
   are leaf parking APIs; every internal critical section (side locks, once
   gates, source registration mutation) completes before any of them is
   reached.
2. **Nested waits never pump.** A wait issued from inside a drained work item
   crashes immediately (indefinite) or sleeps to its own deadline only
   (timed); see `_dispatch_wasi_in_drain()` in `shims/lock.h`. So pumped
   callbacks run only beneath *top-level* blocking calls, never beneath a
   callback that is itself being drained.

## Pumping call sites

| Site | Reached by | Single-thread behavior | Verdict |
|---|---|---|---|
| `semaphore.c` `_dispatch_semaphore_wait_slow` → `_dispatch_sema4_wait` / `_dispatch_sema4_timedwait` | `dispatch_semaphore_wait` (public API) | Pumps queued work / timers / fd events until signaled or deadline. Nothing satisfiable → named crash. | **Intended.** This is the feature. Client code runs beneath the caller's `wait` - the documented pumping cost. |
| `semaphore.c` `_dispatch_group_wait_slow` → `_dispatch_wait_on_address(&dg->dg_gen)` | `dispatch_group_wait` (public API) | Same as semaphores; loops on the group generation. | **Intended.** |
| `queue.c` `__DISPATCH_WAIT_FOR_QUEUE__` → `_dispatch_thread_event_wait(&dsc->dsc_event)` | Contended `dispatch_sync` / `barrier_sync` on a non-empty queue; `dispatch_block_wait` | The waiter pushes its sync record onto the target queue and parks; pumping drains the queue's prior items; the turnover signals the event (a plain sema4 counter, so the pumped signal is observed on the next loop iteration). Re-entrant sync onto the current queue is caught earlier by the owner check (correct under the `tid << 2` encoding). | **Safe by design.** Prior queue items (client code) run beneath the caller's `sync` - semantically required: those items *must* run before the sync block can. |
| `apply.c` `_dispatch_apply_invoke` → `_dispatch_thread_event_wait(&da->da_event)` | `dispatch_apply` | With one thread, the caller runs every iteration inline; the final iteration signals the event on this same thread *before* the wait executes, so the wait consumes an already-posted count on its fast path and never parks. | **Unreachable as a blocking wait.** Covered by the passing `dispatch_wasi_api_surface` apply check. |
| `source.c` cancel/dispose `DSF_CANCEL_WAITER` loop → `_dispatch_wait_on_address(&ds->dq_atomic_flags)` | Synchronous source cancellation teardown waiting for unregistration to complete | Unregistration executes on the manager/target queue; pumping drains the manager queue, `DSF_DELETED` gets set, the loop exits. If completion would require the very drain the caller is inside (nested), the nested rule crashes instead of hanging. | **Converges via pumping.** Rarely reached; behavior is the same drain-or-crash policy as the public waits. |

## Non-pumping wait sites (spin family) - hardened by this audit

`_dispatch_unfair_lock_lock_slow` and `_dispatch_once_wait` do not use the
pumping primitives; on targets without unfair-lock/futex support they loop
over `_dispatch_thread_switch`. On the sole WASI thread these loops are
reachable only in dead states:

- **Recursive acquisition** (a once initializer re-entering its own
  `dispatch_once`; a callback re-locking a lock its own frame holds) is
  caught *before* the switch by the `"trying to lock recursively"` owner
  check - which works precisely because this branch's `tid << 2` encoding
  keeps the owner distinguishable from `DLOCK_OWNER_NULL`.
- **Any other contention** would mean a lock held by an owner that can never
  run again (there is no other thread). The inherited `_dispatch_thread_switch`
  body for this target was **empty**, turning that state into a silent hot
  spin - the one hang-shaped behavior left in the port. It now crashes with
  `single-threaded WASI deadlock: lock contended with no other thread to
  release it`, consistent with the port's never-hang policy.

Side-lock and unfair-lock critical sections were swept for call-outs while
held (`_dispatch_queue_sidelock_lock` and `_dispatch_unfair_lock_lock`
callers compiled for WASI: queue specifics, side suspend-count transfer,
legacy target-queue setting). The `dispatch_queue_set_specific`
replace/remove path submits the old value's destructor with
`_dispatch_barrier_async_detached_f` *while `dqsh_lock` is held*. That is
harmless here for the same reason it is harmless on threaded platforms: a
poke only records pending work (and, with a host scheduler registered,
requests a later host turn); it never runs the destructor on the submitting
stack. An earlier iteration of the port drained on poke and needed bracket
hooks around this and seven other critical sections; the current submission
policy removes both the hazard and the hooks. `specific-destructor.c` pins
that the destructor runs at the next pump and not inside the call. The
queue-dealloc specifics dispose path was checked too - it runs lock-free.
Every other audited critical section only mutates structure and returns; if
a corrupted state ever produces same-stack lock contention anyway, the new
`_dispatch_thread_switch` crash fires instead of a spin.

## Summary

Every path that can block on single-threaded WASI now terminates in exactly
one of three ways: **satisfied** (by pumped work, a timer, fd readiness, or a
pending emulated signal), **timed out** (after genuinely consuming its
deadline), or a **named crash** for provable deadlocks. No wait can hang and
no wait can spin. That includes the level-triggered edge case: a descriptor
that becomes permanently ready under a park (pipe EOF on a host that never
reports the poll hangup flag, against a source the client never cancels)
crashes with a named diagnostic via the harvest's ready-poll rate guard in
`event_wasi.c` instead of turning the wait loop into a silent hot spin;
`pipe-eof-source.c` pins it. The re-entrancy exposure of pumping is confined to
top-level blocking calls, whose callers already accept work happening
"elsewhere" during the wait on threaded platforms - the WASI difference,
documented in `README.md`, is that "elsewhere" is the same stack.
