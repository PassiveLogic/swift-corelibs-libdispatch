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
 * A timed semaphore wait issued from inside a drained work item pumps
 * nothing: with nobody left to signal it, it sleeps to its own 200 ms
 * deadline and reports a timeout. A 10 s timer on the same serial queue
 * must not extend that wait; the 5 s test timeout catches a wait that sleeps
 * to the timer. The work item itself is pumped by a top-level wait in main.
 */
#include <dispatch/dispatch.h>
#include <stdint.h>

#include <bsdtests.h>
#include "dispatch_test.h"

int
main(void)
{
	dispatch_test_start("Dispatch WASI Nested Timed Wait");

	__block long result = 0;
	__block long elapsed_ms = 0;
	dispatch_queue_t queue = dispatch_queue_create("wasi.nested.timed",
			DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC),
			queue, ^{ });
	dispatch_async(queue, ^{
		uint64_t start = dispatch_time(DISPATCH_TIME_NOW, 0);
		result = dispatch_semaphore_wait(semaphore,
				dispatch_time(DISPATCH_TIME_NOW,
				200 * NSEC_PER_MSEC));
		elapsed_ms = (long)((dispatch_time(DISPATCH_TIME_NOW, 0) - start) /
				NSEC_PER_MSEC);
		dispatch_semaphore_signal(done);
	});
	// top-level wait: pumps the queued item
	dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
	test_long("nested timed semaphore wait times out", !result, 0);
	test_long_greater_than_or_equal("nested timed semaphore wait sleeps to "
			"its 200 ms deadline", elapsed_ms, 200);
	// the pending 10 s timer keeps its own reference to the queue
	dispatch_release(semaphore);
	dispatch_release(done);
	dispatch_release(queue);
	test_stop();
	return 0;
}
