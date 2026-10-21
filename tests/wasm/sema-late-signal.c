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
 * A timed semaphore wait whose deadline lapses while the cooperative drain is
 * running the very item that signals it: a 10 ms timer fires during the
 * 20 ms wait's park, and its handler burns 50 ms before signaling. The drain
 * cannot be preempted at the deadline, so by the time the wait can return
 * the semaphore has been signaled - the wait must report success and consume
 * the signal, never "timed out" with the count stranded (which would make a
 * later unrelated wait succeed with no matching signal).
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"
#include "wasi-test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Semaphore Late Signal");

	dispatch_queue_t global = dispatch_get_global_queue(
			DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC),
			global, ^{
		uint64_t until = wasi_test_now_ns() + 50 * NSEC_PER_MSEC;
		while (wasi_test_now_ns() < until) { }
		dispatch_semaphore_signal(semaphore);
	});
	long result = dispatch_semaphore_wait(semaphore,
			dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC));
	test_long("timed wait reports success when the signal lands before "
			"it can return", result, 0);
	// the wait consumed the signal: an immediate retry must time out. This
	// also catches a timed-out wait that stranded the count.
	result = dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW);
	test_long("immediate retry times out because the signal was consumed "
			"exactly once", !result, 0);
	dispatch_release(semaphore);
	test_stop();
	return 0;
}
