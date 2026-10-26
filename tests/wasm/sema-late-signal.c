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

/*
 * A timed semaphore wait whose deadline lapses while the cooperative drain
 * runs the item that signals it. A 10 ms timer fires during the park of a
 * 200 ms wait. Its handler runs until 50 ms after the wait deadline, then
 * signals. The drain cannot stop the item at the deadline, so the semaphore
 * is signaled by the time the wait can return. The wait must report success
 * and consume the signal. It must never report a timeout with the count left
 * behind, because a later unrelated wait would then succeed with no matching
 * signal.
 *
 * The 190 ms between the timer and the wait deadline lets the park oversleep
 * on a loaded host. The handler waits for an absolute time after the
 * deadline, so the signal always lands after the deadline.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

#define TIMER_MS 10
#define WAIT_MS 200
#define LATE_MS 50

int
main(void)
{
	dispatch_test_start("Dispatch WASI Semaphore Late Signal");

	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW,
			WAIT_MS * NSEC_PER_MSEC);
	__block uint64_t signaled_at = 0;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, TIMER_MS * NSEC_PER_MSEC),
			global, ^{
		uint64_t until = (uint64_t)deadline + LATE_MS * NSEC_PER_MSEC;
		while (wasi_test_now_ns() < until) { }
		signaled_at = wasi_test_now_ns();
		dispatch_semaphore_signal(semaphore);
	});
	long result = dispatch_semaphore_wait(semaphore, deadline);
	test_long("timed wait reports success when the signal lands before "
			"it can return", result, 0);
	test_long("the signal lands after the wait deadline",
			signaled_at > (uint64_t)deadline, 1);
	// The wait consumed the signal, so an immediate retry must time out. This
	// also catches a timed-out wait that left the count behind.
	result = dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW);
	test_long("immediate retry times out because the signal was consumed "
			"exactly once", !result, 0);
	dispatch_release(semaphore);
	test_stop();
	return 0;
}
