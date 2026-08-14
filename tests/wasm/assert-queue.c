/*
 * dispatch_assert_queue correctness. The lock-owner encoding must keep the
 * sole thread's tid distinct from DLOCK_OWNER_NULL after DLOCK_OWNER_MASK
 * (see _dispatch_tid_self in shims/lock.h); if the tid masks to zero, every
 * unlocked queue looks owned by the current thread, dispatch_assert_queue
 * silently passes off-queue, and dispatch_assert_queue_not traps spuriously.
 *
 * Expected behavior verified here:
 *  - on-queue:  dispatch_assert_queue passes, off-queue assert_queue_not passes
 *  - off-queue: dispatch_assert_queue traps with the standard diagnostic
 */
#include <dispatch/dispatch.h>
#include <stdio.h>

int main(void)
{
	dispatch_queue_t serial = dispatch_queue_create("v2.assertq",
			DISPATCH_QUEUE_SERIAL);
	dispatch_queue_t other = dispatch_queue_create("v2.assertq.other",
			DISPATCH_QUEUE_SERIAL);
	dispatch_sync(serial, ^{
		dispatch_assert_queue(serial);
		dispatch_assert_queue_not(other);
	});
	printf("assert on-queue OK\n");
	fflush(stdout);
	// Not on this queue: must trap, never pass silently.
	dispatch_assert_queue(serial);
	printf("FAIL: off-queue dispatch_assert_queue did not trap\n");
	return 1;
}
